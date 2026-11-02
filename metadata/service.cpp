#include "service.hpp"
#include "../platform/storage.hpp"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <deque>
#include <iomanip>
#include <map>
#include <mutex>
#include <set>
#include <sstream>
#include <stdexcept>
#include <thread>

namespace {
using Bytes=std::vector<unsigned char>;
using Clock=std::chrono::system_clock;
std::uint64_t Now(){return std::uint64_t(std::chrono::duration_cast<std::chrono::seconds>(Clock::now().time_since_epoch()).count());}
std::string Text(const Bytes& bytes){return {bytes.begin(),bytes.end()};}
Bytes Data(const std::string& text){return {text.begin(),text.end()};}
std::uint32_t Read32(const Bytes& bytes,std::size_t at) {
    return (std::uint32_t(bytes[at])<<24)|(std::uint32_t(bytes[at+1])<<16)|
           (std::uint32_t(bytes[at+2])<<8)|bytes[at+3];
}
std::uint32_t Crc(const unsigned char* bytes,std::size_t length) {
    std::uint32_t value=0xffffffffu;
    for(std::size_t i=0;i<length;++i) {
        value^=bytes[i];for(unsigned bit=0;bit<8;++bit)value=(value>>1)^(0xedb88320u&std::uint32_t(0-int(value&1)));
    }
    return ~value;
}
std::string CacheKey(const std::string& url) {
    // The sidecar stores and verifies the full URL as a collision guard.
    std::uint64_t value=UINT64_C(14695981039346656037);
    for(unsigned char c:url){value^=c;value*=UINT64_C(1099511628211);}
    std::ostringstream out;out<<std::hex<<std::setfill('0')<<std::setw(16)<<value;return out.str();
}
bool Exists(const std::string& path) {
    try{return !Storage::Stat(path).directory;}catch(const std::exception&){return false;}
}
struct Source {std::string category,etag,text;};
const char* const Categories[]={"no-intro","developer","publisher","genre","releaseyear","maxusers"};
void Put32(Bytes& bytes,std::uint32_t value) {
    for(int shift=24;shift>=0;shift-=8)bytes.push_back(static_cast<unsigned char>(value>>shift));
}
void PutString(Bytes& bytes,const std::string& text) {
    if(text.size()>16*1024*1024)throw std::runtime_error("Metadata cache field is too large");
    Put32(bytes,std::uint32_t(text.size()));bytes.insert(bytes.end(),text.begin(),text.end());
}
Bytes Pack(const std::vector<Source>& sources,std::uint64_t timestamp) {
    Bytes bytes={'D','R','V','M','E','T','A','1'};
    Put32(bytes,std::uint32_t(timestamp>>32));Put32(bytes,std::uint32_t(timestamp));Put32(bytes,std::uint32_t(sources.size()));
    for(const auto& source:sources){PutString(bytes,source.category);PutString(bytes,source.etag);PutString(bytes,source.text);}
    if(bytes.size()>32*1024*1024)throw std::runtime_error("Metadata cache is too large");
    return bytes;
}
std::string TakeString(const Bytes& bytes,std::size_t& at,std::size_t limit) {
    if(bytes.size()-at<4)throw std::runtime_error("Truncated metadata cache");
    const auto length=Read32(bytes,at);at+=4;
    if(length>limit||length>bytes.size()-at)throw std::runtime_error("Invalid metadata cache field");
    std::string result(bytes.begin()+at,bytes.begin()+at+length);at+=length;return result;
}
std::vector<Source> Unpack(const Bytes& bytes,std::uint64_t& timestamp) {
    if(bytes.size()<20||std::string(bytes.begin(),bytes.begin()+8)!="DRVMETA1")throw std::runtime_error("Invalid metadata cache header");
    timestamp=(std::uint64_t(Read32(bytes,8))<<32)|Read32(bytes,12);const auto count=Read32(bytes,16);
    if(count!=6)throw std::runtime_error("Invalid metadata cache source count");
    std::vector<Source> result;std::size_t at=20;
    for(unsigned i=0;i<count;++i) {
        Source source;source.category=TakeString(bytes,at,32);source.etag=TakeString(bytes,at,1024);source.text=TakeString(bytes,at,16*1024*1024);
        if(source.category!=Categories[i]||source.etag.find_first_of("\r\n\0",0,3)!=std::string::npos)
            throw std::runtime_error("Invalid metadata cache source");
        result.push_back(std::move(source));
    }
    if(at!=bytes.size())throw std::runtime_error("Unexpected data after metadata cache");
    return result;
}
Metadata::Catalog BuildCatalog(const std::vector<Source>& sources) {
    if(sources.size()!=6||sources.front().text.empty())throw std::runtime_error("Metadata catalog has no base data");
    std::vector<std::pair<std::string,std::string>> supplements;
    for(std::size_t i=1;i<sources.size();++i)supplements.emplace_back(sources[i].category,sources[i].text);
    return Metadata::Catalog::Parse(sources.front().text,supplements);
}
bool Same(const std::vector<Metadata::Service::CoverRequest>& a,const std::vector<Metadata::Service::CoverRequest>& b) {
    if(a.size()!=b.size())return false;
    for(std::size_t i=0;i<a.size();++i)if(a[i].key!=b[i].key||a[i].title!=b[i].title||a[i].localPath!=b[i].localPath)return false;
    return true;
}
}

