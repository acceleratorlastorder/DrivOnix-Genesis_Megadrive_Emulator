#include "ym7101.hpp"
#include <algorithm>
#include <iterator>

YM7101::YM7101()
{
	
}

YM7101& YM7101::get()
{
	static YM7101 instance;
	return instance;
}

void YM7101::Init()
{
	get().vdpResolution = std::pair<int, int>(256, 224);
	get().is256Screen = true;
	get().vCounter = 0x0;
	get().controlPortPending = false;
	get().controlPortData = 0;
    get().hvFrozen=false;get().hvLatch=0;
	get().lineInt = 0xFF;
	get().requestInt = false;
	get().status = 0x3600;

	std::memset(get().vram.bytes, 0x0, sizeof(get().vram.bytes));
    get().lineCycles=get().scanline=get().stallCycles=0;
    get().horizontalPending=get().verticalPending=get().dmaFillPending=false;
    get().fifoCount=get().dmaType=get().dmaRemaining=0;
    get().totalCycles=get().dmaReady=0;
    get().status=Genesis::IsPAL() ? 0x3601:0x3600;
    CRT::SetResolution(256,224);

	for(int i = 0; i < 24; ++i)
	{
		get().vdpRegister[i] = 0;
	}

	for(int i = 0; i < 0x40; ++i)
	{
		get().cram[i] = 0;
	}

	for(int i = 0; i < 0x40; ++i)
	{
		get().vsram[i] = 0;
	}


	CRT::ResetScreen();
}

bool YM7101::GetIs256Screen()
{
	return get().is256Screen;
}

word YM7101::GetAddressRegister()
{
	word result = get().controlPortData >> 16;
	result &= 0x3FFF;

	word bits = get().controlPortData & 0x3;
	result |= bits << 14;

	return result;
}

void YM7101::IncrementAddress()
{
	word address = get().GetAddressRegister();
	address += get().vdpRegister[0xF];

	if(TestBit(address, 15))
	{
		BitSet(get().controlPortData, 1);
	}
	else
	{
		BitReset(get().controlPortData, 1);
	}

	if(TestBit(address, 14))
	{
		BitSet(get().controlPortData, 0);
	}
	else
	{
		BitReset(get().controlPortData, 0);
	}

	for(int i = 0; i < 14; ++i)
	{
		if(TestBit(address, 13 - i))
		{
			BitSet(get().controlPortData, 29 - i);
		}
		else
		{
			BitReset(get().controlPortData, 29 - i);
		}
	}
}

byte YM7101::GetHCounter()
{
    if(get().hvFrozen) return byte(get().hvLatch);
    const unsigned phase=get().lineCycles;
    // Counter phase is referenced to HINT / V-counter increment, not H=0.
    // H32 has a constant divide-by-20 half-dot clock and skips 94..E8.
    if(!get().Is40Cell())
    {
        unsigned counter=0x85+phase/20;
        if(counter>=0x94) counter+=0x55;
        return byte(counter);
    }
    // H40 normally divides by 16, with its shorter jump and the variable
    // horizontal-sync clock. These boundaries are VDP master-clock facts.
    if(phase<272) return byte(0xa5+phase/16);
    if(phase<280) return 0xb6;
    if(phase<288) return 0xe4;
    if(phase<304) return 0xe5;
    if(phase>=652) return byte(0xf8+(phase-652)/16);
    static const unsigned sync[]={304,321,341,361,381,399,419,439,459,478,497,517,537,557,575,595,615,635};
    return byte(0xe6+(std::upper_bound(std::begin(sync),std::end(sync),phase)-std::begin(sync))-1);
}

byte YM7101::GetVCounter()
{
    if(get().hvFrozen) return byte(get().hvLatch>>8);
	unsigned line=get().scanline;
    const unsigned jump=Genesis::IsPAL() ? (get().vdpResolution.second==240 ? 0x10b:0x103):0xeb;
    if (line>=jump) line-=Genesis::IsPAL()?57:6;
    byte result=byte(line);

    if(get().vdpRegister[12]&2)
    {
        if((get().vdpRegister[12]&6)==6) line<<=1;
        result=byte((line&~1u)|((line>>8)&1));
    }

	return result;
}

