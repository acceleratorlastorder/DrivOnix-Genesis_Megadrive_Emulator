#include "library.hpp"
#include "../platform/archive.hpp"
#include "../platform/storage.hpp"
#include "../thirdparty/sqlite/sqlite3.h"
#include <algorithm>
#include <chrono>
#include <condition_variable>
#include <deque>
#include <functional>
#include <map>
#include <mutex>
#include <set>
#include <sstream>
#include <stdexcept>
#include <thread>

namespace {
using Clock=std::chrono::steady_clock;
int64_t Now(){return std::chrono::duration_cast<std::chrono::seconds>(std::chrono::system_clock::now().time_since_epoch()).count();}
std::string Lower(std::string text){for(char& c:text)if(c>='A'&&c<='Z')c=char(c-'A'+'a');return text;}
std::string Trim(const std::string& text){auto first=text.find_first_not_of(" \t\r\n");return first==std::string::npos ? "":text.substr(first,text.find_last_not_of(" \t\r\n")-first+1);}
std::string Base(const std::string& path){auto text=Storage::Filename(path);auto dot=text.find_last_of('.');return dot==std::string::npos ? text:text.substr(0,dot);}
std::string GroupTitle(const std::string& name){auto base=Base(name);auto tag=base.find_first_of("([");auto result=Trim(base.substr(0,tag));return result.empty() ? base:result;}
std::string Extension(const std::string& name){auto dot=name.find_last_of('.');return dot==std::string::npos ? "":Lower(name.substr(dot));}
bool Supported(const std::string& path){const auto ext=Extension(path);return ext==".bin"||ext==".md"||ext==".gen"||ext==".smd"||ext==".zip"||ext==".7z";}
std::string NameRegion(const std::string& name){
    const auto lower=Lower(name);std::string result;
    auto add=[&](const std::string& value){if(!result.empty())result+=" / ";result+=value;};
    if(lower.find("(world)")!=std::string::npos||lower.find("(jue)")!=std::string::npos)return "Japan / USA / Europe";
    if(lower.find("(u)")!=std::string::npos||lower.find("(ue)")!=std::string::npos||lower.find("usa")!=std::string::npos)add("USA");
    if(lower.find("(j)")!=std::string::npos||lower.find("(ju)")!=std::string::npos||lower.find("japan")!=std::string::npos)add("Japan");
    if(lower.find("(e)")!=std::string::npos||lower.find("(ue)")!=std::string::npos||lower.find("europe")!=std::string::npos)add("Europe");
    return result;
}
std::string RegionText(const std::string& text){const auto lower=Lower(Trim(text));if(!lower.empty()&&lower.find_first_not_of("jue ")==std::string::npos){std::string out;if(lower.find('j')!=std::string::npos)out="Japan";if(lower.find('u')!=std::string::npos)out+=(out.empty()?"":" / ")+std::string("USA");if(lower.find('e')!=std::string::npos)out+=(out.empty()?"":" / ")+std::string("Europe");return out;}return text;}
std::string LocatorKey(const std::string& path,const std::string& stamp){
#ifdef _WIN32
    if(!stamp.empty()){auto first=stamp.find(':');auto second=first==std::string::npos ? first:stamp.find(':',first+1);if(second!=std::string::npos)return Lower(path)+"|"+stamp.substr(0,second);}
#else
    (void)stamp;
#endif
    return path;
}
class Statement {
    sqlite3* db_;sqlite3_stmt* statement_=nullptr;
public:
    Statement(sqlite3* db,const char* sql):db_(db){if(sqlite3_prepare_v2(db,sql,-1,&statement_,nullptr)!=SQLITE_OK)throw std::runtime_error(sqlite3_errmsg(db));}
    ~Statement(){sqlite3_finalize(statement_);}
    Statement(const Statement&)=delete;
    void Text(int index,const std::string& value){if(sqlite3_bind_text(statement_,index,value.data(),int(value.size()),SQLITE_TRANSIENT)!=SQLITE_OK)throw std::runtime_error(sqlite3_errmsg(db_));}
    void Int(int index,int64_t value){if(sqlite3_bind_int64(statement_,index,value)!=SQLITE_OK)throw std::runtime_error(sqlite3_errmsg(db_));}
    bool Step(){int code=sqlite3_step(statement_);if(code!=SQLITE_ROW&&code!=SQLITE_DONE)throw std::runtime_error(sqlite3_errmsg(db_));return code==SQLITE_ROW;}
    int64_t Number(int index)const{return sqlite3_column_int64(statement_,index);}
    std::string String(int index)const{const auto* text=sqlite3_column_text(statement_,index);return text ? std::string(reinterpret_cast<const char*>(text),std::size_t(sqlite3_column_bytes(statement_,index))):"";}
};
void Execute(sqlite3* db,const char* sql){char* message=nullptr;if(sqlite3_exec(db,sql,nullptr,nullptr,&message)!=SQLITE_OK){std::string text=message ? message:sqlite3_errmsg(db);sqlite3_free(message);throw std::runtime_error(text);}}
struct Transaction {
    sqlite3* db;bool committed=false;
    explicit Transaction(sqlite3* value):db(value){Execute(db,"BEGIN IMMEDIATE");}
    void Commit(){Execute(db,"COMMIT");committed=true;}
    ~Transaction(){if(!committed)sqlite3_exec(db,"ROLLBACK",nullptr,nullptr,nullptr);}
};
struct IndexedMember {Archive::MemberInfo member;std::string headerTitle,hash,region;};
}

