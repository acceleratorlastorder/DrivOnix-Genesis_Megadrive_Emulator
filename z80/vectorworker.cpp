#include "core.h"
#include "../thirdparty/json/json.h"

#include <array>
#include <cstdlib>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <memory>
#include <set>
#include <stdexcept>
#include <string>
#include <vector>

namespace
{
json_value_s* Field(json_value_s* value, const char* key)
{
    auto* object = json_value_as_object(value);
    if (!object) throw std::runtime_error("Expected JSON object");
    for (auto* entry = object->start; entry; entry = entry->next)
        if (std::strcmp(entry->name->string,key) == 0) return entry->value;
    throw std::runtime_error(std::string("Missing JSON field: ")+key);
}
unsigned Number(json_value_s* value, unsigned maximum = 0xffffffffu)
{
    auto* number = json_value_as_number(value);
    if (!number || number->number_size == 0) throw std::runtime_error("Expected integer");
    std::uint64_t result = 0;
    for (std::size_t i=0;i<number->number_size;++i)
    {
        const char c=number->number[i];
        if (c<'0'||c>'9') throw std::runtime_error("Expected nonnegative integer");
        result=result*10+unsigned(c-'0');
        if (result>maximum) throw std::runtime_error("Integer is outside its field range");
    }
    return static_cast<unsigned>(result);
}
std::vector<json_value_s*> Array(json_value_s* value)
{
    auto* array=json_value_as_array(value);
    if (!array) throw std::runtime_error("Expected JSON array");
    std::vector<json_value_s*> result;
    for (auto* item=array->start;item;item=item->next) result.push_back(item->value);
    return result;
}
struct Port { unsigned address,value; char kind; };

void Execute(json_value_s* request)
{
    const auto* format=json_value_as_string(Field(request,"format"));
    if (!format||std::strcmp(format->string,"drivonix-z80-v1")!=0) throw std::runtime_error("Unknown request format");
    const unsigned index=Number(Field(request,"index"));
    auto* input=Field(request,"initial");
    md_z80_vector before{};
#define READ8(name) before.name=Number(Field(input,#name),255)
#define READ16(name) before.name=Number(Field(input,#name),65535)
#define READBIT(name) before.name=Number(Field(input,#name),1)
    READ8(a);READ8(f);READ8(b);READ8(c);READ8(d);READ8(e);READ8(h);READ8(l);READ8(i);READ8(r);READ8(q);
    READ16(pc);READ16(sp);READ16(ix);READ16(iy);READ16(af_);READ16(bc_);READ16(de_);READ16(hl_);READ16(wz);
    READBIT(p);READBIT(ei);READBIT(iff1);READBIT(iff2);
    before.im=Number(Field(input,"im"),2);
#undef READ8
#undef READ16
#undef READBIT
    std::array<std::uint8_t,65536> memory{};
    std::set<unsigned> observed;
    for (auto* item:Array(Field(input,"ram")))
    {
        const auto pair=Array(item);
        if(pair.size()!=2) throw std::runtime_error("Invalid RAM pair");
        const unsigned address=Number(pair[0],65535);
        memory[address]=Number(pair[1],255);
        observed.insert(address);
    }
    for (auto* address:Array(Field(request,"observe_memory"))) observed.insert(Number(address,65535));
    std::vector<Port> inputPorts,ports;
    for (auto* item:Array(Field(request,"input_ports")))
    {
        const auto entry=Array(item);
        if(entry.size()!=3) throw std::runtime_error("Invalid input port");
        const auto* kind=json_value_as_string(entry[2]);
        if (!kind||std::strcmp(kind->string,"r")) throw std::runtime_error("Input ports must be reads");
        inputPorts.push_back({Number(entry[0],65535),Number(entry[1],255),'r'});
    }
    std::size_t nextInputPort=0;
    auto bus=[&](std::uint64_t pins)
    {
        const unsigned address=Z80_GET_ADDR(pins);
        if(pins&Z80_MREQ)
        {
            if(pins&Z80_RD) { Z80_SET_DATA(pins,memory[address]); }
            else if(pins&Z80_WR)
            {
                memory[address]=Z80_GET_DATA(pins);
                observed.insert(address);
            }
        }
        else if(pins&Z80_IORQ)
        {
            if(pins&Z80_RD)
            {
                unsigned value=255;
                if(nextInputPort<inputPorts.size()&&inputPorts[nextInputPort].address==address)
                    value=inputPorts[nextInputPort++].value;
                ports.push_back({address,value,'r'});
                Z80_SET_DATA(pins,value);
            }
            else if(pins&Z80_WR) ports.push_back({address,Z80_GET_DATA(pins),'w'});
        }
        return pins;
    };
    md_z80_t cpu{};
    std::uint64_t pins=md_z80_load(&cpu,&before);
    // Prime the overlapped M1/T1 fetch; it is not an executed guest instruction.
    pins=bus(md_z80_tick(&cpu,pins));
    unsigned cycles=0;
    do
    {
        pins=bus(md_z80_tick(&cpu,pins));
        if(++cycles>1024) throw std::runtime_error("Z80 instruction did not reach a boundary");
    } while(!md_z80_opdone(&cpu));
    md_z80_vector after{};
    md_z80_save(&cpu,&after);
    std::cout<<"{\"index\":"<<index<<",\"cycles\":"<<cycles<<",\"final\":{";
#define OUT(name) std::cout<<"\"" #name "\":"<<unsigned(after.name)<<','
    OUT(a);OUT(f);OUT(b);OUT(c);OUT(d);OUT(e);OUT(h);OUT(l);OUT(i);OUT(r);OUT(q);
    OUT(pc);OUT(sp);OUT(ix);OUT(iy);OUT(af_);OUT(bc_);OUT(de_);OUT(hl_);OUT(wz);
    OUT(p);OUT(ei);OUT(iff1);OUT(iff2);OUT(im);
#undef OUT
    std::cout<<"\"ram\":[";
    bool separator=false;
    for(unsigned address:observed)
    {
        if(separator)std::cout<<',';
        separator=true;
        std::cout<<'['<<address<<','<<unsigned(memory[address])<<']';
    }
    std::cout<<"]},\"ports\":[";
    separator=false;
    for(const auto& port:ports)
    {
        if(separator)std::cout<<',';
        separator=true;
        std::cout<<'['<<port.address<<','<<port.value<<",\""<<port.kind<<"\"]";
    }
    std::cout<<"]}\n";
}
}
int main()
{
    try
    {
        std::ios::sync_with_stdio(false);
        std::string line;
        while(std::getline(std::cin,line))
        {
            if(line.size()>1048576)throw std::runtime_error("Request exceeds one MiB");
            std::unique_ptr<json_value_s,decltype(&std::free)> request(json_parse(line.data(),line.size()),std::free);
            if(!request)throw std::runtime_error("Invalid JSON");
            Execute(request.get());
            std::cout.flush();
        }
        return 0;
    }
    catch(const std::exception& error)
    {
        std::cerr<<error.what()<<'\n';
        return 2;
    }
}
