#include "catalog.hpp"

#include <algorithm>
#include <cctype>
#include <iomanip>
#include <limits>
#include <map>
#include <sstream>
#include <stdexcept>

namespace {
std::string Lower(std::string value) {
    for(auto& character:value) if(character>='A'&&character<='Z')character=char(character-'A'+'a');
    return value;
}
struct Token { std::string value; bool end=false; bool punctuation=false; };
class Lexer {
public:
    explicit Lexer(const std::string& text):text_(text) {
        if(text.size()>32*1024*1024||text.find('\0')!=std::string::npos)
            throw std::runtime_error("Invalid or oversized metadata catalog");
        if(text_.compare(0,3,"\xef\xbb\xbf")==0)at_=3;
    }
    Token Next() {
        if(++tokens_>1500000)Fail("too many tokens");
        while(at_<text_.size()) {
            const auto c=text_[at_];
            if(c=='#') {while(at_<text_.size()&&text_[at_]!='\n')++at_;continue;}
            if(std::isspace(static_cast<unsigned char>(c))) {if(c=='\n')++line_;++at_;continue;}
            break;
        }
        if(at_==text_.size())return {{},true};
        const auto c=text_[at_++];
        if(c=='('||c==')')return {std::string(1,c),false,true};
        std::string result;
        if(c=='"') {
            while(at_<text_.size()) {
                char value=text_[at_++];
                if(value=='"')return {result,false};
                if(value=='\\'&&at_<text_.size()) {
                    const auto escaped=text_[at_++];
                    if(escaped!='\\'&&escaped!='"')result.push_back('\\');
                    value=escaped;
                }
                if(static_cast<unsigned char>(value)<0x20)Fail("control character in quoted field");
                result.push_back(value);
                if(result.size()>16384)Fail("field is too long");
            }
            Fail("unterminated quoted field");
        }
        result.push_back(c);
        while(at_<text_.size()) {
            const auto value=text_[at_];
            if(value=='('||value==')'||std::isspace(static_cast<unsigned char>(value)))break;
            result.push_back(value);++at_;
            if(result.size()>16384)Fail("field is too long");
        }
        return {result,false};
    }
    [[noreturn]] void Fail(const char* why)const {
        throw std::runtime_error("Metadata DAT line "+std::to_string(line_)+": "+why);
    }
private:
    const std::string& text_;
    std::size_t at_=0,line_=1,tokens_=0;
};
struct Node {
    std::map<std::string,std::string> values;
    std::vector<std::pair<std::string,Node>> children;
};
Node ReadNode(Lexer& lexer,unsigned depth) {
    if(depth>16)lexer.Fail("nesting is too deep");
    Node node;
    for(;;) {
        const auto key=lexer.Next();
        if(key.end)lexer.Fail("missing closing parenthesis");
        if(key.punctuation&&key.value==")")return node;
        if(key.punctuation)lexer.Fail("expected a field name");
        const auto value=lexer.Next();
        if(value.end||(value.punctuation&&value.value==")"))lexer.Fail("expected a field value");
        if(value.punctuation&&value.value=="(")node.children.emplace_back(Lower(key.value),ReadNode(lexer,depth+1));
        else node.values[Lower(key.value)]=value.value;
    }
}
std::vector<Node> Records(const std::string& text) {
    Lexer lexer(text);std::vector<Node> result;
    for(;;) {
        const auto key=lexer.Next();if(key.end)break;
        const auto open=lexer.Next();
        if(open.end||!open.punctuation||open.value!="(")lexer.Fail("expected a record parenthesis");
        auto node=ReadNode(lexer,0);
        if(Lower(key.value)=="game") {
            result.push_back(std::move(node));
            if(result.size()>100000)lexer.Fail("too many games");
        }
    }
    return result;
}
std::string Get(const Node& node,const std::string& key) {
    const auto found=node.values.find(key);return found==node.values.end()?std::string():found->second;
}
bool Number(const std::string& text,std::uint64_t& result,unsigned base=10) {
    if(text.empty())return false;
    result=0;
    for(unsigned char character:text) {
        const unsigned digit=character>='0'&&character<='9'?unsigned(character-'0'):
            character>='a'&&character<='f'?unsigned(character-'a'+10):
            character>='A'&&character<='F'?unsigned(character-'A'+10):base;
        if(digit>=base||result>(std::numeric_limits<std::uint64_t>::max()-digit)/base)return false;
        result=result*base+digit;
    }
    return true;
}
bool Checksum(const Node& rom,std::uint32_t& crc) {
    const auto value=Get(rom,"crc");std::uint64_t parsed=0;
    if(value.size()!=8||!Number(value,parsed,16))return false;
    crc=std::uint32_t(parsed);return true;
}
std::string Identity(const Metadata::Game& game) {
    std::ostringstream out;out<<std::hex<<std::setfill('0')<<std::setw(8)<<game.crc32
        <<'-'<<std::dec<<game.size<<'-'<<game.title;return out.str();
}
unsigned SmallNumber(const std::string& value,unsigned maximum) {
    std::uint64_t result=0;return Number(value,result)&&result<=maximum?unsigned(result):0;
}
std::string Encode(const std::string& value) {
    static const char hex[]="0123456789ABCDEF";std::string result;
    for(unsigned char character:value) {
        if((character>='A'&&character<='Z')||(character>='a'&&character<='z')||
           (character>='0'&&character<='9')||character=='-'||character=='_'||character=='.'||character=='~')
            result.push_back(char(character));
        else {result.push_back('%');result.push_back(hex[character>>4]);result.push_back(hex[character&15]);}
    }
    return result;
}
}