struct Library::Index::Impl {
    std::string path;
    sqlite3* database=nullptr;
    std::thread worker;
    mutable std::mutex mutex;
    std::condition_variable changed;
    struct Command {bool scan=false;std::function<void()> run;};
    std::deque<Command> commands;
    std::shared_ptr<const Snapshot> snapshot=std::make_shared<Snapshot>();
    Process::CancelToken cancel;
    std::vector<std::weak_ptr<std::atomic<bool>>> scanTokens;
    std::atomic<bool> stopping{false};
    Query query;
    std::map<std::pair<uint32_t,uint64_t>,std::vector<Metadata>> catalog;
    Progress progress;
    std::string error;
    uint64_t revision=0;
    bool ready=false;
    Clock::time_point lastPublish=Clock::now();
    explicit Impl(std::string value):path(std::move(value)){worker=std::thread([this]{Run();});}
    ~Impl(){stopping=true;Cancel();changed.notify_all();if(worker.joinable())worker.join();}
    void Cancel(){std::lock_guard<std::mutex> lock(mutex);if(cancel)cancel->store(true);for(const auto& weak:scanTokens)if(auto token=weak.lock())token->store(true);}
    Process::CancelToken NewToken(){std::lock_guard<std::mutex> lock(mutex);scanTokens.erase(std::remove_if(scanTokens.begin(),scanTokens.end(),[](const std::weak_ptr<std::atomic<bool>>& weak){return weak.expired();}),scanTokens.end());auto token=std::make_shared<std::atomic<bool>>(false);scanTokens.push_back(token);return token;}
    void Enqueue(std::function<void()> action,bool scan=false){
        {std::lock_guard<std::mutex> lock(mutex);if(stopping)return;commands.push_back({scan,std::move(action)});}changed.notify_one();
    }
    void Open(){
        Storage::EnsureDirectory(Storage::Parent(path));
        if(sqlite3_open_v2(path.c_str(),&database,SQLITE_OPEN_READWRITE|SQLITE_OPEN_CREATE|SQLITE_OPEN_FULLMUTEX,nullptr)!=SQLITE_OK)throw std::runtime_error(database ? sqlite3_errmsg(database):"Cannot open library database");
        sqlite3_busy_timeout(database,2000);sqlite3_limit(database,SQLITE_LIMIT_LENGTH,16*1024*1024);sqlite3_db_config(database,SQLITE_DBCONFIG_DEFENSIVE,1,nullptr);
        {Statement version(database,"PRAGMA user_version");version.Step();if(version.Number(0)>1)throw std::runtime_error("Library database was created by a newer DrivOnix version");}
        {Statement identity(database,"PRAGMA application_id");identity.Step();if(identity.Number(0)!=0&&identity.Number(0)!=0x4452564f)throw std::runtime_error("Database belongs to another application");}
        Execute(database,"PRAGMA foreign_keys=ON; PRAGMA journal_mode=WAL; PRAGMA synchronous=FULL;");
        Transaction transaction(database);
        Execute(database,R"sql(
CREATE TABLE IF NOT EXISTS index_meta(key TEXT PRIMARY KEY,value TEXT NOT NULL);
CREATE TABLE IF NOT EXISTS roots(key TEXT PRIMARY KEY,path TEXT NOT NULL,recursive INTEGER NOT NULL,enabled INTEGER NOT NULL DEFAULT 1,error TEXT NOT NULL DEFAULT '');
CREATE TABLE IF NOT EXISTS files(id INTEGER PRIMARY KEY,root_key TEXT NOT NULL REFERENCES roots(key),relative_path TEXT NOT NULL,stamp TEXT NOT NULL DEFAULT '',status TEXT NOT NULL DEFAULT '',error TEXT NOT NULL DEFAULT '',seen INTEGER NOT NULL DEFAULT 0,UNIQUE(root_key,relative_path));
CREATE TABLE IF NOT EXISTS games(id INTEGER PRIMARY KEY,group_key TEXT NOT NULL UNIQUE,title TEXT NOT NULL,favorite INTEGER NOT NULL DEFAULT 0,manual INTEGER NOT NULL DEFAULT 0,catalog TEXT NOT NULL DEFAULT '',meta_title TEXT NOT NULL DEFAULT '',developer TEXT NOT NULL DEFAULT '',publisher TEXT NOT NULL DEFAULT '',genre TEXT NOT NULL DEFAULT '',description TEXT NOT NULL DEFAULT '',artwork TEXT NOT NULL DEFAULT '',region TEXT NOT NULL DEFAULT '',year INTEGER NOT NULL DEFAULT 0,players INTEGER NOT NULL DEFAULT 0);
CREATE TABLE IF NOT EXISTS variants(id INTEGER PRIMARY KEY,game_id INTEGER NOT NULL REFERENCES games(id),key TEXT NOT NULL UNIQUE,title TEXT NOT NULL,region TEXT NOT NULL DEFAULT '',tags TEXT NOT NULL DEFAULT '',preferred INTEGER NOT NULL DEFAULT 0);
CREATE TABLE IF NOT EXISTS sources(id INTEGER PRIMARY KEY,file_id INTEGER NOT NULL REFERENCES files(id),member TEXT NOT NULL,variant_id INTEGER NOT NULL REFERENCES variants(id),size INTEGER NOT NULL DEFAULT 0,crc INTEGER NOT NULL DEFAULT 0,has_crc INTEGER NOT NULL DEFAULT 0,available INTEGER NOT NULL DEFAULT 0,status TEXT NOT NULL DEFAULT '',error TEXT NOT NULL DEFAULT '',hash TEXT NOT NULL DEFAULT '',header_title TEXT NOT NULL DEFAULT '',UNIQUE(file_id,member));
CREATE TABLE IF NOT EXISTS sessions(id INTEGER PRIMARY KEY,source_id INTEGER NOT NULL REFERENCES sources(id),started INTEGER NOT NULL,seconds INTEGER NOT NULL,outcome TEXT NOT NULL,hash TEXT NOT NULL DEFAULT '');
CREATE TABLE IF NOT EXISTS normalized(source_id INTEGER PRIMARY KEY REFERENCES sources(id),size INTEGER NOT NULL,crc INTEGER NOT NULL,hash TEXT NOT NULL);
CREATE INDEX IF NOT EXISTS source_variant ON sources(variant_id);CREATE INDEX IF NOT EXISTS session_source ON sessions(source_id);
PRAGMA user_version=1; PRAGMA application_id=1146246735;
)sql");
        {Statement cached(database,"SELECT value FROM index_meta WHERE key='index_version'");bool current=cached.Step()&&cached.String(0)=="1";if(!current){Execute(database,"UPDATE files SET stamp=''; INSERT INTO index_meta(key,value) VALUES('index_version','1') ON CONFLICT(key) DO UPDATE SET value='1';");}}
        transaction.Commit();ready=true;
    }
    void Run(){
        try{Open();Publish();}catch(const std::exception& failure){error=failure.what();Publish(false);}
        for(;;){Command command;{std::unique_lock<std::mutex> lock(mutex);changed.wait(lock,[this]{return stopping||!commands.empty();});if(commands.empty()&&stopping)break;command=std::move(commands.front());commands.pop_front();}
            if(command.scan&&stopping)continue;
            try{if(!ready)throw std::runtime_error(error.empty() ? "Library database unavailable":error);command.run();Publish();}
            catch(const Process::Cancelled&){progress.scanning=false;progress.cancelled=true;Publish();}
            catch(const std::exception& failure){error=failure.what();progress.scanning=false;Publish(false);}
        }
        if(database)sqlite3_close(database);
    }
    void Pulse(){
        Process::CheckCancelled(cancel);
        for(unsigned count=0;count<16;++count){Command command;{std::lock_guard<std::mutex> lock(mutex);if(commands.empty()||commands.front().scan)break;command=std::move(commands.front());commands.pop_front();}command.run();}
        if(Clock::now()-lastPublish>std::chrono::milliseconds(150))Publish();
    }
    Id GameId(const std::string& title){
        const auto key=Lower(title);{Statement insert(database,"INSERT OR IGNORE INTO games(group_key,title) VALUES(?,?)");insert.Text(1,key);insert.Text(2,title);insert.Step();}
        Statement find(database,"SELECT id FROM games WHERE group_key=?");find.Text(1,key);if(!find.Step())throw std::runtime_error("Cannot create game index record");return find.Number(0);
    }
    Id VariantId(const std::string& name,const IndexedMember& item){
        const auto group=GroupTitle(name);const Id game=GameId(group);
        const auto key=std::to_string(game)+"|"+Storage::Filename(name)+"|"+std::to_string(item.member.size)+"|"+(item.member.hasCrc ? std::to_string(item.member.crc32):"unknown");
        {Statement insert(database,"INSERT OR IGNORE INTO variants(game_id,key,title,region,tags) VALUES(?,?,?,?,?)");insert.Int(1,game);insert.Text(2,key);insert.Text(3,Base(name));insert.Text(4,item.region.empty() ? NameRegion(name):RegionText(item.region));insert.Text(5,Trim(Base(name).substr(std::min(group.size(),Base(name).size()))));insert.Step();}
        Statement find(database,"SELECT id FROM variants WHERE key=?");find.Text(1,key);find.Step();return find.Number(0);
    }
    Id FileId(const std::string& root,const std::string& relative){
        {Statement insert(database,"INSERT OR IGNORE INTO files(root_key,relative_path) VALUES(?,?)");insert.Text(1,root);insert.Text(2,relative);insert.Step();}
        Statement find(database,"SELECT id FROM files WHERE root_key=? AND relative_path=?");find.Text(1,root);find.Text(2,relative);find.Step();return find.Number(0);
    }
    void Configure(std::vector<Root> roots){
        Transaction transaction(database);Execute(database,"UPDATE roots SET enabled=0 WHERE key NOT LIKE 'external:%'");std::set<std::string> identities;
        for(auto& root:roots){root.path=Storage::Absolute(root.path);const auto key=root.id.empty() ? "path:"+root.path:root.id;if(!identities.insert(key).second)throw std::runtime_error("Duplicate library root identity");
            Statement upsert(database,"INSERT INTO roots(key,path,recursive,enabled) VALUES(?,?,?,1) ON CONFLICT(key) DO UPDATE SET path=excluded.path,recursive=excluded.recursive,enabled=1");upsert.Text(1,key);upsert.Text(2,root.path);upsert.Int(3,root.recursive);upsert.Step();}
        transaction.Commit();
    }
    std::vector<IndexedMember> Inspect(const std::string& path,const std::string& only={}){
        std::vector<IndexedMember> result;
        if(Archive::IsArchive(path)){
            for(const auto& member:Archive::Inspect(path,cancel).members)if(only.empty()||member.name==only){IndexedMember item;item.member=member;result.push_back(std::move(item));}
            if(result.empty()&&!only.empty())throw std::runtime_error("Archive contains no selected supported cartridge members");
        }else{
            IndexedMember item;item.member.name="";item.member.size=Storage::Stat(path).size;item.member.status="available";item.member.available=true;
            if(item.member.size<512||item.member.size>0x800000+(Extension(path)==".smd" ? 512:0)){item.member.available=false;item.member.status="unsupported-size";item.member.diagnostic="ROM requires a complete header and at most 8 MiB";}
            else{
                auto bytes=Storage::ReadBytes(path,0x800000+512,cancel);item.member.crc32=RomInfo::Crc32(bytes);item.member.hasCrc=true;
                if(Extension(path)==".smd"){
                    if(bytes.size()<=512||(bytes.size()-512)%16384){item.member.available=false;item.member.status="invalid-smd";item.member.diagnostic="SMD ROM requires complete 16 KiB blocks";}
                    else{std::vector<unsigned char> header(512);for(unsigned i=0;i<256;++i){header[i*2]=bytes[512+8192+i];header[i*2+1]=bytes[512+i];}auto info=RomInfo::ProbeHeader(header);item.headerTitle=info.title;item.region=info.regions;}
                }else{auto info=RomInfo::ProbeHeader(bytes);item.headerTitle=info.title;item.region=info.regions;}
            }
            result.push_back(std::move(item));
        }return result;
    }
    void StoreFile(Id file,const Storage::File& stamp,const std::vector<IndexedMember>& members,int64_t generation,bool partial=false){
        Transaction transaction(database);
        if(!partial){Statement missing(database,"UPDATE sources SET available=0,status='missing',error='Member no longer present' WHERE file_id=?");missing.Int(1,file);missing.Step();}
        for(const auto& item:members){const auto name=item.member.name.empty() ? Storage::Filename(stamp.path):item.member.name;const auto variant=VariantId(name,item);
            Statement write(database,"INSERT INTO sources(file_id,member,variant_id,size,crc,has_crc,available,status,error,hash,header_title) VALUES(?,?,?,?,?,?,?,?,?,?,?) ON CONFLICT(file_id,member) DO UPDATE SET variant_id=excluded.variant_id,size=excluded.size,crc=excluded.crc,has_crc=excluded.has_crc,available=excluded.available,status=excluded.status,error=excluded.error,hash=CASE WHEN sources.crc=excluded.crc AND sources.size=excluded.size THEN sources.hash ELSE excluded.hash END,header_title=CASE WHEN excluded.header_title='' THEN sources.header_title ELSE excluded.header_title END");
            write.Int(1,file);write.Text(2,item.member.name);write.Int(3,variant);write.Int(4,int64_t(item.member.size));write.Int(5,item.member.crc32);write.Int(6,item.member.hasCrc);write.Int(7,item.member.available);write.Text(8,item.member.status);write.Text(9,item.member.diagnostic);write.Text(10,item.hash);write.Text(11,item.headerTitle);write.Step();
        }
        Statement update(database,"UPDATE files SET stamp=?,status='indexed',error='',seen=? WHERE id=?");update.Text(1,stamp.stamp);update.Int(2,generation);update.Int(3,file);update.Step();transaction.Commit();
    }
    void Scan(bool force,const Process::CancelToken& requested={}){
        if(stopping)return;
        {std::lock_guard<std::mutex> lock(mutex);cancel=requested ? requested:std::make_shared<std::atomic<bool>>(false);}
        Process::CheckCancelled(cancel);
        error.clear();progress={};progress.scanning=true;Publish();
        Statement rootQuery(database,"SELECT key,path,recursive FROM roots WHERE enabled=1 AND key NOT LIKE 'external:%'");
        struct ScanRoot{std::string key,path;bool recursive;};std::vector<ScanRoot> roots;while(rootQuery.Step())roots.push_back({rootQuery.String(0),rootQuery.String(1),rootQuery.Number(2)!=0});
        Statement next(database,"SELECT COALESCE(MAX(seen),0)+1 FROM files");next.Step();const int64_t generation=next.Number(0);
        for(const auto& root:roots){
            try{
                Storage::Walk(root.path,root.recursive,[&](const Storage::File& file){
                    Pulse();if(!Supported(file.path))return;++progress.files;progress.current=file.path;
                    const auto relative=file.path.substr(root.path.size()+(root.path.back()=='/' ? 0:1));const Id id=FileId(root.key,relative);
                    Statement cached(database,"SELECT stamp,status FROM files WHERE id=?");cached.Int(1,id);cached.Step();
                    if(!force&&cached.String(0)==file.stamp&&(cached.String(1)=="indexed"||cached.String(1)=="unsupported")){
                        Statement seen(database,"UPDATE files SET seen=? WHERE id=?");seen.Int(1,generation);seen.Int(2,id);seen.Step();++progress.unchanged;return;}
                    try{
                        auto members=Inspect(file.path);Process::CheckCancelled(cancel);if(Storage::Stat(file.path).stamp!=file.stamp)throw std::runtime_error("File changed while indexing; retry on next scan");
                        StoreFile(id,file,members,generation);++progress.inspected;
                        if(members.empty()){++progress.errors;Statement unsupported(database,"UPDATE files SET status='unsupported',error='Archive contains no supported Mega Drive cartridge members' WHERE id=?");unsupported.Int(1,id);unsupported.Step();}
                        for(const auto& member:members)if(!member.member.available)++progress.errors;
                    }catch(const Process::Cancelled&){throw;}catch(const std::exception& failure){
                        ++progress.errors;Statement failed(database,"UPDATE files SET status='error',error=?,seen=? WHERE id=?");failed.Text(1,failure.what());failed.Int(2,generation);failed.Int(3,id);failed.Step();
                    }
                },cancel);
                Process::CheckCancelled(cancel);
                Transaction transaction(database);
                Statement missing(database,"UPDATE files SET status='missing',error='File no longer present' WHERE root_key=? AND seen<>?");missing.Text(1,root.key);missing.Int(2,generation);missing.Step();
                Statement good(database,"UPDATE roots SET error='' WHERE key=?");good.Text(1,root.key);good.Step();transaction.Commit();
            }catch(const Process::Cancelled&){throw;}catch(const std::exception& failure){++progress.errors;Statement failed(database,"UPDATE roots SET error=? WHERE key=?");failed.Text(1,failure.what());failed.Text(2,root.key);failed.Step();}
        }
        // Directly opened games are refreshed individually, never by walking
        // their parent folders or importing other archive members.
        Statement externalFiles(database,"SELECT f.id,r.path,f.relative_path,f.stamp,f.status FROM files f JOIN roots r ON r.key=f.root_key WHERE r.enabled=1 AND r.key LIKE 'external:%'");
        struct ExternalFile{Id id;std::string path,stamp,status;};std::vector<ExternalFile> external;
        while(externalFiles.Step())external.push_back({externalFiles.Number(0),Storage::Join(externalFiles.String(1),externalFiles.String(2)),externalFiles.String(3),externalFiles.String(4)});
        for(const auto& file:external){
            Pulse();++progress.files;progress.current=file.path;
            try{
                auto stamp=Storage::Stat(file.path);
                if(!force&&stamp.stamp==file.stamp&&(file.status=="indexed"||file.status=="unsupported")){++progress.unchanged;continue;}
                std::set<std::string> selected;Statement prior(database,"SELECT member FROM sources WHERE file_id=?");prior.Int(1,file.id);while(prior.Step())selected.insert(prior.String(0));
                auto members=Inspect(file.path);members.erase(std::remove_if(members.begin(),members.end(),[&](const IndexedMember& item){return !selected.count(item.member.name);}),members.end());
                Process::CheckCancelled(cancel);if(Storage::Stat(file.path).stamp!=stamp.stamp)throw std::runtime_error("External game changed while indexing");
                StoreFile(file.id,stamp,members,generation);++progress.inspected;
            }catch(const Process::Cancelled&){throw;}catch(const std::exception& failure){++progress.errors;Statement failed(database,"UPDATE files SET status='unavailable',error=? WHERE id=?");failed.Text(1,failure.what());failed.Int(2,file.id);failed.Step();}
        }
        EnrichCatalog();progress.scanning=false;progress.current.clear();Publish();
    }
    void Publish(bool queryDatabase=true){
        auto result=std::make_shared<Snapshot>();result->revision=++revision;result->ready=ready;result->error=error;result->progress=progress;result->busy=progress.scanning;
        if(queryDatabase&&ready){
            std::map<Id,Game> games;std::map<Id,std::pair<Id,std::size_t>> variants;std::set<std::pair<std::string,std::string>> locators;
            Statement rows(database,R"sql(SELECT g.id,g.title,g.favorite,g.manual,g.catalog,g.meta_title,g.developer,g.publisher,g.genre,g.description,g.artwork,g.region,g.year,g.players,
v.id,v.title,v.region,v.tags,v.preferred,c.id,c.member,c.size,c.crc,c.has_crc,c.available,c.status,c.error,c.hash,f.relative_path,f.status,f.error,r.path,r.error,n.size,n.crc,n.source_id,COALESCE(CAST(p.value AS INTEGER),0),f.stamp
FROM sources c JOIN variants v ON v.id=c.variant_id JOIN games g ON g.id=v.game_id JOIN files f ON f.id=c.file_id JOIN roots r ON r.key=f.root_key LEFT JOIN normalized n ON n.source_id=c.id AND n.hash=c.hash LEFT JOIN index_meta p ON p.key=('preferred_variant:'||g.id) WHERE r.enabled=1 ORDER BY g.id,v.id,c.id)sql");
            while(rows.Step()){
                if(!locators.emplace(LocatorKey(Storage::Join(rows.String(31),rows.String(28)),rows.String(37)),rows.String(20)).second)continue;
                auto& game=games[rows.Number(0)];if(!game.id){game.id=rows.Number(0);game.title=rows.String(5).empty() ? rows.String(1):rows.String(5);game.favorite=rows.Number(2)!=0;game.manualOverride=rows.Number(3)!=0;game.metadata.catalogId=rows.String(4);game.metadata.title=rows.String(5);game.metadata.developer=rows.String(6);game.metadata.publisher=rows.String(7);game.metadata.genre=rows.String(8);game.metadata.description=rows.String(9);game.metadata.artworkPath=rows.String(10);game.metadata.region=rows.String(11);game.metadata.year=int(rows.Number(12));game.metadata.players=int(rows.Number(13));game.preferredVariant=rows.Number(36);}
                const Id variantId=rows.Number(14);auto found=variants.find(variantId);
                if(found==variants.end()){Variant variant;variant.id=variantId;variant.title=rows.String(15);variant.region=rows.String(16);variant.tags=rows.String(17);variant.preferredCopy=rows.Number(18);game.variants.push_back(std::move(variant));found=variants.emplace(variantId,std::make_pair(game.id,game.variants.size()-1)).first;}
                auto& variant=game.variants[found->second.second];Copy copy;copy.id=rows.Number(19);copy.locator.path=Storage::Join(rows.String(31),rows.String(28));copy.locator.member=rows.String(20);copy.size=uint64_t(rows.Number(21));copy.crc32=uint32_t(rows.Number(22));copy.hasCrc=rows.Number(23)!=0;copy.status=rows.String(25);copy.error=rows.String(26);copy.sha256=rows.String(27);copy.available=rows.Number(24)!=0&&rows.String(29)=="indexed"&&rows.String(32).empty();
                if(!copy.available){if(!rows.String(32).empty()){copy.status="root-unavailable";copy.error=rows.String(32);}else if(rows.String(29)!="indexed"){copy.status=rows.String(29);copy.error=rows.String(30);}}
                if(copy.locator.member.empty())copy.saveIdentity=copy.locator.path;
                else{try{copy.saveIdentity=Archive::SaveIdentity(copy.locator.path,copy.locator.member);}catch(const std::exception&){copy.saveIdentity.clear();}}
                if(rows.Number(35)){copy.normalizedSize=uint64_t(rows.Number(33));copy.normalizedCrc32=uint32_t(rows.Number(34));copy.hasNormalizedCrc=true;}
                else if(Extension(copy.locator.member.empty()?copy.locator.path:copy.locator.member)!=".smd"){copy.normalizedSize=copy.size;copy.normalizedCrc32=copy.crc32;copy.hasNormalizedCrc=copy.hasCrc;}
                variant.copies.push_back(std::move(copy));
            }
            Statement history(database,"SELECT v.game_id,SUM(s.seconds),MAX(s.started) FROM sessions s JOIN sources c ON c.id=s.source_id JOIN variants v ON v.id=c.variant_id GROUP BY v.game_id");while(history.Step()){auto found=games.find(history.Number(0));if(found!=games.end()){found->second.playSeconds=uint64_t(history.Number(1));found->second.lastPlayed=history.Number(2);}}
            std::map<std::string,std::string> genres,regions;std::set<int> years;
            auto addRegion=[&](const std::string& raw){std::string text=RegionText(raw);std::size_t start=0;while(start<=text.size()){auto end=text.find_first_of("/,|",start);auto region=Trim(text.substr(start,end==std::string::npos?end:end-start));if(!region.empty())regions.emplace(Lower(region),region);if(end==std::string::npos)break;start=end+1;}};
            for(const auto& record:games){const auto& game=record.second;if(!game.metadata.genre.empty())genres.emplace(Lower(game.metadata.genre),game.metadata.genre);if(game.metadata.year>0)years.insert(game.metadata.year);addRegion(game.metadata.region);for(const auto& variant:game.variants)addRegion(variant.region);}
            for(const auto& value:genres)result->genres.push_back(value.second);
            for(const auto& value:regions)result->regions.push_back(value.second);
            result->years.assign(years.begin(),years.end());
            std::vector<Game> matching;
            for(auto& record:games){auto& game=record.second;bool region=query.region.empty()||Lower(game.metadata.region).find(Lower(query.region))!=std::string::npos;std::string searchable=Lower(game.title+" "+game.metadata.developer+" "+game.metadata.publisher+" "+game.metadata.genre);
                for(auto& variant:game.variants){if(Lower(variant.region).find(Lower(query.region))!=std::string::npos)region=true;searchable+=" "+Lower(variant.title);for(auto& copy:variant.copies){const bool confirmed=!copy.sha256.empty()&&std::count_if(variant.copies.begin(),variant.copies.end(),[&](const Copy& other){return other.sha256==copy.sha256;})>1;copy.duplicateCandidate=variant.copies.size()>1&&!confirmed;}if(!query.includeUnavailable)variant.copies.erase(std::remove_if(variant.copies.begin(),variant.copies.end(),[](const Copy& copy){return !copy.available;}),variant.copies.end());}
                game.variants.erase(std::remove_if(game.variants.begin(),game.variants.end(),[](const Variant& variant){return variant.copies.empty();}),game.variants.end());
                if(game.variants.empty()||(query.favoritesOnly&&!game.favorite)||(query.recentOnly&&!game.lastPlayed)||!region||(!query.genre.empty()&&Lower(game.metadata.genre).find(Lower(query.genre))==std::string::npos)||(query.year&&game.metadata.year!=query.year)||(!query.search.empty()&&searchable.find(Lower(query.search))==std::string::npos))continue;
                matching.push_back(std::move(game));}
            std::sort(matching.begin(),matching.end(),[&](const Game& a,const Game& b){if(query.sort==Sort::LastPlayed&&a.lastPlayed!=b.lastPlayed)return a.lastPlayed>b.lastPlayed;if(query.sort==Sort::MostPlayed&&a.playSeconds!=b.playSeconds)return a.playSeconds>b.playSeconds;const auto left=Lower(a.title),right=Lower(b.title);return left==right ? a.id<b.id:left<right;});
            result->totalGames=matching.size();for(const auto& game:matching)for(const auto& variant:game.variants){++result->totalVariants;result->totalCopies+=variant.copies.size();}
            const auto first=std::min(query.offset,matching.size()),last=std::min(first+std::min<std::size_t>(query.limit,200),matching.size());for(std::size_t i=first;i<last;++i)result->games.push_back(std::move(matching[i]));
        }else if(ready){std::lock_guard<std::mutex> lock(mutex);result->games=snapshot->games;result->totalGames=snapshot->totalGames;result->totalVariants=snapshot->totalVariants;result->totalCopies=snapshot->totalCopies;result->genres=snapshot->genres;result->regions=snapshot->regions;result->years=snapshot->years;}
        {std::lock_guard<std::mutex> lock(mutex);snapshot=std::move(result);}lastPublish=Clock::now();
    }
    void Loaded(Id copy,const RomInfo::Prepared& prepared){Transaction transaction(database);Statement update(database,"UPDATE sources SET hash=?,header_title=? WHERE id=?");update.Text(1,prepared.sha256);update.Text(2,prepared.title);update.Int(3,copy);update.Step();Statement normalized(database,"INSERT INTO normalized(source_id,size,crc,hash) VALUES(?,?,?,?) ON CONFLICT(source_id) DO UPDATE SET size=excluded.size,crc=excluded.crc,hash=excluded.hash");normalized.Int(1,copy);normalized.Int(2,int64_t(prepared.size));normalized.Int(3,prepared.crc32);normalized.Text(4,prepared.sha256);normalized.Step();transaction.Commit();EnrichCatalog();}
    void Session(Id copy,uint64_t seconds,const std::string& outcome){
        Statement find(database,"SELECT hash FROM sources WHERE id=?");find.Int(1,copy);if(!find.Step())throw std::runtime_error("Unknown library source");
        Statement insert(database,"INSERT INTO sessions(source_id,started,seconds,outcome,hash) VALUES(?,?,?,?,?)");insert.Int(1,copy);insert.Int(2,Now());insert.Int(3,int64_t(std::min<uint64_t>(seconds,UINT64_C(315576000))));insert.Text(4,outcome);insert.Text(5,find.String(0));insert.Step();
    }
    void MetadataUpdate(Id game,const Metadata& requested,bool manual){
        Statement existing(database,"SELECT manual,catalog,meta_title,developer,publisher,genre,description,artwork,region,year,players FROM games WHERE id=?");existing.Int(1,game);if(!existing.Step())throw std::runtime_error("Unknown library game");if(existing.Number(0)&&!manual)return;
        Metadata metadata=requested;
        if(!manual){
            std::string* fields[]={&metadata.catalogId,&metadata.title,&metadata.developer,&metadata.publisher,&metadata.genre,&metadata.description,&metadata.artworkPath,&metadata.region};
            for(unsigned i=0;i<8;++i)if(fields[i]->empty())*fields[i]=existing.String(int(i+1));
            if(!metadata.year)metadata.year=int(existing.Number(9));
            if(!metadata.players)metadata.players=int(existing.Number(10));
        }
        const std::string* fields[]={&metadata.catalogId,&metadata.title,&metadata.developer,&metadata.publisher,&metadata.genre,&metadata.description,&metadata.artworkPath,&metadata.region};
        bool differs=bool(existing.Number(0))!=manual||metadata.year!=existing.Number(9)||metadata.players!=existing.Number(10);for(unsigned i=0;i<8;++i)differs=differs||*fields[i]!=existing.String(int(i+1));if(!differs)return;
        Statement update(database,"UPDATE games SET manual=?,catalog=?,meta_title=?,developer=?,publisher=?,genre=?,description=?,artwork=?,region=?,year=?,players=? WHERE id=?");update.Int(1,manual);for(unsigned i=0;i<8;++i)update.Text(int(i+2),*fields[i]);update.Int(10,metadata.year);update.Int(11,metadata.players);update.Int(12,game);update.Step();
    }
    void EnrichCatalog(){
        if(catalog.empty())return;
        Statement rows(database,"SELECT v.game_id,c.size,c.crc,c.has_crc,c.member,f.relative_path,n.size,n.crc,n.source_id FROM sources c JOIN variants v ON v.id=c.variant_id JOIN files f ON f.id=c.file_id LEFT JOIN normalized n ON n.source_id=c.id AND n.hash=c.hash WHERE c.available=1 ORDER BY c.id");
        std::map<Id,Metadata> matches;
        while(rows.Step()){
            uint64_t size=uint64_t(rows.Number(1));uint32_t crc=uint32_t(rows.Number(2));bool known=rows.Number(3)!=0;
            if(rows.Number(8)){size=uint64_t(rows.Number(6));crc=uint32_t(rows.Number(7));known=true;}
            else if(Extension(rows.String(4).empty()?rows.String(5):rows.String(4))==".smd")known=false;
            if(!known||matches.count(rows.Number(0)))continue;
            auto found=catalog.find({crc,size});if(found!=catalog.end()&&found->second.size()==1)matches.emplace(rows.Number(0),found->second.front());
        }
        Transaction transaction(database);
        {Statement previous(database,"SELECT id FROM games WHERE manual=0 AND catalog<>''");std::vector<Id> stale;while(previous.Step())if(!matches.count(previous.Number(0)))stale.push_back(previous.Number(0));for(auto game:stale){Statement clear(database,"UPDATE games SET catalog='',meta_title='',developer='',publisher='',genre='',description='',region='',year=0,players=0 WHERE id=?");clear.Int(1,game);clear.Step();}}
        for(const auto& match:matches)MetadataUpdate(match.first,match.second,false);
        transaction.Commit();
    }
    void Catalog(std::vector<CatalogRecord> records){
        catalog.clear();for(auto& record:records){auto& candidates=catalog[{record.crc32,record.size}];bool repeated=std::any_of(candidates.begin(),candidates.end(),[&](const Metadata& value){return value.catalogId==record.metadata.catalogId&&value.title==record.metadata.title;});if(!repeated)candidates.push_back(std::move(record.metadata));}EnrichCatalog();
    }
    void External(Locator locator,const RomInfo::Prepared& prepared,uint64_t seconds,const std::string& outcome){
        locator.path=Storage::Absolute(locator.path);std::replace(locator.member.begin(),locator.member.end(),'\\','/');Id copy=0;
        Statement existing(database,"SELECT c.id,r.path,f.relative_path,c.member FROM sources c JOIN files f ON f.id=c.file_id JOIN roots r ON r.key=f.root_key WHERE r.enabled=1");while(existing.Step())if(Storage::Join(existing.String(1),existing.String(2))==locator.path&&existing.String(3)==locator.member){copy=existing.Number(0);break;}
        if(!copy){const auto parent=Storage::Parent(locator.path),key="external:"+parent;
            {Statement root(database,"INSERT OR IGNORE INTO roots(key,path,recursive,enabled) VALUES(?,?,0,1)");root.Text(1,key);root.Text(2,parent);root.Step();}
            const auto file=FileId(key,Storage::Filename(locator.path));auto stamp=Storage::Stat(locator.path);auto members=Inspect(locator.path,locator.member);StoreFile(file,stamp,members,Now(),true);
            Statement source(database,"SELECT id FROM sources WHERE file_id=? AND member=?");source.Int(1,file);source.Text(2,locator.member);if(!source.Step())throw std::runtime_error("Cannot record external cartridge");copy=source.Number(0);
        }
        Loaded(copy,prepared);Session(copy,seconds,outcome);
    }
};