word YM7101::GetHVCounter()
{
	return word((word(get().GetVCounter())<<8)|GetHCounter());
}

void YM7101::WriteControlPortBYTE(byte data)
{
	word doubleData = (data << 8) | data;
	get().WriteControlPortWORD(doubleData);
}

void YM7101::WriteControlPortWORD(word data)
{
	if(!get().controlPortPending)
	{
		byte code = (byte)((data >> 14) & 0x3);
		if(code == 2)
		{
			get().UpdateRegister(data);
		}
		else
		{
			get().controlPortData &= 0xFFFF;
			get().controlPortData |= ((dword)data << 16);
			get().controlPortPending = true;
		}
	}
	else
	{
		get().controlPortPending = false;
		get().controlPortData &= 0xFFFF0000;
		get().controlPortData |= data;

		if(TestBit(get().controlPortData, 7) && TestBit(get().vdpRegister[1], 4))
		{
			if (((get().vdpRegister[23]>>6)&3)==2) get().dmaFillPending=true;
            else get().DMA(data);
		}
	}
}

word YM7101::ReadControlPortWORD()
{
	word result = word((get().status & ~0x302) | (get().fifoCount==0 ? 0x200:0) | (get().fifoCount==4 ? 0x100:0) | (get().dmaType ? 2:0));
    // Status reads reset sprite flags and the two-word command latch, not IRQ latches.
    get().status &= ~word(0x60);
	get().controlPortPending = false;
	BitReset(get().status, 1);
	return result;
}

void YM7101::WriteDataPortBYTE(byte data)
{
	word doubleData = (data << 8) | data;
	get().WriteDataPortWORD(doubleData);
}

void YM7101::WriteDataPortWORD(word data)
{
    auto& s=get();
    if (s.dmaFillPending) { s.dmaFillPending=false;s.DMAVRAMFill(data);return; }
    const unsigned code=s.GetControlCode();
    if (code!=WRITE_VRAM && code!=WRITE_CRAM && code!=WRITE_VSRAM) return;
    while (s.fifoCount==4) Genesis::StallMasterCycles(unsigned(s.fifo[0].ready-s.totalCycles));
    const uint64_t after=s.fifoCount ? s.fifo[s.fifoCount-1].ready:s.totalCycles;
    s.fifo[s.fifoCount++]={s.GetAddressRegister(),data,code,s.NextAccess(after,code==WRITE_VRAM?2:1)};
    s.IncrementAddress();s.controlPortPending=false;
}

word YM7101::ReadDataPortWORD()
{
    while (get().fifoCount) Genesis::StallMasterCycles(unsigned(get().fifo[0].ready-get().totalCycles));
	int code = get().GetControlCode();

	if(code == WRITE_VRAM || code == WRITE_REG || code == WRITE_CRAM || code == WRITE_VSRAM)
	{
		//on ignore les writes
		return 0;
	}

	get().controlPortPending = false;

	word result = 0;
	switch(code)
	{
		case READ_VRAM:
		result = get().ReadVram();
		break;

		case READ_CRAM:
		result = get().ReadCram();
		break;

		case READ_VSRAM:
		result = get().ReadVsram();
		break;
	}

	get().IncrementAddress();

	return result;
}

int YM7101::GetControlCode()
{
	byte lo = (byte)(get().controlPortData >> 30) & 0x3;
	byte hi = (byte)(get().controlPortData >> 4) & 0x3;
	return (int)((hi << 2) | lo);
}

void YM7101::UpdateRegister(word data)
{
	byte reg = (data >> 8) & 0x7F;
	if(reg < 24)
	{
        if(reg==0)
        {
            const bool freeze=(data&2)!=0;
            if(freeze&&!get().hvFrozen) get().hvLatch=GetHVCounter();
            get().hvFrozen=freeze;
        }
		get().vdpRegister[reg] = data & 0xFF;
        if (reg==0 || reg==1) get().requestInt=(get().verticalPending && TestBit(get().vdpRegister[1],5)) || (get().horizontalPending && TestBit(get().vdpRegister[0],4));
        const unsigned width=get().Is40Cell()?320:256;
        const unsigned height=Genesis::IsPAL() && TestBit(get().vdpRegister[1],3)?240:224;
        get().vdpResolution=std::make_pair(int(width),int(height));
        get().is256Screen=width==256;
        CRT::SetResolution(width,((get().vdpRegister[12]&6)==6)?height*2:height);
	}
}

