#include "ui.hpp"
#include "../platform/storage.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <stdexcept>

namespace {
bool Inside(sfFloatRect rectangle,float x,float y) {
    return x>=rectangle.left&&y>=rectangle.top&&x<rectangle.left+rectangle.width&&y<rectangle.top+rectangle.height;
}
sfColor Fade(sfColor color,unsigned char alpha){color.a=alpha;return color;}
sfColor Accent(Ui::Tone tone) {
    return tone==Ui::Tone::Danger?Ui::Red:tone==Ui::Tone::Accent?Ui::Green:Ui::Blue;
}
float Rounded(float value){return std::floor(value+0.5f);}
}

struct Ui::Canvas::Impl {
    struct Item {std::string id;sfFloatRect area;bool enabled;Tone tone;};
    sfRenderWindow* window=nullptr;
    sfFont *main=nullptr,*upper=nullptr;
    std::vector<unsigned char> mainBytes,upperBytes;
    sfRectangleShape* rectangle=sfRectangleShape_create();
    sfText* text=sfText_create();
    sfSprite* sprite=sfSprite_create();
    sfView* view=sfView_createFromRect({0,0,640,360});
    Input::UiActions input;
    Pointer pointer;
    std::vector<Item> items;
    std::string focus;
    Impl(){if(!rectangle||!text||!sprite||!view)throw std::runtime_error("Cannot create retro interface renderer");}
    ~Impl(){
        if(sprite)sfSprite_destroy(sprite);
        if(text)sfText_destroy(text);
        if(rectangle)sfRectangleShape_destroy(rectangle);
        if(view)sfView_destroy(view);
        if(main)sfFont_destroy(main);
        if(upper)sfFont_destroy(upper);
    }
    const sfFont* Font(sfUint32 point)const {
        if(!main)throw std::runtime_error("Interface font is not loaded");
        // These pinned Unifont files divide coverage at the BMP boundary:
        // unifont.otf supplies U+0000..FFFF; unifont_upper.otf supplies the
        // supplementary planes. Avoid sfFont_hasGlyph, which is absent from
        // distro CSFML 2.6.0 even though that supported release has the other
        // required font APIs. Missing glyphs retain the font's replacement.
        if(point>0xffff&&upper)return upper;
        return main;
    }
    float Advance(sfUint32 point,unsigned size)const {
        if(point=='\t')point=' ';
        return sfFont_getGlyph(Font(point),point,size,sfFalse,0).advance;
    }
    float Width(const std::vector<sfUint32>& line,unsigned size)const {
        float width=0;const sfFont* previousFont=nullptr;sfUint32 previous=0;
        for(auto point:line) {
            const auto* font=Font(point);
            if(font==previousFont)width+=sfFont_getKerning(font,previous,point,size);
            width+=Advance(point,size);previous=point;previousFont=font;
        }
        return width;
    }
    void Line(std::vector<sfUint32> line,float x,float y,unsigned size,sfColor color,float maxWidth) {
        if(!window||line.empty()||!size)return;
        if(maxWidth>0) {
            std::vector<float> widths{0};const sfFont* previousFont=nullptr;sfUint32 previous=0;
            for(auto point:line) {
                const auto* font=Font(point);auto width=widths.back()+Advance(point,size);
                if(font==previousFont)width+=sfFont_getKerning(font,previous,point,size);
                widths.push_back(width);previous=point;previousFont=font;
            }
            if(widths.back()>maxWidth) {
            const sfUint32 ellipsis=0x2026;const auto ellipsisWidth=Advance(ellipsis,size);
            while(!line.empty()&&widths[line.size()]+ellipsisWidth>maxWidth)line.pop_back();
            if(ellipsisWidth<=maxWidth)line.push_back(ellipsis);else line.clear();
            }
        }
        // Keep one baseline for both fonts; only the needed fallback glyphs use
        // the upper-plane font. sfText copies the terminated glyph run.
        float cursor=x;const sfFont* currentFont=nullptr;std::vector<sfUint32> run;
        const auto draw=[&]{
            if(run.empty())return;
            const auto advance=Width(run,size);run.push_back(0);
            sfText_setFont(text,currentFont);sfText_setCharacterSize(text,size);sfText_setUnicodeString(text,run.data());
            sfText_setFillColor(text,color);sfText_setPosition(text,{Rounded(cursor),Rounded(y)-2});
            sfRenderWindow_drawText(window,text,nullptr);cursor+=advance;run.clear();
        };
        for(auto point:line) {
            if(point=='\t') {draw();cursor+=Advance(' ',size)*4;currentFont=nullptr;continue;}
            if(!point)point=0xfffd;
            const auto* font=Font(point);
            if(font!=currentFont){draw();currentFont=font;}
            run.push_back(point);
        }
        draw();
    }
};

