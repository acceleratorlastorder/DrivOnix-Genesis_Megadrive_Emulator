#include "rominfo.hpp"
#include "../platform/archive.hpp"
#include "../platform/storage.hpp"
#include <algorithm>
#include <array>
#include <iomanip>
#include <sstream>
#include <stdexcept>
namespace {
uint32_t Rotate(uint32_t value,unsigned count){return (value>>count)|(value<<(32-count));}
std::string Text(const std::vector<unsigned char>& bytes,std::size_t start,std::size_t length){
    std::string text;for(std::size_t i=start;i<start+length;++i){const auto c=bytes[i];text.push_back(c>=32&&c<127 ? char(c):' ');}
    auto first=text.find_first_not_of(' ');return first==std::string::npos ? "":text.substr(first,text.find_last_not_of(' ')-first+1);
}
unsigned char Region(const std::vector<unsigned char>& bytes){
    std::string text;for(unsigned i=0x1f0;i<0x200;++i){unsigned c=bytes[i];if(c&&(c<32||c>126))return 'U';text.push_back(!c ? ' ':char(c>='a'&&c<='z' ? c-'a'+'A':c));}
    auto first=text.find_first_not_of(' ');if(first==std::string::npos)return 'U';text=text.substr(first,text.find_last_not_of(' ')-first+1);
    unsigned mask=0;
    if(text=="USA")mask=4;else if(text=="JAPAN")mask=1;else if(text=="EUROPE")mask=8;
    else{bool letters=true;for(char c:text){if(c=='J')mask|=1;else if(c=='U')mask|=4;else if(c=='E')mask|=8;else if(c!=' '){letters=false;break;}}
        if(!letters){mask=0;std::size_t at=text.compare(0,2,"0X")==0 ? 2:0;if(at==text.size())return 'U';for(;at<text.size();++at){char c=text[at];unsigned digit=c>='0'&&c<='9'?unsigned(c-'0'):c>='A'&&c<='F'?unsigned(c-'A'+10):16;if(digit>15||mask*16+digit>15)return 'U';mask=mask*16+digit;}}}
    return mask&4 ? 'U':mask&1 ? 'J':mask&8 ? 'E':'U';
}
}
RomInfo::Header RomInfo::ProbeHeader(const std::vector<unsigned char>& bytes){
    if(bytes.size()<512)throw std::runtime_error("ROM does not contain a complete header");
    Header result;result.title=Text(bytes,0x150,48);result.domesticTitle=Text(bytes,0x120,48);result.product=Text(bytes,0x180,14);result.regions=Text(bytes,0x1f0,16);result.region=Region(bytes);result.sega=std::equal(bytes.begin()+0x100,bytes.begin()+0x104,"SEGA");return result;
}
uint32_t RomInfo::Crc32(const std::vector<unsigned char>& bytes){
    static const std::array<uint32_t,256> table=[](){std::array<uint32_t,256> out={};for(unsigned i=0;i<256;++i){uint32_t value=i;for(unsigned bit=0;bit<8;++bit)value=(value>>1)^(value&1 ? 0xedb88320u:0);out[i]=value;}return out;}();
    uint32_t crc=0xffffffffu;for(unsigned char value:bytes)crc=table[(crc^value)&255]^(crc>>8);return crc^0xffffffffu;
}
std::string RomInfo::Sha256(const std::vector<unsigned char>& bytes){
    static const uint32_t constants[]={
        0x428a2f98,0x71374491,0xb5c0fbcf,0xe9b5dba5,0x3956c25b,0x59f111f1,0x923f82a4,0xab1c5ed5,0xd807aa98,0x12835b01,0x243185be,0x550c7dc3,0x72be5d74,0x80deb1fe,0x9bdc06a7,0xc19bf174,
        0xe49b69c1,0xefbe4786,0x0fc19dc6,0x240ca1cc,0x2de92c6f,0x4a7484aa,0x5cb0a9dc,0x76f988da,0x983e5152,0xa831c66d,0xb00327c8,0xbf597fc7,0xc6e00bf3,0xd5a79147,0x06ca6351,0x14292967,
        0x27b70a85,0x2e1b2138,0x4d2c6dfc,0x53380d13,0x650a7354,0x766a0abb,0x81c2c92e,0x92722c85,0xa2bfe8a1,0xa81a664b,0xc24b8b70,0xc76c51a3,0xd192e819,0xd6990624,0xf40e3585,0x106aa070,
        0x19a4c116,0x1e376c08,0x2748774c,0x34b0bcb5,0x391c0cb3,0x4ed8aa4a,0x5b9cca4f,0x682e6ff3,0x748f82ee,0x78a5636f,0x84c87814,0x8cc70208,0x90befffa,0xa4506ceb,0xbef9a3f7,0xc67178f2};
    uint32_t hash[]={0x6a09e667,0xbb67ae85,0x3c6ef372,0xa54ff53a,0x510e527f,0x9b05688c,0x1f83d9ab,0x5be0cd19};
    auto padded=bytes;padded.push_back(0x80);while(padded.size()%64!=56)padded.push_back(0);const uint64_t bits=uint64_t(bytes.size())*8;for(int i=7;i>=0;--i)padded.push_back(unsigned((bits>>(i*8))&255));
    for(std::size_t start=0;start<padded.size();start+=64){uint32_t words[64]={};for(unsigned i=0;i<16;++i)for(unsigned j=0;j<4;++j)words[i]=(words[i]<<8)|padded[start+i*4+j];
        for(unsigned i=16;i<64;++i){auto a=words[i-15],b=words[i-2];words[i]=words[i-16]+(Rotate(a,7)^Rotate(a,18)^(a>>3))+words[i-7]+(Rotate(b,17)^Rotate(b,19)^(b>>10));}
        uint32_t a=hash[0],b=hash[1],c=hash[2],d=hash[3],e=hash[4],f=hash[5],g=hash[6],h=hash[7];
        for(unsigned i=0;i<64;++i){uint32_t one=h+(Rotate(e,6)^Rotate(e,11)^Rotate(e,25))+((e&f)^(~e&g))+constants[i]+words[i];uint32_t two=(Rotate(a,2)^Rotate(a,13)^Rotate(a,22))+((a&b)^(a&c)^(b&c));h=g;g=f;f=e;e=d+one;d=c;c=b;b=a;a=one+two;}
        hash[0]+=a;hash[1]+=b;hash[2]+=c;hash[3]+=d;hash[4]+=e;hash[5]+=f;hash[6]+=g;hash[7]+=h;}
    std::ostringstream out;out<<std::hex<<std::setfill('0');for(auto value:hash)out<<std::setw(8)<<value;return out.str();
}
RomInfo::Prepared RomInfo::Load(const std::string& path,const std::string& member,const Process::CancelToken& cancel){
    Process::CheckCancelled(cancel);Prepared result;std::string selected=member;
    if(Archive::IsArchive(path)){
        if(selected.empty()){const auto entries=Archive::List(path,cancel);if(entries.size()!=1)throw std::runtime_error("Archive contains multiple games; select an exact archive member");selected=entries.front().name;}
        result.bytes=Archive::Read(path,selected,cancel);result.saveIdentity=Archive::SaveIdentity(path,selected);
    }else{if(!selected.empty())throw std::runtime_error("Archive member requires ZIP or 7z");result.saveIdentity=path;selected=path;}
    auto dot=selected.find_last_of('.');std::string extension=dot==std::string::npos ? "":selected.substr(dot);std::transform(extension.begin(),extension.end(),extension.begin(),[](unsigned char c){return char(std::tolower(c));});
    if(extension!=".bin"&&extension!=".md"&&extension!=".gen"&&extension!=".smd"&&extension!=".bios")throw std::runtime_error("Unsupported ROM extension: "+selected);
    if(!Archive::IsArchive(path)){
        try{
            const auto size=Storage::Stat(path).size;const uint64_t limit=0x800000+(extension==".smd"?512:0);
            if(size<512||size>limit)throw std::runtime_error("ROM must contain a complete header and fit in the 8 MiB cartridge image limit");
            result.bytes=Storage::ReadBytes(path,limit,cancel);
        }catch(const Process::Cancelled&){throw;}catch(const std::runtime_error& error){throw std::runtime_error("Cannot open ROM: "+path+" ("+error.what()+")");}
    }
    if(extension==".smd"){
        if(result.bytes.size()<=512||(result.bytes.size()-512)%16384)throw std::runtime_error("SMD ROM requires complete 16 KiB blocks");
        std::vector<unsigned char> decoded(result.bytes.size()-512);
        for(std::size_t block=0;block<decoded.size();block+=16384){Process::CheckCancelled(cancel);for(std::size_t i=0;i<8192;++i){decoded[block+i*2]=result.bytes[512+block+8192+i];decoded[block+i*2+1]=result.bytes[512+block+i];}}
        result.bytes=std::move(decoded);
    }
    if(result.bytes.size()<512||result.bytes.size()>0x800000)throw std::runtime_error("ROM must contain a complete header and fit in the 8 MiB limit");
    result.header=ProbeHeader(result.bytes);result.region=result.header.region;result.title=result.header.title;result.size=result.bytes.size();Process::CheckCancelled(cancel);result.crc32=Crc32(result.bytes);result.sha256=Sha256(result.bytes);Process::CheckCancelled(cancel);return result;
}