void YM7101::DMA(word data)
{
	int mode = (get().vdpRegister[23] >> 6) & 0x3;
	switch(mode)
	{
		case 0:
		case 1:
		get().DMAFromM68KToVRAM();
		break;

		case 2:
		get().DMAVRAMFill(data);
		break;

		case 3:
		get().DMAVRAMCopy();
		break;
	}
}

uint64_t YM7101::NextAccess(uint64_t after, unsigned slots)
{
    // Access positions are hardware timing facts, in master clocks from HBLANK.
    static const unsigned h32[]={230,510,810,970,1130,1450,1610,1770,2090,2250,2410,2730,2890,3050,3350,3370};
    static const unsigned h40[]={352,820,948,1076,1332,1460,1588,1844,1972,2100,2356,2484,2612,2868,2996,3124,3364,3380};
    uint64_t result=after;
    while(slots--) {
        const uint64_t delta=result-totalCycles;
        const unsigned position=unsigned((lineCycles+delta)%3420);
        const unsigned line=unsigned((scanline+(lineCycles+delta)/3420)%(Genesis::IsPAL()?313:262));
        const bool blank=line>=unsigned(vdpResolution.second) || !TestBit(vdpRegister[1],6);
        unsigned next=3420;
        if (blank) {
            const unsigned count=Is40Cell()?204:166;
            next=unsigned((uint64_t(position)*count/3420+1)*3420/count);
            if(next<=position) ++next;
        } else {
            const unsigned* table=Is40Cell()?h40:h32;
            const unsigned count=Is40Cell()?18:16;
            for(unsigned i=0;i<count;++i) if(table[i]>position) {next=table[i];break;}
        }
        result+=next-position;
        if (!blank && next==3420) ++slots;
    }
    return result;
}
void YM7101::CommitWrite(word address,unsigned code,word value)
{
    if(code==WRITE_VRAM) {
        if(address&1) InvertWordEndian(value);
        address&=0xfffe;vram[address]=byte(value>>8);vram[address+1]=byte(value);
    } else if(code==WRITE_CRAM) cram[(address&0x7f)>>1]=value&0x0eee;
    else if(code==WRITE_VSRAM) {
        const unsigned index=(address&0x7f)>>1;
        if(index<40) vsram[index]=value&0x7ff;
    }
}
void YM7101::DMAFromM68KToVRAM()
{
    dmaRemaining=(vdpRegister[20]<<8)|vdpRegister[19];if(!dmaRemaining) dmaRemaining=65536;
    dmaSource=((vdpRegister[23]&0x7f)<<17)|(vdpRegister[22]<<9)|(vdpRegister[21]<<1);
    dmaCode=GetControlCode()&7;dmaType=1;
    dmaReady=NextAccess(fifoCount ? fifo[fifoCount-1].ready:totalCycles,dmaCode==WRITE_VRAM?2:1);
}
void YM7101::DMAVRAMFill(word data)
{
    dmaRemaining=(vdpRegister[20]<<8)|vdpRegister[19];if(!dmaRemaining) dmaRemaining=65536;
    dmaType=2;dmaValue=data;dmaCode=WRITE_VRAM;
    CommitWrite(GetAddressRegister(),WRITE_VRAM,data);
    dmaReady=NextAccess(fifoCount ? fifo[fifoCount-1].ready:totalCycles,3);
}
void YM7101::DMAVRAMCopy()
{
    dmaRemaining=(vdpRegister[20]<<8)|vdpRegister[19];if(!dmaRemaining) dmaRemaining=65536;
    dmaSource=(vdpRegister[22]<<8)|vdpRegister[21];dmaType=3;
    dmaReady=NextAccess(fifoCount ? fifo[fifoCount-1].ready:totalCycles,2);
}
void YM7101::DmaStep()
{
    if(dmaType==1) {
        const word value=(dmaSource&0xe700e0)==0xc00000 ? 0xffff:Genesis::M68KReadMemoryWORD(dmaSource);
        CommitWrite(GetAddressRegister(),dmaCode,value);
        dmaSource=(dmaSource&0xfe0000)|((dmaSource+2)&0x1ffff);
        vdpRegister[21]=byte(dmaSource>>1);vdpRegister[22]=byte(dmaSource>>9);
        vdpRegister[23]=byte((vdpRegister[23]&0x80)|(dmaSource>>17));
    } else if(dmaType==2) vram[GetAddressRegister()^1]=byte(dmaValue>>8);
    else {vram[GetAddressRegister()]=vram[dmaSource++];vdpRegister[21]=byte(dmaSource);vdpRegister[22]=byte(dmaSource>>8);}
    IncrementAddress();--dmaRemaining;
    vdpRegister[19]=byte(dmaRemaining);vdpRegister[20]=byte(dmaRemaining>>8);
    if(!dmaRemaining) dmaType=0;
    else dmaReady=NextAccess(totalCycles,dmaType==3 || (dmaType==1 && dmaCode==WRITE_VRAM)?2:1);
}