Metadata::Catalog Metadata::Catalog::Parse(const std::string& noIntro,
    const std::vector<std::pair<std::string,std::string>>& supplements) {
    Catalog catalog;
    for(const auto& record:Records(noIntro)) {
        const auto title=Get(record,"name");if(title.empty())continue;
        for(const auto& child:record.children) {
            if(child.first!="rom")continue;
            Game game;game.title=title;game.region=Get(record,"region");
            std::uint64_t size=0;
            if(!Number(Get(child.second,"size"),size)||!size||!Checksum(child.second,game.crc32))continue;
            game.hasChecksum=true;game.size=size;game.id=Identity(game);
            catalog.games_.push_back(std::move(game));
        }
    }
    if(catalog.games_.empty())throw std::runtime_error("Metadata catalog contains no checksum-identified games");
    std::multimap<std::uint32_t,std::size_t> checksumIndex;
    for(std::size_t i=0;i<catalog.games_.size();++i)checksumIndex.emplace(catalog.games_[i].crc32,i);
    for(const auto& supplement:supplements) {
        const auto category=Lower(supplement.first);
        if(category!="developer"&&category!="publisher"&&category!="genre"&&category!="releaseyear"&&category!="maxusers")
            throw std::invalid_argument("Unsupported metadata category: "+category);
        for(const auto& record:Records(supplement.second)) {
            const auto value=Get(record,category);if(value.empty())continue;
            for(const auto& child:record.children) {
                std::uint32_t crc=0;if(child.first!="rom"||!Checksum(child.second,crc))continue;
                const auto range=checksumIndex.equal_range(crc);
                for(auto at=range.first;at!=range.second;++at) {
                    auto& game=catalog.games_[at->second];
                    if(category=="developer")game.developer=value;
                    else if(category=="publisher")game.publisher=value;
                    else if(category=="genre")game.genre=value;
                    else if(category=="releaseyear")game.year=SmallNumber(value,9999);
                    else game.players=SmallNumber(value,64);
                }
            }
        }
    }
    std::sort(catalog.games_.begin(),catalog.games_.end(),[](const Game& a,const Game& b){return a.id<b.id;});
    catalog.games_.erase(std::unique(catalog.games_.begin(),catalog.games_.end(),
        [](const Game& a,const Game& b){return a.id==b.id;}),catalog.games_.end());
    return catalog;
}
const std::vector<Metadata::Game>& Metadata::Catalog::Games()const {return games_;}
std::vector<Metadata::Game> Metadata::Catalog::Match(std::uint32_t crc,std::uint64_t size)const {
    std::vector<Game> matches;
    // Identity starts with a fixed-width hexadecimal CRC, so the stored order
    // also provides a checksum index without a second mutable data structure.
    auto at=std::lower_bound(games_.begin(),games_.end(),crc,[](const Game& game,std::uint32_t value){return game.crc32<value;});
    for(;at!=games_.end()&&at->crc32==crc;++at)if(at->hasChecksum&&at->size==size)matches.push_back(*at);
    return matches;
}
std::vector<Metadata::Game> Metadata::Catalog::Search(const std::string& query,std::size_t limit)const {
    std::vector<Game> result;const auto needle=Lower(query);
    for(const auto& game:games_)if(Lower(game.title).find(needle)!=std::string::npos) {
        if(result.size()==limit)break;
        result.push_back(game);
    }
    return result;
}
const Metadata::Game* Metadata::Catalog::Find(const std::string& id)const {
    const auto at=std::lower_bound(games_.begin(),games_.end(),id,[](const Game& game,const std::string& key){return game.id<key;});
    return at!=games_.end()&&at->id==id?&*at:nullptr;
}
std::string Metadata::CoverUrl(const std::string& canonicalTitle) {
    if(canonicalTitle.empty()||canonicalTitle.size()>4096)throw std::invalid_argument("Invalid artwork title");
    std::string filename=canonicalTitle;
    for(auto& character:filename) {
        if(static_cast<unsigned char>(character)<0x20)throw std::invalid_argument("Control character in artwork title");
        if(std::string("&*/:`<>?\\|").find(character)!=std::string::npos)character='_';
    }
    return "https://raw.githubusercontent.com/libretro-thumbnails/Sega_-_Mega_Drive_-_Genesis/master/Named_Boxarts/"+Encode(filename+".png");
}
std::string Metadata::CatalogUrl(const std::string& category) {
    if(category!="no-intro"&&category!="developer"&&category!="publisher"&&category!="genre"&&category!="releaseyear"&&category!="maxusers")
        throw std::invalid_argument("Unsupported metadata category");
    return "https://raw.githubusercontent.com/libretro/libretro-database/master/metadat/"+category+"/Sega%20-%20Mega%20Drive%20-%20Genesis.dat";
}
const char* Metadata::Attribution() {
    return "Game metadata: Libretro Database (CC-BY-SA-4.0), https://github.com/libretro/libretro-database. "
           "Cover artwork: Libretro Thumbnails; original artwork belongs to its respective rights holders. "
           "https://github.com/libretro-thumbnails/Sega_-_Mega_Drive_-_Genesis. "
           "No ROM files are downloaded or uploaded.";
}
