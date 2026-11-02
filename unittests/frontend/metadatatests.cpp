#include "../../metadata/catalog.hpp"
#include "../../metadata/http.hpp"
#include "../../metadata/service.hpp"
#include "../../platform/storage.hpp"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <functional>
#include <iostream>
#include <stdexcept>
#include <thread>

namespace {
using Bytes=std::vector<unsigned char>;
void Need(bool okay,const char* reason){if(!okay)throw std::runtime_error(reason);}
void Throws(const std::function<void()>& action,const std::string& part) {
    try {action();}catch(const std::exception& error) {
        Need(std::string(error.what()).find(part)!=std::string::npos,"Exception lost its metadata context");return;
    }
    throw std::runtime_error("Expected metadata operation to fail");
}
Bytes Data(const std::string& text){return {text.begin(),text.end()};}
Bytes Png() {
    const std::string hex="89504e470d0a1a0a0000000d49484452000000010000000108060000001f15c4890000000d49444154789c6360f8cff01f00040101ff71eb47e50000000049454e44ae426082";
    Bytes bytes;
    for(std::size_t i=0;i<hex.size();i+=2)bytes.push_back(static_cast<unsigned char>(std::stoul(hex.substr(i,2),nullptr,16)));
    return bytes;
}
std::string Base() {
    return u8"clrmamepro ( name \"test catalog\" )\n"
        "game ( name \"Green & Blue (USA)\" region \"USA\" rom ( name \"game.bin\" size 512 crc 12345678 ) )\n"
        "game ( name \"Green & Blue (Japan)\" region \"Japan\" rom ( size 512 crc 12345678 ) )\n"
        "game ( name \"日本語 (World)\" rom ( size 1024 crc 87654321 ) )\n";
}
std::string Supplement(const std::string& category) {
    const auto value=category=="developer"?"Studio":category=="publisher"?"Publisher":
        category=="genre"?"Platform":category=="releaseyear"?"1994":"2";
    return "game ( comment \"Green & Blue\" "+category+" \""+value+"\" rom ( crc 12345678 ) )";
}
template<class Predicate> void Wait(Predicate predicate,const char* reason) {
    const auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(5);
    while(std::chrono::steady_clock::now()<deadline) {
        if(predicate())return;
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    throw std::runtime_error(reason);
}
Metadata::Event Await(Metadata::Service& service,Metadata::Event::Type type,const std::string& key) {
    Metadata::Event selected;bool found=false;
    Wait([&]{for(auto& event:service.PollEvents())if(event.type==type&&event.key==key){selected=std::move(event);found=true;}return found;},"Metadata worker did not deliver expected event");
    return selected;
}
void CatalogTests() {
    std::vector<std::pair<std::string,std::string>> extra;
    for(const auto* category:{"developer","publisher","genre","releaseyear","maxusers"})extra.emplace_back(category,Supplement(category));
    const auto catalog=Metadata::Catalog::Parse(Base(),extra);
    Need(catalog.Games().size()==3,"Catalog dropped a variant or checksum collision");
    const auto matches=catalog.Match(0x12345678,512);
    Need(matches.size()==2,"Ambiguous checksum match was silently collapsed");
    Need(catalog.Match(0x12345678,1024).empty(),"Catalog matched a wrong-size ROM");
    Need(matches.front().developer=="Studio"&&matches.front().publisher=="Publisher"&&matches.front().genre=="Platform"&&matches.front().year==1994&&matches.front().players==2,
         "Supplement catalog fields were not merged by checksum");
    Need(catalog.Search("gReEn").size()==2&&catalog.Search(u8"日本語").size()==1,"Metadata search changed case/Unicode matching");
    Need(catalog.Search("",1).size()==1&&catalog.Search("",0).empty(),"Metadata search limit ignored");
    Need(catalog.Find(matches.front().id)&&!catalog.Find("missing"),"Catalog identity lookup failed");
    const auto punctuation=Metadata::Catalog::Parse("game ( name \"(\" rom ( size 512 crc 00000000 ) )");
    Need(punctuation.Games().front().title=="(","Quoted punctuation was parsed as structure");
    Throws([]{Metadata::Catalog::Parse("game ( name \"bad\"");},"line");
    Throws([]{Metadata::Catalog::Parse("game ( name \"bad\" rom ( size 9999999999999999999999 crc 12345678 ) )");},"no checksum");
    Throws([]{Metadata::Catalog::Parse(std::string(32*1024*1024+1,'x'));},"oversized");
    Throws([]{Metadata::Catalog::Parse("game ( name \"unterminated");},"unterminated");
    Need(Metadata::CoverUrl("Green & Blue (USA)").find("Green%20_%20Blue%20%28USA%29.png")!=std::string::npos,"Artwork filename normalization/URL encoding failed");
    Need(Metadata::CoverUrl(u8"日本語").find("%E6%97%A5")!=std::string::npos,"Artwork URL lost Unicode");
    Need(Metadata::AllowedUrl(Metadata::CoverUrl("safe"))&&Metadata::AllowedUrl(Metadata::CatalogUrl("no-intro")),"Provider URL was rejected");
    for(const auto* url:{"http://raw.githubusercontent.com/x", "https://evil.example/file.png", "https://raw.githubusercontent.com/libretro-thumbnails/Sega_-_Mega_Drive_-_Genesis/master/Named_Boxarts/../file.png"})
        Need(!Metadata::AllowedUrl(url),"Unapproved artwork endpoint accepted");
    Throws([]{Metadata::Download({"https://evil.example/game.png",{},100,1});},"approved");
    Throws([]{Metadata::Download({Metadata::CoverUrl("safe"),"bad\r\nheader",100,1});},"validator");
    const auto image=Metadata::ValidatePng(Png());Need(image.width==1&&image.height==1,"PNG dimensions decoded incorrectly");
    auto bad=Png();bad[54]^=1;Throws([&]{Metadata::ValidatePng(bad);},"checksum");
    bad=Png();bad.pop_back();Throws([&]{Metadata::ValidatePng(bad);},"Truncated");
    bad=Png();bad.insert(bad.end(),1,0);Throws([&]{Metadata::ValidatePng(bad);},"ending");
    Throws([]{Metadata::ValidatePng(Bytes(8*1024*1024+1));},"bounded");
}
struct Transport {
    std::atomic<unsigned> calls{0},coverCalls{0};
    std::atomic<bool> invalidBase{false},blockCover{false},entered{false},cancelled{false},badImage{false},failNetwork{false};
    std::atomic<bool> blockBase{false},enteredBase{false},cancelledBase{false},missingSupplement{false};
    Metadata::HttpResponse Fetch(const Metadata::HttpRequest& request,const Metadata::CancelCheck& cancel) {
        ++calls;
        if(failNetwork)throw std::runtime_error("Fixture certificate/connection failure");
        for(const auto* category:{"no-intro","developer","publisher","genre","releaseyear","maxusers"})
            if(request.url==Metadata::CatalogUrl(category)) {
                if(std::string(category)=="no-intro") {
                    enteredBase=true;
                    while(blockBase&&!cancel())std::this_thread::sleep_for(std::chrono::milliseconds(5));
                    if(cancel()){cancelledBase=true;throw std::runtime_error("Catalog fixture cancelled");}
                }
                if(missingSupplement&&std::string(category)=="genre")return {404,{},{},{}};
                return {200,"\"fixture-v1\"","text/plain",Data(std::string(category)=="no-intro"?(invalidBase?"broken":Base()):Supplement(category))};
            }
        ++coverCalls;
        if(blockCover) {
            entered=true;
            while(!cancel())std::this_thread::sleep_for(std::chrono::milliseconds(5));
            cancelled=true;throw std::runtime_error("Fixture fetch cancelled");
        }
        if(request.url==Metadata::CoverUrl("missing"))return {404,{},{},{}};
        if(badImage)return {200,{},"image/png",Data("not a png")};
        return {200,"\"image\"","image/png",Png()};
    }
};
std::size_t PngCount(const std::string& path) {
    std::size_t count=0;
    for(const auto& file:Storage::List(path))if(!file.directory&&Storage::Filename(file.path).size()==20)++count;
    return count;
}
void ServiceTests(const std::string& root) {
    const auto cache=Storage::Join(root,u8"cache-é-日本語");Transport transport;
    const Metadata::Fetch fetch=[&](const Metadata::HttpRequest& r,const Metadata::CancelCheck& c){return transport.Fetch(r,c);};
    Metadata::Options online;online.cacheLimitBytes=128;
    const auto overridePath=Storage::Join(root,u8"override-é-日本語.png");Storage::AtomicWrite(overridePath,Png());
    {
        Metadata::Service service(cache,online,fetch);
        Await(service,Metadata::Event::Type::CatalogReady,"catalog");
        Need(service.Snapshot()->Games().size()==3&&transport.calls==6,"Online catalog initialization was incomplete");
        service.RequestCovers({{"one","Green & Blue (USA)",{}}});
        const auto cover=Await(service,Metadata::Event::Type::CoverReady,"one");
        Need(cover.bytes==Png()&&!cover.path.empty(),"Valid cover was not cached/delivered");
        const auto calls=transport.calls.load();service.RequestCovers({{"one","Green & Blue (USA)",{}}});
        std::this_thread::sleep_for(std::chrono::milliseconds(30));Need(transport.calls==calls,"Unchanged viewport repeated network I/O");
        service.RequestCovers({{"override","Green & Blue (USA)",overridePath}});
        const auto local=Await(service,Metadata::Event::Type::CoverReady,"override");
        Need(local.path==overridePath&&local.bytes==Png()&&transport.calls==calls,"Local override did not take precedence");
        const auto before=Storage::ReadBytes(Storage::Join(cache,"catalog.cache"),32*1024*1024);
        transport.invalidBase=true;service.RefreshCatalog();Await(service,Metadata::Event::Type::Error,"catalog");
        Need(service.Snapshot()->Games().size()==3&&Storage::ReadBytes(Storage::Join(cache,"catalog.cache"),32*1024*1024)==before,
             "Failed refresh replaced a valid catalog snapshot or cache");transport.invalidBase=false;
        transport.failNetwork=true;service.RefreshCatalog();Await(service,Metadata::Event::Type::Error,"catalog");
        Need(service.Snapshot()->Games().size()==3,"Network failure discarded the cached catalog");transport.failNetwork=false;
        Metadata::Options offline=online;offline.enabled=false;service.Configure(offline);
        service.RequestCovers({{"offline","Green & Blue (USA)",{}}});
        const auto cached=Await(service,Metadata::Event::Type::CoverReady,"offline");Need(cached.bytes==Png(),"Offline cover cache failed");
    }
    const auto offlineCalls=transport.calls.load();
    {
        Metadata::Options offline=online;offline.enabled=false;Metadata::Service service(cache,offline,fetch);
        Await(service,Metadata::Event::Type::CatalogReady,"catalog");
        service.RequestCovers({{"restart","Green & Blue (USA)",{}}});Await(service,Metadata::Event::Type::CoverReady,"restart");
        Need(transport.calls==offlineCalls,"Offline startup contacted the network");
        service.ClearCache();
        Wait([&]{try{Storage::Stat(Storage::Join(cache,"catalog.cache"));return false;}catch(const std::exception&){return true;}},"Metadata cache did not clear");
        Need(Storage::ReadBytes(overridePath,1024)==Png(),"Clearing cache deleted a local artwork override");
    }
    {
        Metadata::Options options=online;options.cacheLimitBytes=Png().size();
        Metadata::Service service(Storage::Join(root,"bounded"),options,fetch);Await(service,Metadata::Event::Type::CatalogReady,"catalog");
        service.RequestCovers({{"a","first",{}},{"b","second",{}}});
        bool first=false,second=false;
        Wait([&]{for(const auto& event:service.PollEvents())if(event.type==Metadata::Event::Type::CoverReady){first|=event.key=="a";second|=event.key=="b";}return first&&second;},"Cover worker omitted a visible request");
        Need(PngCount(Storage::Join(Storage::Join(root,"bounded"),"artwork"))==1,"Artwork cache did not evict to its byte budget");
        service.RequestCovers({{"missing","missing",{}}});Wait([&]{return transport.coverCalls>=4;},"Missing cover was not attempted");
        const auto count=transport.coverCalls.load();
        service.RequestCovers({});service.RequestCovers({{"missing","missing",{}}});
        std::this_thread::sleep_for(std::chrono::milliseconds(30));Need(transport.coverCalls==count,"Missing cover was retried without backoff");
        transport.badImage=true;service.RequestCovers({{"bad","bad-image",{}}});Await(service,Metadata::Event::Type::Error,"bad");transport.badImage=false;
        transport.blockCover=true;service.RequestCovers({{"blocked","blocked",{}}});Wait([&]{return transport.entered.load();},"Blocked fixture did not start");
        service.RequestCovers({});Wait([&]{return transport.cancelled.load();},"Viewport change did not cancel obsolete cover I/O");
        transport.blockCover=false;
    }
    {
        Transport independent;independent.blockBase=true;independent.missingSupplement=true;
        Metadata::Service service(Storage::Join(root,"independent-cancel"),online,
            [&](const Metadata::HttpRequest& request,const Metadata::CancelCheck& cancel){return independent.Fetch(request,cancel);});
        Wait([&]{return independent.enteredBase.load();},"Catalog refresh did not start");
        service.RequestCovers({{"first","first",{}}});service.RequestCovers({{"second","second",{}}});
        independent.blockBase=false;
        Await(service,Metadata::Event::Type::CatalogReady,"catalog");
        Need(!independent.cancelledBase&&service.Snapshot()->Games().size()==3,
             "Viewport changes cancelled catalog initialization or missing supplements hid all games");
        Wait([&]{return independent.coverCalls>0;},"Latest viewport was not processed after catalog refresh");
    }
}
}

int main(int argc,char** argv) {
    try {
        if(argc==2&&std::string(argv[1])=="--live") {
            const auto response=Metadata::Download({Metadata::CoverUrl("Sonic The Hedgehog 3 (USA)"),{},8*1024*1024,20});
            Need(response.status==200,"Live metadata provider did not return artwork");
            const auto image=Metadata::ValidatePng(response.bytes);
            std::cout<<"Live HTTPS artwork transport passed: "<<response.bytes.size()<<" bytes, "<<image.width<<"x"<<image.height<<"\n";return 0;
        }
        Need(argc==2,"Expected metadata fixture directory");
        const auto root=Storage::Join(argv[1],"run-"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
        Storage::EnsureDirectory(root);CatalogTests();ServiceTests(root);
        std::cout<<"Metadata DAT matching, Unicode URLs, bounded PNGs, async cancellation, cache and offline checks passed\n";return 0;
    }catch(const std::exception& error){std::cerr<<"Metadata test: "<<error.what()<<'\n';return 1;}
}
