#include "../../library/library.hpp"
#include "../../platform/archive.hpp"
#include "../../platform/storage.hpp"
#include "../../thirdparty/sqlite/sqlite3.h"
#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <iostream>
#include <stdexcept>
#include <thread>
#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#else
#include <sys/stat.h>
#endif
namespace {
void Require(bool value,const char* message){if(!value)throw std::runtime_error(message);}
void Fence(Library::Index& index){auto future=index.Barrier();Require(future.wait_for(std::chrono::seconds(20))==std::future_status::ready,"Library worker did not complete");future.get();auto snapshot=index.GetSnapshot();Require(snapshot->error.empty(),snapshot->error.c_str());}
std::vector<unsigned char> Cartridge(unsigned char region='U'){
    std::vector<unsigned char> bytes(0x8000);for(std::size_t i=0;i<bytes.size();++i)bytes[i]=static_cast<unsigned char>(i*37);
    std::fill(bytes.begin()+0x1f0,bytes.begin()+0x200,' ');bytes[0x1f0]=region;std::copy_n("SEGA",4,bytes.begin()+0x100);std::fill(bytes.begin()+0x150,bytes.begin()+0x180,' ');std::copy_n("TEST GAME",9,bytes.begin()+0x150);return bytes;
}
std::string SevenZip(){for(const auto* name:{"7zz","7z","7za","7zz.exe","7z.exe","7za.exe"}){auto found=Process::FindProgram(name);if(!found.empty())return found;}
#ifdef _WIN32
    for(const auto* env:{"ProgramW6432","ProgramFiles","ProgramFiles(x86)"})if(const char* directory=std::getenv(env)){auto path=Storage::Join(directory,"7-Zip/7z.exe");try{Storage::Stat(path);return path;}catch(const std::exception&){}}
#endif
    throw std::runtime_error("Tests require installed7-Zip");
}
void Environment(const std::string& value){
#ifdef _WIN32
    int n=MultiByteToWideChar(CP_UTF8,0,value.data(),int(value.size()),nullptr,0);std::wstring wide(n,0);MultiByteToWideChar(CP_UTF8,0,value.data(),int(value.size()),&wide[0],n);Require(SetEnvironmentVariableW(L"PATH",wide.c_str())!=0,"Cannot set test PATH");
#else
    Require(setenv("PATH",value.c_str(),1)==0,"Cannot set test PATH");
#endif
}
const Library::Game& Find(const Library::Snapshot& snapshot,const std::string& title){auto found=std::find_if(snapshot.games.begin(),snapshot.games.end(),[&](const Library::Game& game){return game.title==title;});Require(found!=snapshot.games.end(),"Expected indexed game missing");return *found;}
void Basics(const std::string& directory,const std::string& helper){
    const auto root=Storage::Join(directory,u8"roms 日本語"),source=Storage::Join(directory,"packing"),database=Storage::Join(directory,u8"data 保存/library.sqlite3");
    Storage::EnsureDirectory(root);Storage::EnsureDirectory(source);
    const auto image=Cartridge(),europe=Cartridge('E');
    const std::string name="Test Game (U) [!].bin",raw=Storage::Join(root,name),archive=Storage::Join(root,"Test Game.zip");
    Storage::AtomicWrite(Storage::Join(source,name),image);Storage::AtomicWrite(Storage::Join(source,"bad.bin"),{1,2,3});
    std::vector<unsigned char> smd(512+image.size());for(std::size_t block=0;block<image.size();block+=16384)for(std::size_t i=0;i<8192;++i){smd[512+block+i]=image[block+i*2+1];smd[512+block+8192+i]=image[block+i*2];}
    const auto smdPath=Storage::Join(source,"encoded.SMD");Storage::AtomicWrite(smdPath,smd);const auto normalized=RomInfo::Load(smdPath);Require(normalized.bytes==image&&normalized.crc32==RomInfo::Crc32(image)&&normalized.sha256==RomInfo::Sha256(image),"Pure SMD normalization/hash differs from BIN");
    auto cancelled=std::make_shared<std::atomic<bool>>(true);bool stopped=false;try{RomInfo::Load(Storage::Join(source,"not present.bin"),"",cancelled);}catch(const Process::Cancelled&){stopped=true;}Require(stopped,"Cancelled ROM preparation still performed file I/O");
    auto packed=Process::Run({SevenZip(),"a","-tzip","-bd","-bb0","--",archive,Storage::Join(source,name),Storage::Join(source,"bad.bin")},100000);
    Require(packed.exitCode==0,"Cannot create generated archive");Storage::AtomicWrite(raw,image);Storage::AtomicWrite(Storage::Join(root,"Test Game (E) [!].bin"),europe);Storage::AtomicWrite(Storage::Join(root,"Other.bin"),image);Storage::AtomicWrite(Storage::Join(root,u8"nested/日本語.bin"),image);
    const auto save=raw+".srm";std::vector<unsigned char> saved(512,0x5a);Storage::AtomicWrite(save,saved);
    auto inspected=Archive::Inspect(archive);Require(inspected.members.size()==2,"Archive inspection lost a member");Require(std::count_if(inspected.members.begin(),inspected.members.end(),[](const Archive::MemberInfo& item){return item.available;})==1,"Bad archive member hid its valid sibling");Require(Archive::List(archive).size()==1,"Playable archive list did not retain valid sibling");
    Library::Id favorite=0,copyId=0,variantId=0;std::string identity;
    {
        Library::Index index(database);Fence(index);index.Configure({{root,true,"stable-root"}});Fence(index);auto first=index.GetSnapshot();Require(first->totalCopies==6&&first->totalGames==4,"Incorrect game/variant/copy index cardinality");Require(first->progress.inspected==5&&first->progress.errors==1,"Scan did not report archive member diagnostic");
        const auto game=Find(*first,"Test Game");favorite=game.id;Require(game.variants.size()==2,"Regions were not retained as variants");
        auto variant=std::find_if(game.variants.begin(),game.variants.end(),[](const Library::Variant& item){return item.copies.size()==2;});Require(variant!=game.variants.end(),"Raw/archive candidate copies not grouped");variantId=variant->id;
        auto copy=std::find_if(variant->copies.begin(),variant->copies.end(),[](const Library::Copy& item){return !item.locator.member.empty();});Require(copy!=variant->copies.end(),"Archive source missing");copyId=copy->id;identity=copy->saveIdentity;Require(identity==Archive::SaveIdentity(archive,name),"Library changed legacy archive save identity");Require(copy->duplicateCandidate,"Unverified candidate copies must remain labelled");
        auto prepared=RomInfo::Load(archive,name);Require(prepared.bytes==image&&prepared.region=='U'&&prepared.saveIdentity==identity,"Pure cartridge preparation changed ROM/region/save identity");Require(prepared.sha256==RomInfo::Sha256(image),"Prepared cartridge hash differs");
        index.SetFavorite(favorite,true);index.SetPreferredCopy(variantId,copyId);index.SetPreferredVariant(favorite,variantId);index.RecordLoaded(copyId,prepared);index.RecordSession(copyId,73);Fence(index);
        Library::Metadata metadata;metadata.title="Manual title";metadata.developer="Developer";metadata.genre="Platform";metadata.year=1994;index.ApplyMetadata(favorite,metadata,true);Fence(index);Library::Metadata remote;remote.title="Remote title";index.ApplyMetadata(favorite,remote,false);Fence(index);Require(Find(*index.GetSnapshot(),"Manual title").metadata.developer=="Developer","Catalog replaced manual metadata");
        index.Scan();Fence(index);Require(index.GetSnapshot()->progress.unchanged==5&&index.GetSnapshot()->progress.inspected==0,"Unchanged files were unnecessarily rescanned");Require(Find(*index.GetSnapshot(),"Manual title").playSeconds==73,"History duration lost");
        auto changed=image;changed[0x250]^=0x40;const auto other=Storage::Join(root,"Other.bin");Storage::AtomicWrite(other,changed);index.Scan();Fence(index);Require(index.GetSnapshot()->progress.inspected==1&&index.GetSnapshot()->progress.unchanged==4,"Changed file did not invalidate only its own cache");
        Storage::RemoveFile(other);index.Scan();Fence(index);const auto missing=Find(*index.GetSnapshot(),"Other");Require(!missing.variants.front().copies.front().available&&missing.variants.front().copies.front().status=="missing","Completed scan did not retain a missing source diagnostic");Storage::AtomicWrite(other,image);index.Scan();Fence(index);
        Library::Query query;query.favoritesOnly=true;query.genre="platform";query.year=1994;query.region="USA";index.QueryPage(query);Fence(index);Require(index.GetSnapshot()->totalGames==1&&index.GetSnapshot()->games.front().id==favorite,"Favorites/metadata/region filters failed");
        query={};query.limit=1;query.offset=1;index.QueryPage(query);Fence(index);Require(index.GetSnapshot()->games.size()==1&&index.GetSnapshot()->totalGames==4,"Library paging failed");query={};index.QueryPage(query);Fence(index);
        index.Rebuild();Fence(index);const auto rebuilt=Find(*index.GetSnapshot(),"Manual title");Require(rebuilt.favorite&&rebuilt.id==favorite&&rebuilt.playSeconds==73,"Rebuild lost favorite/history/manual metadata");Require(rebuilt.preferredVariant==variantId,"Rebuild lost last selected variant");Require(rebuilt.variants.front().preferredCopy==copyId||rebuilt.variants.back().preferredCopy==copyId,"Rebuild lost preferred copy");
        // Overlapping roots must not expose the same physical locator twice.
        index.Configure({{root,true,"stable-root"},{Storage::Join(root,"nested"),true,"nested-root"}});Fence(index);Require(index.GetSnapshot()->totalCopies==6,"Overlapping roots duplicated a locator");index.Configure({{root,true,"stable-root"}});Fence(index);
        // A blocked archive process is cancelled; unseen existing entries stay.
        const auto tools=Storage::Join(directory,"tools");Storage::EnsureDirectory(tools);
#ifdef _WIN32
        const auto mock=Storage::Join(tools,"7zz.exe");
#else
        const auto mock=Storage::Join(tools,"7zz");
#endif
        Storage::CopyFileAtomic(helper,mock,8*1024*1024);
#ifndef _WIN32
        Require(chmod(mock.c_str(),0700)==0,"Cannot mark external fixture executable");
#endif
        Storage::AtomicWrite(Storage::Join(root,"000 slow.7z"),{1,2,3});const char* old=std::getenv("PATH");const std::string oldPath=old ? old:"";
#ifdef _WIN32
        Environment(tools+";"+oldPath);
#else
        Environment(tools+":"+oldPath);
#endif
        // An unchanged archive must not invoke this deliberately slow backend.
        const auto fast=std::chrono::steady_clock::now();Storage::RemoveFile(Storage::Join(root,"000 slow.7z"));index.Scan();Fence(index);Require(index.GetSnapshot()->progress.inspected==0&&std::chrono::steady_clock::now()-fast<std::chrono::seconds(2),"Cached scan invoked an archive process");Storage::AtomicWrite(Storage::Join(root,"000 slow.7z"),{1,2,3});
        index.Scan(true);const auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(3);while(!index.GetSnapshot()->progress.scanning&&std::chrono::steady_clock::now()<deadline)std::this_thread::sleep_for(std::chrono::milliseconds(2));Require(index.GetSnapshot()->progress.scanning,"Slow scan did not start");const auto start=std::chrono::steady_clock::now();index.CancelScan();Fence(index);Environment(oldPath);Require(std::chrono::steady_clock::now()-start<std::chrono::seconds(2),"Active archive cancellation did not terminate promptly");Require(index.GetSnapshot()->progress.cancelled&&index.GetSnapshot()->totalCopies==6,"Cancelled scan pruned old records");Storage::RemoveFile(Storage::Join(root,"000 slow.7z"));
        // Unavailable root, then same UUID at a moved path, retaining IDs/state.
        index.Configure({{Storage::Join(directory,"not mounted"),true,"stable-root"}});Fence(index);Require(index.GetSnapshot()->totalCopies==6&&Find(*index.GetSnapshot(),"Manual title").favorite,"Unavailable root discarded old records");
        const auto moved=Storage::Join(directory,u8"relocated 保存");Storage::EnsureDirectory(moved);Storage::Walk(root,true,[&](const Storage::File& file){Storage::CopyFileAtomic(file.path,Storage::Join(moved,file.path.substr(root.size()+1)));});
        index.Configure({{moved,true,"stable-root"}});Fence(index);const auto relocated=Find(*index.GetSnapshot(),"Manual title");Require(relocated.id==favorite&&relocated.favorite&&relocated.playSeconds==73,"Root relocation changed persistent game IDs/state");bool preserved=false;for(const auto& v:relocated.variants)for(const auto& c:v.copies)if(c.id==copyId)preserved=true;Require(preserved,"Root relocation changed source ID");
        Require(Storage::ReadBytes(save,1024)==saved,"Library indexing modified existing raw save bytes");
        auto external=Storage::Join(directory,"outside/External.bin");Storage::AtomicWrite(external,image);Storage::AtomicWrite(Storage::Join(directory,"outside/Unselected Neighbor.bin"),image);auto outside=RomInfo::Load(external);index.RecordExternal({external,""},outside,0,"started");index.RecordExternal({external,""},outside,12,"completed");Fence(index);Require(Find(*index.GetSnapshot(),"External").playSeconds==12,"Directly opened external game missing from history");
        // Catalog enrichment covers off-page games and waits for normalized SMD
        // bytes, while preserving manual metadata and ambiguous-match safety.
        const auto lazyPath=Storage::Join(moved,"Lazy SMD.smd");Storage::AtomicWrite(lazyPath,smd);index.Scan();Fence(index);auto lazy=Find(*index.GetSnapshot(),"Lazy SMD");const auto lazyId=lazy.id,lazyCopy=lazy.variants.front().copies.front().id;
        Library::CatalogRecord unique;unique.crc32=RomInfo::Crc32(image);unique.size=image.size();unique.metadata.catalogId="catalog-unique";unique.metadata.title="Catalog title";unique.metadata.genre="Action";unique.metadata.year=1992;
        Library::Query onePage;onePage.limit=1;index.QueryPage(onePage);Fence(index);index.ApplyCatalog({unique});Fence(index);auto facets=index.GetSnapshot();Require(facets->games.size()==1&&std::find(facets->genres.begin(),facets->genres.end(),"Platform")!=facets->genres.end()&&std::find(facets->genres.begin(),facets->genres.end(),"Action")!=facets->genres.end(),"Genre facets were restricted to the current page");Require(std::find(facets->years.begin(),facets->years.end(),1994)!=facets->years.end()&&std::find(facets->years.begin(),facets->years.end(),1992)!=facets->years.end(),"Year facets were restricted to the current page");Require(std::find(facets->regions.begin(),facets->regions.end(),"USA")!=facets->regions.end()&&std::find(facets->regions.begin(),facets->regions.end(),"Europe")!=facets->regions.end(),"Region facets were restricted to the current page");Library::Query catalogFilter;catalogFilter.genre="Action";catalogFilter.year=1992;index.QueryPage(catalogFilter);Fence(index);Require(index.GetSnapshot()->totalGames>=3,"Catalog updates missed games outside the visible page");
        index.QueryPage({});Fence(index);lazy=Find(*index.GetSnapshot(),"Lazy SMD");Require(lazy.metadata.catalogId.empty()&&!lazy.variants.front().copies.front().hasNormalizedCrc,"Encoded SMD CRC was treated as normalized cartridge CRC");index.RecordLoaded(lazyCopy,RomInfo::Load(lazyPath));Fence(index);auto current=index.GetSnapshot();auto enriched=std::find_if(current->games.begin(),current->games.end(),[&](const Library::Game& game){return game.id==lazyId;});Require(enriched!=current->games.end()&&enriched->metadata.catalogId=="catalog-unique"&&enriched->variants.front().copies.front().hasNormalizedCrc,"Lazy SMD load did not trigger cached catalog enrichment");Require(Find(*current,"Manual title").metadata.year==1994,"Bulk catalog replaced manual metadata");
        auto ambiguous=unique;ambiguous.metadata.catalogId="different-record";ambiguous.metadata.title="Ambiguous title";index.ApplyCatalog({unique,ambiguous});Fence(index);current=index.GetSnapshot();auto rejected=std::find_if(current->games.begin(),current->games.end(),[&](const Library::Game& game){return game.id==lazyId;});Require(rejected!=current->games.end()&&rejected->metadata.catalogId.empty(),"Ambiguous catalog CRC retained an automatic match");
        Storage::RemoveFile(external);index.Scan();Fence(index);current=index.GetSnapshot();const auto absent=Find(*current,"External");Require(!absent.variants.front().copies.front().available&&absent.playSeconds==12,"Refresh discarded or failed to diagnose missing external game");Require(std::none_of(current->games.begin(),current->games.end(),[](const Library::Game& game){return game.title=="Unselected Neighbor";}),"External recent scan imported an unselected neighboring game");
    }
    {Library::Index reopened(database);Fence(reopened);auto snapshot=reopened.GetSnapshot();Require(Find(*snapshot,"Manual title").favorite&&Find(*snapshot,"Manual title").playSeconds==73,"Persistent state not retained across database reopen");Require(Find(*snapshot,"External").playSeconds==12,"External-game recent history not persisted");}
    const auto broken=Storage::Join(directory,"broken.sqlite3");std::vector<unsigned char> garbage={'n','o','t',' ','s','q','l','i','t','e'};Storage::AtomicWrite(broken,garbage);
    {Library::Index invalid(broken);const auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(3);while(invalid.GetSnapshot()->error.empty()&&std::chrono::steady_clock::now()<deadline)std::this_thread::sleep_for(std::chrono::milliseconds(2));Require(!invalid.GetSnapshot()->error.empty()&&!invalid.GetSnapshot()->ready,"Invalid database was silently replaced");}
    Require(Storage::ReadBytes(broken,100)==garbage,"Invalid existing database was modified");
    const auto futurePath=Storage::Join(directory,"future.sqlite3");sqlite3* future=nullptr;Require(sqlite3_open(futurePath.c_str(),&future)==SQLITE_OK,"Cannot create future-schema fixture");Require(sqlite3_exec(future,"PRAGMA user_version=99",nullptr,nullptr,nullptr)==SQLITE_OK,"Cannot mark future schema");sqlite3_close(future);const auto futureBytes=Storage::ReadBytes(futurePath,1024*1024);
    {Library::Index invalid(futurePath);const auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(3);while(invalid.GetSnapshot()->error.empty()&&std::chrono::steady_clock::now()<deadline)std::this_thread::sleep_for(std::chrono::milliseconds(2));Require(invalid.GetSnapshot()->error.find("newer")!=std::string::npos,"Future database schema not rejected");}
    Require(Storage::ReadBytes(futurePath,1024*1024)==futureBytes,"Opening unsupported future schema modified its bytes");
}
}
int main(int argc,char** argv){try{Require(argc==3,"Expected writable fixture directory and unsanitized slow archiver executable");Require(RomInfo::Sha256({})=="e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855","SHA256 empty test vector");Require(RomInfo::Sha256({'a','b','c'})=="ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad","SHA256 abc test vector");Require(RomInfo::Crc32({'1','2','3','4','5','6','7','8','9'})==0xcbf43926,"CRC32 independent test vector");Basics(Storage::Join(Storage::Absolute(argv[1]),"run-"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count())),Storage::Absolute(argv[2]));std::cout<<"Library incremental scan, grouping, metadata, history, Unicode, cancellation, rebuild and save identity passed\n";return 0;}catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}}