void YM7101::WriteVram(word data)
{
	word address = get().GetAddressRegister();

	if(TestBit(address, 0))
	{
		InvertWordEndian(data);
	}

	address &= 0xfffe;
	get().vram[address] = data >> 8;
	get().vram[address + 1] = data & 0xFF;
}

void YM7101::WriteCram(word data)
{
	word address = get().GetAddressRegister() % 0x80;
	address >>= 1;
	get().cram[address] = data;
}

void YM7101::WriteVsram(word data)
{
	word address = get().GetAddressRegister() % 0x80;
	address >>= 1;
	get().vsram[address] = data;
}

word YM7101::ReadVram()
{
	word address = get().GetAddressRegister();
	address &= 0xfffe;
	return get().vram[address] << 8 | get().vram[address + 1];
}

word YM7101::ReadCram()
{
	word address = get().GetAddressRegister() % 0x80;
	address >>= 1;
	return get().cram[address];
}

word YM7101::ReadVsram()
{
	word address = get().GetAddressRegister() % 0x80;
	address >>= 1;
	return get().vsram[address];
}

bool YM7101::GetRequestInt()
{
	return get().requestInt;
}

int YM7101::GetIntType()
{
    if (get().verticalPending && TestBit(get().vdpRegister[1],5)) {
        get().verticalPending=false;get().status &= ~word(0x80);get().requestInt=get().horizontalPending && TestBit(get().vdpRegister[0],4); return 6;
    }
    if (get().horizontalPending && TestBit(get().vdpRegister[0],4)) {
        get().horizontalPending=false;get().requestInt=get().verticalPending && TestBit(get().vdpRegister[1],5); return 4;
    }
    get().requestInt=false;return -1;
}

word YM7101::GetBaseLayerA()
{
	return ((get().vdpRegister[2] & 0x38) << 10);
}

word YM7101::GetBaseLayerB()
{
	return ((get().vdpRegister[4] & 0x7) << 13);
}

word YM7101::GetBaseWindow()
{
	return ((get().vdpRegister[3] & 0x3E) << 10);
}

word YM7101::GetSpriteBase()
{
	word result = ((get().vdpRegister[5] & 0x7F) << 9);

	if(get().Is40Cell())
	{
		BitReset(result, 9);
	}

	return result;
}

bool YM7101::Is40Cell()
{	
	if(TestBit(get().vdpRegister[0xC], 7))
	{
		return true;
	}

	if(TestBit(get().vdpRegister[0xC], 0))
	{
		return true;
	}

	return false;
}

byte YM7101::GetColorShade(byte data)
{
	if(!TestBit(get().vdpRegister[0], 2))
	{
		data &= 0x3; //8color mode
	}

	BitReset(data, 0);

	return (data << 4);
}

int YM7101::GetPatternResolution(int data)
{
	switch(data)
	{
		case 0:
		return 32;
		break;

		case 1:
		return 64;
		break;

		case 3:
		return 128;
		break;
	}

	return 32;
}

std::pair<int, int> YM7101::GetLayerSize()
{
	byte data = get().vdpRegister[0x10];
	int width = data & 0x3;
	int height = (data >> 4) & 0x3;
	return std::make_pair<int, int>(get().GetPatternResolution(width), GetPatternResolution(height));
}