Metadata::ImageInfo Metadata::ValidatePng(const std::vector<unsigned char>& bytes) {
    const unsigned char signature[]={137,80,78,71,13,10,26,10};
    if(bytes.size()<45||bytes.size()>8*1024*1024||!std::equal(signature,signature+8,bytes.begin()))
        throw std::runtime_error("Artwork is not a bounded PNG image");
    std::size_t at=8;bool header=false,data=false,ended=false;ImageInfo info;
    while(at<bytes.size()) {
        if(bytes.size()-at<12)throw std::runtime_error("Truncated PNG artwork chunk");
        const auto count=Read32(bytes,at);if(count>bytes.size()-at-12)throw std::runtime_error("Truncated PNG artwork data");
        const std::string type(bytes.begin()+at+4,bytes.begin()+at+8);
        if(Crc(bytes.data()+at+4,std::size_t(count)+4)!=Read32(bytes,at+8+count))throw std::runtime_error("Artwork PNG checksum failed");
        if(!header) {
            if(type!="IHDR"||count!=13)throw std::runtime_error("Artwork PNG has no valid header");
            info.width=Read32(bytes,at+8);info.height=Read32(bytes,at+12);
            if(!info.width||!info.height||info.width>2048||info.height>2048||std::uint64_t(info.width)*info.height>4*1024*1024)
                throw std::runtime_error("Artwork dimensions exceed the allowed limit");
            const auto depth=bytes[at+16],color=bytes[at+17];
            const bool depthValid=(color==0&&(depth==1||depth==2||depth==4||depth==8||depth==16))||
                (color==2&&(depth==8||depth==16))||(color==3&&(depth==1||depth==2||depth==4||depth==8))||
                ((color==4||color==6)&&(depth==8||depth==16));
            if(!depthValid||bytes[at+18]||bytes[at+19]||bytes[at+20]>1)throw std::runtime_error("Unsupported PNG artwork encoding");
            header=true;
        } else if(type=="IHDR")throw std::runtime_error("Duplicate artwork PNG header");
        if(type=="IDAT")data=true;
        if(type=="IEND") {
            if(count||!data||at+12!=bytes.size())throw std::runtime_error("Invalid artwork PNG ending");
            ended=true;break;
        }
        // Unknown critical chunks cannot safely be interpreted by the decoder.
        if(type.size()!=4||(!std::islower(static_cast<unsigned char>(type[0]))&&type!="IHDR"&&type!="PLTE"&&type!="IDAT"&&type!="IEND"))
            throw std::runtime_error("Unsupported critical artwork PNG chunk");
        at+=std::size_t(count)+12;
    }
    if(!ended)throw std::runtime_error("Artwork PNG is incomplete");
    return info;
}