Ui::Canvas::Canvas():impl_(new Impl){}
Ui::Canvas::~Canvas()=default;
void Ui::Canvas::LoadFonts(const std::string& mainPath,const std::string& upperPath) {
    auto mainBytes=Storage::ReadBytes(mainPath,16*1024*1024);
    auto upperBytes=Storage::ReadBytes(upperPath,16*1024*1024);
    auto* main=sfFont_createFromMemory(mainBytes.data(),mainBytes.size());
    if(!main)throw std::runtime_error("Cannot load the DrivOnix interface font");
    auto* upper=sfFont_createFromMemory(upperBytes.data(),upperBytes.size());
    if(!upper){sfFont_destroy(main);throw std::runtime_error("Cannot load the Unicode interface font");}
    sfFont_setSmooth(main,sfFalse);sfFont_setSmooth(upper,sfFalse);
    if(impl_->main)sfFont_destroy(impl_->main);
    if(impl_->upper)sfFont_destroy(impl_->upper);
    impl_->mainBytes=std::move(mainBytes);impl_->upperBytes=std::move(upperBytes);
    impl_->main=main;impl_->upper=upper;
}
void Ui::Canvas::Begin(sfRenderWindow* window,const Input::UiActions& input,const Pointer& pointer) {
    if(!window)throw std::invalid_argument("Interface window is missing");
    impl_->window=window;impl_->input=input;impl_->pointer=pointer;impl_->items.clear();
    const auto pixels=sfRenderWindow_getSize(window);
    const float width=float(std::max(1u,pixels.x)),height=float(std::max(1u,pixels.y));
    const auto scale=std::min(width/640.f,height/360.f);
    sfView_setViewport(impl_->view,{(width-640*scale)/(2*width),(height-360*scale)/(2*height),640*scale/width,360*scale/height});
    sfRenderWindow_setView(window,impl_->view);
}
void Ui::Canvas::Rect(sfFloatRect area,sfColor fill,sfColor outline,float border) {
    if(!impl_->window||area.width<=0||area.height<=0)return;
    sfRectangleShape_setPosition(impl_->rectangle,{Rounded(area.left),Rounded(area.top)});
    sfRectangleShape_setSize(impl_->rectangle,{Rounded(area.width),Rounded(area.height)});
    sfRectangleShape_setRotation(impl_->rectangle,0);
    sfRectangleShape_setFillColor(impl_->rectangle,fill);sfRectangleShape_setOutlineColor(impl_->rectangle,outline);
    sfRectangleShape_setOutlineThickness(impl_->rectangle,outline.a?border:0);
    sfRenderWindow_drawRectangleShape(impl_->window,impl_->rectangle,nullptr);
}
void Ui::Canvas::Text(const std::string& text,float x,float y,unsigned size,sfColor color,float maxWidth) {
    auto characters=Decode(text);std::vector<sfUint32> line;
    for(auto character:characters) {
        if(character=='\n'){impl_->Line(std::move(line),x,y,size,color,maxWidth);line.clear();y+=size+3;}
        else if(character!='\r')line.push_back(character);
    }
    impl_->Line(std::move(line),x,y,size,color,maxWidth);
}
void Ui::Canvas::Wrap(const std::string& text,sfFloatRect area,unsigned size,sfColor color) {
    if(area.width<=0||area.height<=0||!size)return;
    const auto characters=Decode(text);std::vector<sfUint32> line;float y=area.top;
    const auto emit=[&]{if(y+size<=area.top+area.height)impl_->Line(line,area.left,y,size,color,area.width);y+=size+3;line.clear();};
    for(auto character:characters) {
        if(character=='\r')continue;
        if(character=='\n'){emit();if(y>=area.top+area.height)return;continue;}
        line.push_back(character);
        if(impl_->Width(line,size)>area.width) {
            const auto lastSpace=std::find(line.rbegin(),line.rend(),sfUint32(' '));
            if(lastSpace!=line.rend()&&std::size_t(std::distance(line.rbegin(),lastSpace))+1<line.size()) {
                const auto split=line.size()-std::size_t(std::distance(line.rbegin(),lastSpace))-1;
                std::vector<sfUint32> tail(line.begin()+split+1,line.end());line.resize(split);emit();line=std::move(tail);
            } else {line.pop_back();emit();if(character!=' ')line.push_back(character);}
            if(y>=area.top+area.height)return;
        }
    }
    if(!line.empty()&&y+size<=area.top+area.height)impl_->Line(std::move(line),area.left,y,size,color,area.width);
}
void Ui::Canvas::Image(const sfTexture* texture,sfFloatRect area,sfIntRect source) {
    if(!texture||!impl_->window||area.width<=0||area.height<=0)return;
    const auto size=sfTexture_getSize(texture);
    if(source.width<=0||source.height<=0)source={0,0,int(size.x),int(size.y)};
    if(source.left<0||source.top<0||source.width<=0||source.height<=0||
       unsigned(source.left)+unsigned(source.width)>size.x||unsigned(source.top)+unsigned(source.height)>size.y)return;
    const auto scale=std::min(area.width/float(source.width),area.height/float(source.height));
    const auto width=source.width*scale,height=source.height*scale;
    sfSprite_setTexture(impl_->sprite,texture,sfTrue);sfSprite_setTextureRect(impl_->sprite,source);
    sfSprite_setScale(impl_->sprite,{scale,scale});sfSprite_setColor(impl_->sprite,sfWhite);
    sfSprite_setPosition(impl_->sprite,{Rounded(area.left+(area.width-width)/2),Rounded(area.top+(area.height-height)/2)});
    sfRenderWindow_drawSprite(impl_->window,impl_->sprite,nullptr);
}
void Ui::Canvas::Button(const std::string& id,const std::string& label,sfFloatRect area,bool enabled,Tone tone,unsigned textSize) {
    if(id.empty())throw std::invalid_argument("Interface action needs an identity");
    for(const auto& item:impl_->items)if(item.id==id)throw std::logic_error("Duplicate interface action: "+id);
    impl_->items.push_back({id,area,enabled,tone});
    const bool focused=enabled&&id==impl_->focus;
    const auto accent=Accent(tone);
    auto fill=tone==Tone::Quiet?sfColor{9,13,22,245}:Panel;
    if(focused)fill=sfColor{25,36,51,255};
    if(!enabled)fill=sfColor{10,14,22,255};
    Rect({area.left+1,area.top+2,area.width,area.height},sfColor{0,0,0,100});
    Rect(area,fill,enabled?(focused?Fade(accent,160):Border):sfColor{24,32,43,255});
    if(tone!=Tone::Quiet)Rect({area.left+1,area.top+1,2,area.height-2},enabled?Fade(accent,focused?255:115):Fade(Muted,50));
    if(!label.empty()) {
        const auto line=Decode(label);const auto width=impl_->Width(line,textSize);
        Text(label,area.left+std::max(6.f,(area.width-width)/2),area.top+(area.height-textSize)/2,textSize,
            enabled?(tone==Tone::Danger?sfColor{255,174,179,255}:White):sfColor{91,105,126,255},std::max(0.f,area.width-12));
    }
}
void Ui::Canvas::Card(const std::string& id,const std::string& label,sfFloatRect area,const sfTexture* image,bool favorite,bool available) {
    Button(id,"",area,true,available?Tone::Normal:Tone::Quiet);
    const sfFloatRect art={area.left+5,area.top+5,area.width-10,std::max(1.f,area.height-34)};
    Rect(art,{4,7,13,255});
    if(image)Image(image,art);
    else {
        const auto center=art.top+art.height/2;
        for(unsigned i=0;i<3;++i)Rect({art.left+7+float(i)*7,center-16,4,14},i==0?Red:i==1?Green:Blue);
        Text("DRIVONIX",art.left+5,center+3,11,Muted,art.width-10);
        Text(available?"NO COVER":"UNAVAILABLE",art.left+5,center+20,8,Muted,art.width-10);
    }
    Rect({area.left+5,area.top+area.height-26,area.width-10,1},Border);
    Text(label,area.left+6,area.top+area.height-21,11,available?White:Muted,area.width-12);
    if(favorite) {
        Rect({area.left+area.width-20,area.top+6,14,15},sfColor{8,12,19,230});
        Text("*",area.left+area.width-17,area.top+6,13,Green);
    }
}
std::string Ui::Canvas::Finish() {
    auto find=[&](const std::string& id)->const Impl::Item* {
        for(const auto& item:impl_->items)if(item.enabled&&item.id==id)return &item;
        return nullptr;
    };
    const Impl::Item* hovered=nullptr;
    if(impl_->pointer.moved||impl_->pointer.clicked)
        for(auto item=impl_->items.rbegin();item!=impl_->items.rend();++item)
            if(item->enabled&&Inside(item->area,impl_->pointer.x,impl_->pointer.y)){hovered=&*item;break;}
    if(hovered)impl_->focus=hovered->id;
    const auto* current=find(impl_->focus);
    if(!current)for(const auto& item:impl_->items)if(item.enabled){impl_->focus=item.id;current=&item;break;}
    const int dx=int(impl_->input.right)-int(impl_->input.left),dy=int(impl_->input.down)-int(impl_->input.up);
    if(current&&(dx||dy)) {
        const auto x=current->area.left+current->area.width/2,y=current->area.top+current->area.height/2;
        const Impl::Item* next=nullptr;float best=std::numeric_limits<float>::max();
        for(const auto& item:impl_->items) {
            if(!item.enabled||item.id==current->id)continue;
            const float distanceX=item.area.left+item.area.width/2-x,distanceY=item.area.top+item.area.height/2-y;
            const float forward=dy?distanceY*dy:distanceX*dx,sideways=dy?std::abs(distanceX):std::abs(distanceY);
            if(forward<=0.5f)continue;
            const auto score=forward+sideways*4;
            if(score<best){best=score;next=&item;}
        }
        if(next){impl_->focus=next->id;current=next;}
    }
    if(current) {
        const auto area=current->area;const auto accent=Accent(current->tone);
        Rect({area.left-2,area.top-2,area.width+4,area.height+4},sfTransparent,accent,1);
        Rect({area.left-3,area.top-3,5,2},accent);Rect({area.left-3,area.top-3,2,5},accent);
        Rect({area.left+area.width-2,area.top+area.height+1,5,2},accent);
        Rect({area.left+area.width+1,area.top+area.height-2,2,5},accent);
    }
    if(impl_->pointer.clicked&&hovered)return hovered->id;
    if(impl_->input.confirm&&current)return current->id;
    return {};
}
const std::string& Ui::Canvas::Focus()const{return impl_->focus;}
std::vector<Ui::ControlInfo> Ui::Canvas::Controls()const {
    std::vector<ControlInfo> result;result.reserve(impl_->items.size());
    for(const auto& item:impl_->items)result.push_back({item.id,item.area,item.enabled});
    return result;
}
void Ui::Canvas::Focus(const std::string& id){impl_->focus=id;}
void Ui::Canvas::ClearFocus(){impl_->focus.clear();}
sfVector2f Ui::Canvas::ToLogical(sfVector2i pixels)const {
    if(!impl_->window)return {-100,-100};
    const auto size=sfRenderWindow_getSize(impl_->window);
    if(!size.x||!size.y)return {-100,-100};
    const auto scale=std::min(float(size.x)/640,float(size.y)/360);
    return {(pixels.x-(float(size.x)-640*scale)/2)/scale,(pixels.y-(float(size.y)-360*scale)/2)/scale};
}
std::vector<sfUint32> Ui::Canvas::Decode(const std::string& text) {
    std::vector<sfUint32> points;
    for(std::size_t at=0;at<text.size();) {
        const auto lead=static_cast<unsigned char>(text[at]);
        if(lead<0x80){points.push_back(lead);++at;continue;}
        unsigned count=lead>=0xc2&&lead<=0xdf?2:lead>=0xe0&&lead<=0xef?3:lead>=0xf0&&lead<=0xf4?4:0;
        if(!count||count>text.size()-at){points.push_back(0xfffd);++at;continue;}
        sfUint32 value=lead&((1u<<(7-count))-1);bool valid=true;
        for(unsigned i=1;i<count;++i) {
            const auto part=static_cast<unsigned char>(text[at+i]);
            if((part&0xc0)!=0x80){valid=false;break;}value=(value<<6)|(part&0x3f);
        }
        if(!valid||(count==2&&value<0x80)||(count==3&&value<0x800)||(count==4&&value<0x10000)||
           (value>=0xd800&&value<=0xdfff)||value>0x10ffff){points.push_back(0xfffd);++at;continue;}
        points.push_back(value);at+=count;
    }
    return points;
}
std::string Ui::Canvas::Encode(const std::vector<sfUint32>& points) {
    std::string result;
    for(auto point:points) {
        if(point>0x10ffff||(point>=0xd800&&point<=0xdfff))point=0xfffd;
        if(point<0x80)result.push_back(char(point));
        else if(point<0x800){result.push_back(char(0xc0|(point>>6)));result.push_back(char(0x80|(point&0x3f)));}
        else if(point<0x10000){result.push_back(char(0xe0|(point>>12)));result.push_back(char(0x80|((point>>6)&0x3f)));result.push_back(char(0x80|(point&0x3f)));}
        else {result.push_back(char(0xf0|(point>>18)));result.push_back(char(0x80|((point>>12)&0x3f)));result.push_back(char(0x80|((point>>6)&0x3f)));result.push_back(char(0x80|(point&0x3f)));}
    }
    return result;
}