word YM7101::GetVScroll(int layer, int doubleColumnNum)
{
	switch(layer)
	{
		case LAYER_A:
		{
			if(!TestBit(get().vdpRegister[0xB], 2))
			{
				return get().vsram[0x0];
			}
			else
			{
				return get().vsram[doubleColumnNum * 2];
			}
		}
		break;

		case LAYER_B:
		{
			if(!TestBit(get().vdpRegister[0xB], 2))
			{
				return get().vsram[0x1];
			}
			else
			{
				return get().vsram[(doubleColumnNum * 2) + 1];
			}
		}
		break;
	}

	return 0;
}

void YM7101::GetHScroll(int line, word& aScroll, word& bScroll)
{
	word address = ((get().vdpRegister[0xD] & 0x3F) << 10);

	byte size = get().vdpRegister[0xB] & 0x3;

	switch(size)
	{
		case 0:
		{
			aScroll = (get().vram[address] << 8) | get().vram[address + 1];
			bScroll = (get().vram[address + 2] << 8) | get().vram[address + 3];
		}
		break;

		case 1:
		{
			int value = ((line & 7) * 4);
			aScroll = (get().vram[address + value] << 8) | get().vram[address + value + 1];
			bScroll = (get().vram[address + value + 2] << 8) | get().vram[address + value + 3];
		}
		break;

		case 2:
		{
			int value = ((line & ~7) * 4);
			aScroll = (get().vram[address + value] << 8) | get().vram[address + value + 1];
			bScroll = (get().vram[address + value + 2] << 8) | get().vram[address + value + 3];
		}
		break;

		case 3:
		{
			int value = (line * 4);
			aScroll = (get().vram[address + value] << 8) | get().vram[address + value + 1];
			bScroll = (get().vram[address + value + 2] << 8) | get().vram[address + value + 3];
		}
		break;
	}
	aScroll &= 0x3FF;
	bScroll &= 0x3FF;
}