struct Metadata::Service::Impl {
    std::string directory,artDirectory;
    Fetch fetch;
    mutable std::mutex mutex;
    std::condition_variable wake;
    Options options;
    std::shared_ptr<const Catalog> catalog=std::make_shared<Catalog>();
    std::vector<CoverRequest> viewport;
    std::deque<Event> events;
    std::size_t eventBytes=0;
    std::atomic<bool> stop{false};
    std::atomic<std::uint64_t> generation{0},coverGeneration{0};
    bool refresh=false,coverPending=false,clear=false,resetRetry=false;
    std::thread worker;
    std::vector<Source> sources;
    std::uint64_t timestamp=0;
    std::map<std::string,std::uint64_t> retryAfter;

    Impl(std::string path,Options settings,Fetch transport):directory(std::move(path)),
        artDirectory(Storage::Join(directory,"artwork")),fetch(std::move(transport)),options(settings) {
        if(directory.empty()||directory.find('\0')!=std::string::npos||!fetch)throw std::invalid_argument("Invalid metadata service configuration");
        worker=std::thread([this]{Work();});
    }
    ~Impl(){stop=true;++generation;wake.notify_all();if(worker.joinable())worker.join();}
    void Emit(Event event,const CancelCheck& cancel={}) {
        std::unique_lock<std::mutex> lock(mutex);
        wake.wait(lock,[&]{return stop.load()||(cancel&&cancel())||(events.size()<64&&eventBytes+event.bytes.size()<=32*1024*1024);});
        if(stop||(cancel&&cancel()))return;
        eventBytes+=event.bytes.size();events.push_back(std::move(event));
    }
    void PurgeCovers() {
        for(auto at=events.begin();at!=events.end();) {
            if(at->type==Event::Type::CoverReady||(at->type==Event::Type::Error&&at->key!="catalog"&&at->key!="cache")) {
                eventBytes-=at->bytes.size();at=events.erase(at);
            } else ++at;
        }
    }
    void Error(const std::string& key,const std::string& message){Emit({Event::Type::Error,key,{},message,{}});}
    CancelCheck Cancel(std::uint64_t version) {
        return [this,version]{return stop.load()||generation.load()!=version;};
    }
    void Publish(Catalog value) {
        auto snapshot=std::make_shared<const Catalog>(std::move(value));
        {std::lock_guard<std::mutex> lock(mutex);catalog=std::move(snapshot);}
        Emit({Event::Type::CatalogReady,"catalog",{},"Catalog ready",{}});
    }
    void LoadCache() {
        const auto path=Storage::Join(directory,"catalog.cache");
        if(!Exists(path))return;
        try {auto loaded=Unpack(Storage::ReadBytes(path,32*1024*1024),timestamp);auto value=BuildCatalog(loaded);
            sources=std::move(loaded);Publish(std::move(value));}
        catch(const std::exception& error){sources.clear();timestamp=0;Error("catalog",std::string("Cached metadata could not be read: ")+error.what());}
    }
    void Refresh(std::uint64_t version) {
        const auto cancel=Cancel(version);std::vector<Source> next;
        for(unsigned i=0;i<6;++i) {
            if(cancel())return;
            Source source;source.category=Categories[i];
            if(sources.size()==6)source=sources[i];
            try {
                auto response=fetch({CatalogUrl(Categories[i]),source.etag,16*1024*1024,20},cancel);
                if(cancel())return;
                if(response.bytes.size()>16*1024*1024)throw std::runtime_error("Metadata catalog response exceeds the allowed size");
                if(response.etag.size()>1024||response.etag.find_first_of("\r\n\0",0,3)!=std::string::npos)
                    throw std::runtime_error("Invalid catalog response validator");
                if(response.status==200) {source.text=Text(response.bytes);source.etag=response.etag;}
                else if(response.status!=304||source.text.empty())throw std::runtime_error("Catalog download failed (HTTP "+std::to_string(response.status)+")");
            }catch(const std::exception& error) {
                if(cancel())return;
                if(i==0)throw;
                Error("catalog",std::string(Categories[i])+" metadata unavailable: "+error.what());
            }
            next.push_back(std::move(source));
        }
        auto parsed=BuildCatalog(next);if(cancel())return;
        const auto now=Now();const auto bytes=Pack(next,now);
        Storage::AtomicWrite(Storage::Join(directory,"catalog.cache"),bytes);
        if(cancel())return;
        sources=std::move(next);timestamp=now;Publish(std::move(parsed));
    }
    void Evict(std::size_t limit) {
        struct Cached {std::string path;std::uint64_t size,access;};
        std::vector<Cached> files;std::uint64_t total=0;
        for(const auto& file:Storage::List(artDirectory)) {
            const auto name=Storage::Filename(file.path);
            if(file.directory||file.link||name.size()!=20||name.substr(16)!=".png")continue;
            std::uint64_t access=0;
            try {const auto metadata=Text(Storage::ReadBytes(file.path+".url",32768));access=std::stoull(metadata.substr(0,metadata.find('\n')));}catch(const std::exception&){}
            files.push_back({file.path,file.size,access});total+=file.size;
        }
        std::sort(files.begin(),files.end(),[](const Cached& a,const Cached& b){return a.access<b.access||(a.access==b.access&&a.path<b.path);});
        for(const auto& file:files) {
            if(total<=limit)break;
            if(Storage::RemoveFile(file.path)){Storage::RemoveFile(file.path+".url");total-=file.size;}
        }
    }
    void Cover(const CoverRequest& request,const Options& settings,std::uint64_t version,std::uint64_t coverVersion) {
        const CancelCheck cancel=[this,version,coverVersion]{return stop.load()||generation.load()!=version||coverGeneration.load()!=coverVersion;};if(cancel())return;
        if(!request.localPath.empty()) {
            const auto bytes=Storage::ReadBytes(request.localPath,8*1024*1024);
            ValidatePng(bytes);if(!cancel())Emit({Event::Type::CoverReady,request.key,request.localPath,{},bytes},cancel);return;
        }
        if(request.title.empty())return;
        const auto url=CoverUrl(request.title);const auto path=Storage::Join(artDirectory,CacheKey(url)+".png");
        Bytes cached;
        if(Exists(path)&&Exists(path+".url")) {
            try {
                const auto metadata=Text(Storage::ReadBytes(path+".url",32768));const auto end=metadata.find('\n');
                if(end!=std::string::npos&&metadata.substr(end+1)==url) {
                    cached=Storage::ReadBytes(path,8*1024*1024);ValidatePng(cached);
                }
            }catch(const std::exception&) {cached.clear();Storage::RemoveFile(path);Storage::RemoveFile(path+".url");}
        }
        if(!cached.empty()) {
            try {Storage::AtomicWrite(path+".url",Data(std::to_string(Now())+"\n"+url));}
            catch(const std::exception& error){Error("cache",error.what());}
            if(!cancel())Emit({Event::Type::CoverReady,request.key,path,{},std::move(cached)},cancel);
            return;
        }
        if(!settings.enabled||retryAfter[url]>Now())return;
        try {
            auto response=fetch({url,{},8*1024*1024,20},cancel);if(cancel())return;
            if(response.status!=200) {
                retryAfter[url]=Now()+(response.status==404?24*60*60:5*60);
                if(response.status!=404)Error(request.key,"Cover download failed (HTTP "+std::to_string(response.status)+")");
                return;
            }
            ValidatePng(response.bytes);if(cancel())return;
            std::string saved;
            if(response.bytes.size()<=settings.cacheLimitBytes) {
                try {
                    Storage::AtomicWrite(path,response.bytes);
                    Storage::AtomicWrite(path+".url",Data(std::to_string(Now())+"\n"+url));saved=path;
                    Evict(settings.cacheLimitBytes);
                }catch(const std::exception& error){saved.clear();Error("cache",error.what());}
            }
            if(!cancel())Emit({Event::Type::CoverReady,request.key,saved,{},std::move(response.bytes)},cancel);
        }catch(const std::exception&) {if(!cancel())retryAfter[url]=Now()+5*60;throw;}
    }
    void Clear() {
        Storage::RemoveFile(Storage::Join(directory,"catalog.cache"));
        for(const auto& file:Storage::List(artDirectory)) {
            const auto name=Storage::Filename(file.path);
            if(!file.directory&&!file.link&&((name.size()==20&&name.substr(16)==".png")||(name.size()==24&&name.substr(16)==".png.url")))
                Storage::RemoveFile(file.path);
        }
        sources.clear();timestamp=0;retryAfter.clear();
    }
    void Work() {
        try {Storage::EnsureDirectory(directory);Storage::EnsureDirectory(artDirectory);LoadCache();}
        catch(const std::exception& error){Error("catalog",error.what());}
        {std::lock_guard<std::mutex> lock(mutex);refresh=options.enabled&&(timestamp==0||Now()-std::min(Now(),timestamp)>=options.refreshSeconds);}
        while(!stop) {
            Options settings;std::vector<CoverRequest> covers;bool doRefresh=false,doClear=false,doResetRetry=false;std::uint64_t version=0,coverVersion=0;
            {
                std::unique_lock<std::mutex> lock(mutex);
                wake.wait(lock,[&]{return stop.load()||refresh||coverPending||clear;});if(stop)break;
                settings=options;doRefresh=refresh&&settings.enabled;doClear=clear;doResetRetry=resetRetry;covers=viewport;
                refresh=false;coverPending=false;clear=false;resetRetry=false;version=generation.load();coverVersion=coverGeneration.load();
            }
            try {if(doResetRetry)retryAfter.clear();if(doClear)Clear();Evict(settings.cacheLimitBytes);if(doRefresh)Refresh(version);}
            catch(const std::exception& error){if(!Cancel(version)())Error("catalog",error.what());}
            for(const auto& request:covers) {
                if(Cancel(version)())break;
                try {Cover(request,settings,version,coverVersion);}
                catch(const std::exception& error){if(!Cancel(version)()&&coverGeneration.load()==coverVersion)Error(request.key,error.what());}
            }
        }
    }
};