Library::Index::Index(std::string path):impl_(new Impl(Storage::Absolute(path))){}
Library::Index::~Index()=default;
void Library::Index::Configure(std::vector<Root> roots,bool scan){impl_->Cancel();auto token=impl_->NewToken();impl_->Enqueue([this,roots=std::move(roots),scan,token]{impl_->Configure(roots);if(scan)impl_->Scan(false,token);},true);}
void Library::Index::Scan(bool force){auto token=impl_->NewToken();impl_->Enqueue([this,force,token]{impl_->Scan(force,token);},true);}
void Library::Index::CancelScan(){impl_->Cancel();}
void Library::Index::QueryPage(Query query){impl_->Enqueue([this,query]{impl_->query=query;});}
std::shared_ptr<const Library::Snapshot> Library::Index::GetSnapshot()const{std::lock_guard<std::mutex> lock(impl_->mutex);return impl_->snapshot;}
void Library::Index::SetFavorite(Id game,bool value){impl_->Enqueue([this,game,value]{Statement update(impl_->database,"UPDATE games SET favorite=? WHERE id=?");update.Int(1,value);update.Int(2,game);update.Step();});}
void Library::Index::SetPreferredCopy(Id variant,Id copy){impl_->Enqueue([this,variant,copy]{Statement update(impl_->database,"UPDATE variants SET preferred=? WHERE id=? AND EXISTS(SELECT 1 FROM sources WHERE id=? AND variant_id=?)");update.Int(1,copy);update.Int(2,variant);update.Int(3,copy);update.Int(4,variant);update.Step();});}
void Library::Index::SetPreferredVariant(Id game,Id variant){impl_->Enqueue([this,game,variant]{Statement update(impl_->database,"INSERT OR REPLACE INTO index_meta(key,value) SELECT ?,? FROM variants WHERE id=? AND game_id=?");update.Text(1,"preferred_variant:"+std::to_string(game));update.Text(2,std::to_string(variant));update.Int(3,variant);update.Int(4,game);update.Step();});}
void Library::Index::ApplyMetadata(Id game,Metadata metadata,bool manual){impl_->Enqueue([this,game,metadata=std::move(metadata),manual]{impl_->MetadataUpdate(game,metadata,manual);});}
void Library::Index::ApplyCatalog(std::vector<CatalogRecord> records){impl_->Enqueue([this,records=std::move(records)]()mutable{impl_->Catalog(std::move(records));});}
void Library::Index::RecordSession(Id copy,uint64_t seconds,const std::string& outcome){impl_->Enqueue([this,copy,seconds,outcome]{impl_->Session(copy,seconds,outcome);});}
void Library::Index::RecordLoaded(Id copy,const RomInfo::Prepared& prepared){RomInfo::Prepared info;info.sha256=prepared.sha256;info.title=prepared.title;info.size=prepared.size?prepared.size:prepared.bytes.size();info.crc32=prepared.crc32;impl_->Enqueue([this,copy,info]{impl_->Loaded(copy,info);});}
void Library::Index::RecordExternal(const Locator& locator,const RomInfo::Prepared& prepared,uint64_t seconds,const std::string& outcome){RomInfo::Prepared info;info.title=prepared.title;info.sha256=prepared.sha256;info.size=prepared.size?prepared.size:prepared.bytes.size();info.crc32=prepared.crc32;impl_->Enqueue([this,locator,info,seconds,outcome]{impl_->External(locator,info,seconds,outcome);});}
void Library::Index::Rebuild(){impl_->Cancel();auto token=impl_->NewToken();impl_->Enqueue([this,token]{Execute(impl_->database,"UPDATE files SET stamp=''");impl_->Scan(true,token);},true);}
std::future<void> Library::Index::Barrier(){auto promise=std::make_shared<std::promise<void>>();auto future=promise->get_future();impl_->Enqueue([promise]{promise->set_value();},true);return future;}