void YM7101::Render()
{
    auto& s=get();
    const bool interlace=(s.vdpRegister[12]&6)==6;
    const unsigned tileHeight=interlace?16:8;
    const unsigned outputY=interlace?(unsigned(s.vCounter)*2+((s.status>>4)&1)):s.vCounter;
    if(s.vCounter>=s.vdpResolution.second) return;
    const unsigned screenWidth=s.vdpResolution.first;
    struct Pixel { unsigned color=0; bool high=false; };
    auto vword=[&](unsigned address) { return word((word(s.vram[address])<<8)|s.vram[address+1]); };
    auto pattern=[&](word attribute,unsigned x,unsigned y) {
        if(attribute&0x0800) x=7-x;
        if(attribute&0x1000) y=tileHeight-1-y;
        const unsigned address=(attribute&0x07ff)*(interlace?64:32)+y*4+x/2;
        const byte packed=s.vram[address];
        return unsigned((x&1)?packed&15:packed>>4);
    };
    const auto size=s.GetLayerSize();
    word scrollA=0,scrollB=0;
    s.GetHScroll(s.vCounter,scrollA,scrollB);
    auto plane=[&](unsigned layer,unsigned x) {
        Pixel pixel;
        const unsigned scroll=layer?scrollB:scrollA;
        const unsigned sx=(x-scroll)&(unsigned(size.first)*8-1);
        const unsigned vertical=s.GetVScroll(layer,x/16)&(interlace?0x7ff:0x3ff);
        const unsigned sy=(outputY+vertical)&(unsigned(size.second)*tileHeight-1);
        const unsigned base=layer?s.GetBaseLayerB():s.GetBaseLayerA();
        const word attribute=vword(base+((sy/tileHeight)*size.first+sx/8)*2);
        const unsigned color=pattern(attribute,sx&7,sy&(tileHeight-1));
        pixel.color=color?(((attribute>>9)&0x30)|color):0;
        pixel.high=(attribute&0x8000)!=0;
        return pixel;
    };
    auto window=[&](unsigned x) {
        Pixel pixel;
        const unsigned columns=s.Is40Cell()?64:32;
        const unsigned base=(s.vdpRegister[3]&(s.Is40Cell()?0x3c:0x3e))<<10;
        const word attribute=vword(base+((outputY/tileHeight)*columns+x/8)*2);
        const unsigned color=pattern(attribute,x&7,outputY&(tileHeight-1));
        pixel.color=color?(((attribute>>9)&0x30)|color):0;
        pixel.high=(attribute&0x8000)!=0;
        return pixel;
    };
    std::array<Pixel,320> sprites{};
    const unsigned maximumSprites=s.Is40Cell()?80:64;
    const unsigned maximumOnLine=s.Is40Cell()?20:16;
    const unsigned spriteBase=s.GetSpriteBase();
    unsigned link=0,onLine=0,dots=0;
    bool earlierNonzeroX=false;
    for(unsigned visited=0;visited<maximumSprites&&TestBit(s.vdpRegister[1],6);++visited)
    {
        if(link>=maximumSprites) break;
        const unsigned at=spriteBase+link*8;
        const unsigned rawY=vword(at)&(interlace?0x3ff:0x1ff);
        const int sy=int(rawY)-(interlace?256:128);
        const byte dimensions=s.vram[at+2];
        const unsigned tilesHigh=(dimensions&3)+1,tilesWide=((dimensions>>2)&3)+1;
        const unsigned rawX=vword(at+6)&0x1ff;
        const int sx=int(rawX)-128;
        const word attribute=vword(at+4);
        link=s.vram[at+3]&0x7f;
        if(int(outputY)>=sy&&int(outputY)<sy+int(tilesHigh*tileHeight))
        {
            if(++onLine>maximumOnLine) { s.status|=0x40;break; }
            if(rawX==0&&earlierNonzeroX) break;
            earlierNonzeroX|=rawX!=0;
            unsigned py=unsigned(int(outputY)-sy);
            if(attribute&0x1000) py=tilesHigh*tileHeight-1-py;
            for(unsigned pos=0;pos<tilesWide*8;++pos)
            {
                if(dots++>=screenWidth) { s.status|=0x40;break; }
                const int x=sx+int(pos);
                if(x<0||x>=int(screenWidth)) continue;
                const unsigned px=(attribute&0x0800)?tilesWide*8-1-pos:pos;
                const unsigned tile=(attribute&0x7ff)+(px/8)*tilesHigh+py/tileHeight;
                const word tileAttribute=word((attribute&0xe000)|(tile&0x7ff));
                const unsigned color=pattern(tileAttribute,px&7,py&(tileHeight-1));
                if(!color) continue;
                if(sprites[x].color) s.status|=0x20;
                else sprites[x]={unsigned(((attribute>>9)&0x30)|color),(attribute&0x8000)!=0};
            }
            if(dots>screenWidth) break;
        }
        if(link==0) break;
    }
    for(unsigned x=0;x<screenWidth;++x)
    {
        unsigned selected=s.vdpRegister[7]&0x3f;
        int intensity=INTENSITY_NORMAL;
        if(TestBit(s.vdpRegister[1],6)&&!TestBit(s.vdpRegister[0],0)&&!(TestBit(s.vdpRegister[0],5)&&x<8))
        {
            const unsigned edgeX=(s.vdpRegister[17]&31)*16;
            const unsigned edgeY=(s.vdpRegister[18]&31)*tileHeight;
            const bool horizontal=(s.vdpRegister[17]&0x80)?x>=edgeX:x<edgeX;
            const bool vertical=(s.vdpRegister[18]&0x80)?outputY>=edgeY:outputY<edgeY;
            const Pixel a=(horizontal||vertical)?window(x):plane(0,x);
            const Pixel b=plane(1,x);
            const Pixel sprite=sprites[x];
            const bool shadows=TestBit(s.vdpRegister[12],3);
            intensity=shadows&&!a.high&&!b.high?INTENSITY_SHADOW:INTENSITY_NORMAL;
            // Plane priorities are resolved before selecting the first SAT sprite.
            if(b.color) selected=b.color;
            if(a.color&&(!b.high||a.high||!b.color)) selected=a.color;
            const bool highPlane=(a.color&&a.high)||(b.color&&b.high);
            if(sprite.color&&(sprite.high||!highPlane))
            {
                if(shadows&&sprite.color==0x3e) intensity=std::min(INTENSITY_LIGHT,intensity+1);
                else if(shadows&&sprite.color==0x3f) intensity=INTENSITY_SHADOW;
                else
                {
                    selected=sprite.color;
                    if(sprite.high||(sprite.color&15)==14) intensity=INTENSITY_NORMAL;
                }
            }
        }
        const word color=s.cram[selected];
        CRT::Write(x,outputY,s.GetColorShade(color&15),s.GetColorShade((color>>4)&15),s.GetColorShade((color>>8)&15));
        if(intensity!=INTENSITY_NORMAL) CRT::ApplyIntensity(x,outputY,intensity);
    }
}