Metadata::Service::Service(const std::string& directory,const Options& options,Fetch fetch):impl_(new Impl(directory,options,std::move(fetch))){}
Metadata::Service::~Service()=default;
void Metadata::Service::Configure(const Options& options) {
    std::lock_guard<std::mutex> lock(impl_->mutex);
    if(options.enabled==impl_->options.enabled&&options.cacheLimitBytes==impl_->options.cacheLimitBytes&&options.refreshSeconds==impl_->options.refreshSeconds)return;
    const bool enabledNow=options.enabled&&!impl_->options.enabled;
    impl_->options=options;++impl_->generation;impl_->PurgeCovers();impl_->coverPending=true;
    if(enabledNow)impl_->refresh=true;
    impl_->wake.notify_all();
}
void Metadata::Service::RefreshCatalog() {
    std::lock_guard<std::mutex> lock(impl_->mutex);++impl_->generation;impl_->PurgeCovers();impl_->refresh=true;impl_->coverPending=true;impl_->resetRetry=true;impl_->wake.notify_all();
}
std::shared_ptr<const Metadata::Catalog> Metadata::Service::Snapshot()const {
    std::lock_guard<std::mutex> lock(impl_->mutex);return impl_->catalog;
}
void Metadata::Service::RequestCovers(const std::vector<CoverRequest>& requests) {
    if(requests.size()>128)throw std::invalid_argument("Too many visible artwork requests");
    std::set<std::string> keys;
    for(const auto& request:requests)if(request.key.empty()||!keys.insert(request.key).second)
        throw std::invalid_argument("Artwork request keys must be nonempty and unique");
    std::lock_guard<std::mutex> lock(impl_->mutex);if(Same(impl_->viewport,requests))return;
    impl_->viewport=requests;++impl_->coverGeneration;impl_->PurgeCovers();impl_->coverPending=true;impl_->wake.notify_all();
}
std::vector<Metadata::Event> Metadata::Service::PollEvents() {
    std::vector<Event> result;
    {std::lock_guard<std::mutex> lock(impl_->mutex);result.assign(std::make_move_iterator(impl_->events.begin()),std::make_move_iterator(impl_->events.end()));impl_->events.clear();impl_->eventBytes=0;}
    impl_->wake.notify_all();return result;
}
void Metadata::Service::ClearCache() {
    std::lock_guard<std::mutex> lock(impl_->mutex);++impl_->generation;impl_->PurgeCovers();impl_->clear=true;impl_->coverPending=true;impl_->refresh=false;impl_->wake.notify_all();
}