void YM7101::Update(int clicks) { if (clicks>0) Advance(unsigned(clicks)*7); }
unsigned YM7101::TakeStallCycles() { return get().dmaType==1 ? unsigned(get().dmaReady-get().totalCycles):0; }
bool YM7101::Z80Interrupt()
{
    const unsigned edge=get().Is40Cell()?788:770;
    const unsigned blank=get().vdpResolution.second;
    return (get().scanline==blank && get().lineCycles>=edge) ||
           (get().scanline==blank+1 && get().lineCycles<edge);
}
unsigned YM7101::Scanline() { return get().scanline; }
unsigned YM7101::LineCycles() { return get().lineCycles; }
void YM7101::Advance(unsigned ticks)
{
    auto& s=get();
    while (ticks) {
        unsigned step=std::min(ticks,3420-s.lineCycles);
        const unsigned vintEdge=s.Is40Cell()?788:770;
        if(s.scanline==unsigned(s.vdpResolution.second)&&s.lineCycles<vintEdge)
            step=std::min(step,vintEdge-s.lineCycles);
        if(s.fifoCount) step=std::min<unsigned>(step,unsigned(s.fifo[0].ready-s.totalCycles));
        if(s.dmaType) step=std::min<unsigned>(step,unsigned(s.dmaReady-s.totalCycles));
        s.lineCycles+=step;ticks-=step;s.totalCycles+=step;
        if(s.fifoCount && s.fifo[0].ready==s.totalCycles) {
            const auto entry=s.fifo[0];s.CommitWrite(entry.address,entry.code,entry.value);
            for(unsigned i=1;i<s.fifoCount;++i) s.fifo[i-1]=s.fifo[i];
            --s.fifoCount;
        }
        if(s.dmaType && s.dmaReady==s.totalCycles) s.DmaStep();
        if (s.lineCycles>=3420) {
            s.lineCycles=0;
            if (s.scanline<unsigned(s.vdpResolution.second)) { s.vCounter=word(s.scanline);s.Render(); }
            if (s.scanline<=unsigned(s.vdpResolution.second)) {
                if (!s.lineInt) { s.lineInt=s.vdpRegister[10];s.horizontalPending=true; }
                else --s.lineInt;
            } else s.lineInt=s.vdpRegister[10];
            ++s.scanline;

            if (s.scanline>=unsigned(Genesis::IsPAL()?313:262)) {
                s.scanline=0;s.lineInt=s.vdpRegister[10];
                BitReset(s.status,7);BitReset(s.status,5);BitReset(s.status,6);
                if (TestBit(s.vdpRegister[12],1)) s.status^=0x10;
            }
        }
        if(s.scanline==unsigned(s.vdpResolution.second)&&s.lineCycles==vintEdge)
        {
            s.status|=0x80;s.verticalPending=true;
        }
        if (s.scanline>=unsigned(s.vdpResolution.second) || !TestBit(s.vdpRegister[1],6)) BitSet(s.status,3);else BitReset(s.status,3);
        const unsigned blankStart=s.Is40Cell()?228:280,blankEnd=s.Is40Cell()?872:860;
        if (s.lineCycles>=blankStart && s.lineCycles<blankEnd) BitSet(s.status,2);else BitReset(s.status,2);
        s.requestInt=(s.verticalPending && TestBit(s.vdpRegister[1],5)) || (s.horizontalPending && TestBit(s.vdpRegister[0],4));
    }
}
