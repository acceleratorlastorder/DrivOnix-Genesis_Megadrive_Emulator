#include "ui.hpp"
#include "../platform/storage.hpp"
#include "../library/rominfo.hpp"
#include "../thirdparty/json/json.h"
#include <SFML/Window/Context.h>
#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif
#include <GL/gl.h>

#include <algorithm>
#include <cmath>
#include <limits>
#include <map>
#include <stdexcept>
#include <cstdlib>

namespace {
bool Inside(sfFloatRect rectangle,float x,float y) {
    return x>=rectangle.left&&y>=rectangle.top&&x<rectangle.left+rectangle.width&&y<rectangle.top+rectangle.height;
}
sfColor Fade(sfColor color,unsigned char alpha){color.a=alpha;return color;}
sfColor Accent(Ui::Tone tone,const Ui::Palette& colors) {
    return tone==Ui::Tone::Danger?colors.red:tone==Ui::Tone::Accent?colors.green:colors.blue;
}
float Rounded(float value){return std::floor(value+0.5f);}
bool Finite(sfFloatRect area){return std::isfinite(area.left)&&std::isfinite(area.top)&&std::isfinite(area.width)&&std::isfinite(area.height);}
struct Weight {bool& target;bool old;explicit Weight(bool& value):target(value),old(value){target=true;}~Weight(){target=old;}};
json_value_s* Field(json_value_s* value,const char* key) {
    auto* object=value?json_value_as_object(value):nullptr;
    if(object)for(auto* item=object->start;item;item=item->next)
        if(std::string(item->name->string,item->name->string_size)==key)return item->value;
    return nullptr;
}
std::string String(json_value_s* value) {
    auto* text=value?json_value_as_string(value):nullptr;
    if(!text)throw std::runtime_error("Invalid smooth font coverage string");
    return std::string(text->string,text->string_size);
}
unsigned Number(json_value_s* value) {
    auto* number=value?json_value_as_number(value):nullptr;
    if(!number||!number->number_size)throw std::runtime_error("Invalid smooth font coverage number");
    unsigned result=0;
    for(std::size_t i=0;i<number->number_size;++i){const char c=number->number[i];
        if(c<'0'||c>'9'||result>0x10ffffu/10)throw std::runtime_error("Invalid smooth font coverage range");
        result=result*10+unsigned(c-'0');if(result>0x10ffff)throw std::runtime_error("Smooth font coverage range exceeds Unicode");}
    return result;
}
std::vector<std::pair<sfUint32,sfUint32>> Coverage(const std::vector<unsigned char>& data,
        const std::vector<unsigned char>& regular,const std::vector<unsigned char>& medium) {
    std::unique_ptr<json_value_s,decltype(&std::free)> parsed(json_parse(data.data(),data.size()),&std::free);
    if(!parsed||Number(Field(parsed.get(),"schema"))!=1)throw std::runtime_error("Unknown smooth font coverage schema");
    auto* fonts=Field(parsed.get(),"fonts");
    if(String(Field(fonts,"regular_sha256"))!=RomInfo::Sha256(regular)||String(Field(fonts,"medium_sha256"))!=RomInfo::Sha256(medium))
        throw std::runtime_error("Smooth font coverage does not match the font bytes");
    auto* value=Field(parsed.get(),"ranges");auto* array=value?json_value_as_array(value):nullptr;
    if(!array||!array->length||array->length>4096)throw std::runtime_error("Invalid smooth font coverage table");
    std::vector<std::pair<sfUint32,sfUint32>> result;
    for(auto* item=array->start;item;item=item->next){auto* range=json_value_as_array(item->value);
        if(!range||range->length!=2)throw std::runtime_error("Invalid smooth font coverage pair");
        const auto first=Number(range->start->value),last=Number(range->start->next->value);
        if(first>last||(!result.empty()&&first<=result.back().second)||(first<=0xdfff&&last>=0xd800))
            throw std::runtime_error("Unordered smooth font coverage table");
        result.push_back({first,last});
    }
    return result;
}
template<class Function> Function GlFunction(const char* name) {
    const auto function=sfContext_getFunction(name);
    if(!function)throw std::runtime_error(std::string("Framebuffer capture requires OpenGL ")+name);
    return reinterpret_cast<Function>(function);
}
struct Readback {
    using Get=void (APIENTRY*)(GLenum,GLint*);
    using Set=void (APIENTRY*)(GLenum,GLint);
    using Read=void (APIENTRY*)(GLenum);
    using Pixels=void (APIENTRY*)(GLint,GLint,GLsizei,GLsizei,GLenum,GLenum,void*);
    using Error=GLenum (APIENTRY*)();
    using Bind=void (APIENTRY*)(GLenum,GLuint);
    Get get=GlFunction<Get>("glGetIntegerv");
    Set set=GlFunction<Set>("glPixelStorei");
    Read read=GlFunction<Read>("glReadBuffer");
    Pixels pixels=GlFunction<Pixels>("glReadPixels");
    Error error=GlFunction<Error>("glGetError");
    Bind bind=nullptr;
    static constexpr GLenum packBuffer=0x88eb,packBufferBinding=0x88ed;
    const GLenum fields[6]={GL_PACK_ALIGNMENT,GL_PACK_ROW_LENGTH,GL_PACK_SKIP_ROWS,
        GL_PACK_SKIP_PIXELS,GL_PACK_SWAP_BYTES,GL_PACK_LSB_FIRST};
    GLint previous[6]{},buffer=0,source=GL_BACK;
    bool saved=false;
    explicit Readback(sfRenderWindow* window) {
        const auto settings=sfRenderWindow_getSettings(window);
        const bool hasBuffers=settings.majorVersion>2||(settings.majorVersion==2&&settings.minorVersion>=1)||
            sfContext_isExtensionAvailable("GL_ARB_pixel_buffer_object")||sfContext_isExtensionAvailable("GL_EXT_pixel_buffer_object");
        if(hasBuffers){
            auto function=sfContext_getFunction("glBindBuffer");
            if(!function)function=sfContext_getFunction("glBindBufferARB");
            if(!function)throw std::runtime_error("Cannot preserve OpenGL pixel-pack buffer during capture");
            bind=reinterpret_cast<Bind>(function);
        }
        const auto prior=error();
        if(prior!=GL_NO_ERROR)throw std::runtime_error("OpenGL error before framebuffer capture: "+std::to_string(prior));
        get(GL_READ_BUFFER,&source);
        for(unsigned i=0;i<6;++i)get(fields[i],&previous[i]);
        if(bind)get(packBufferBinding,&buffer);
        const auto status=error();
        if(status!=GL_NO_ERROR)throw std::runtime_error("Cannot query framebuffer capture state: "+std::to_string(status));
        saved=true;
        if(bind)bind(packBuffer,0);
        for(unsigned i=0;i<6;++i)set(fields[i],i==0?1:0);
        read(GL_BACK);
    }
    ~Readback(){
        if(saved){read(GLenum(source));for(unsigned i=0;i<6;++i)set(fields[i],previous[i]);
            if(bind)bind(packBuffer,GLuint(buffer));}
    }
    Readback(const Readback&)=delete;Readback& operator=(const Readback&)=delete;
};
}

void Ui::CaptureWindow(sfRenderWindow* window,const std::string& path) {
    if(!window||path.empty()||path.find('\0')!=std::string::npos)throw std::invalid_argument("Invalid framebuffer capture destination");
    const auto size=sfRenderWindow_getSize(window);
    const auto count=std::uint64_t(size.x)*size.y;
    if(!size.x||!size.y||size.x>unsigned(std::numeric_limits<GLsizei>::max())||size.y>unsigned(std::numeric_limits<GLsizei>::max())||
       count>std::numeric_limits<std::size_t>::max()/4)throw std::runtime_error("Framebuffer capture dimensions exceed the addressable image size");
    std::vector<sfUint8> bytes(std::size_t(count)*4);
    if(!sfRenderWindow_setActive(window,sfTrue))throw std::runtime_error("Cannot activate the window for framebuffer capture");
    {
        Readback state(window);
        state.pixels(0,0,GLsizei(size.x),GLsizei(size.y),GL_RGBA,GL_UNSIGNED_BYTE,bytes.data());
        const auto status=state.error();
        if(status!=GL_NO_ERROR)throw std::runtime_error("Cannot read the rendered framebuffer: "+std::to_string(status));
    }
    std::unique_ptr<sfImage,decltype(&sfImage_destroy)> image(sfImage_createFromPixels(size.x,size.y,bytes.data()),sfImage_destroy);
    if(!image)throw std::runtime_error("Cannot allocate the framebuffer image");
    sfImage_flipVertically(image.get());
    if(!sfImage_saveToFile(image.get(),path.c_str()))throw std::runtime_error("Cannot save the framebuffer capture: "+path);
}

struct Ui::Canvas::Impl {
    struct Item {std::string id;sfFloatRect area;bool enabled;Tone tone;std::string panel;};
    struct Scroll {sfFloatRect viewport{0,0,0,0};float height=0,offset=0,drawOffset=0;};
    sfRenderWindow* window=nullptr;
    sfFont *main=nullptr,*upper=nullptr,*regular=nullptr,*medium=nullptr;
    std::vector<unsigned char> mainBytes,upperBytes,regularBytes,mediumBytes;
    std::vector<std::pair<sfUint32,sfUint32>> coverage;
    ThemeId theme=ThemeId::Retronix;
    unsigned uiScale=100;
    LayoutContext context;
    bool emphasis=false,wheelUsed=false,revealFocus=false;
    sfRectangleShape* rectangle=sfRectangleShape_create();
    sfText* text=sfText_create();
    sfSprite* sprite=sfSprite_create();
    sfView* view=sfView_createFromRect({0,0,640,360});
    sfView* child=sfView_createFromRect({0,0,640,360});
    std::vector<sfVector2f> roundPoints;
    sfShape* rounded=nullptr;
    Input::UiActions input;
    Pointer pointer;
    std::vector<Item> items;
    std::map<std::string,Scroll> panels;
    std::string panel,lastFocus;
    std::string focus;
    Impl(){
        rounded=sfShape_create([](void* p)->std::size_t{return static_cast<Impl*>(p)->roundPoints.size();},
            [](std::size_t i,void* p)->sfVector2f{return static_cast<Impl*>(p)->roundPoints[i];},this);
        if(!rectangle||!text||!sprite||!view||!child||!rounded)throw std::runtime_error("Cannot create interface renderer");
    }
    ~Impl(){
        if(sprite)sfSprite_destroy(sprite);
        if(text)sfText_destroy(text);
        if(rectangle)sfRectangleShape_destroy(rectangle);
        if(view)sfView_destroy(view);
        if(child)sfView_destroy(child);
        if(rounded)sfShape_destroy(rounded);
        if(main)sfFont_destroy(main);
        if(upper)sfFont_destroy(upper);
        if(regular)sfFont_destroy(regular);
        if(medium)sfFont_destroy(medium);
    }
    bool Native()const{return theme!=ThemeId::Retronix;}
    const Palette& Colors()const{return Theme(theme).colors;}
    float Scale()const{return Native()?context.scale:1;}
    unsigned Pixels(unsigned size)const{return std::max(1u,unsigned(::Rounded(float(size)*Scale())));}
    float Snap(float value)const{return ::Rounded(value*Scale())/Scale();}
    bool Covered(sfUint32 point)const {
        const auto found=std::upper_bound(coverage.begin(),coverage.end(),point,
            [](sfUint32 p,const std::pair<sfUint32,sfUint32>& range){return p<range.first;});
        return found!=coverage.begin()&&point<=(found-1)->second;
    }
    const sfFont* Font(sfUint32 point)const {
        if(!main)throw std::runtime_error("Interface font is not loaded");
        if(Native()&&regular&&Covered(point))return emphasis?medium:regular;
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
        return sfFont_getGlyph(Font(point),point,Pixels(size),sfFalse,0).advance/Scale();
    }
    float Width(const std::vector<sfUint32>& line,unsigned size)const {
        float width=0;const sfFont* previousFont=nullptr;sfUint32 previous=0;
        for(auto point:line) {
            const auto* font=Font(point);
            if(font==previousFont)width+=sfFont_getKerning(font,previous,point,Pixels(size))/Scale();
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
                if(font==previousFont)width+=sfFont_getKerning(font,previous,point,Pixels(size))/Scale();
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
            const auto pixels=Pixels(size);sfText_setFont(text,currentFont);sfText_setCharacterSize(text,pixels);sfText_setUnicodeString(text,run.data());
            sfText_setScale(text,{1/Scale(),1/Scale()});
            const auto top=Native()?float(pixels)+sfFont_getGlyph(emphasis?medium:regular,'H',pixels,sfFalse,0).bounds.top:2.f;
            sfText_setFillColor(text,color);sfText_setPosition(text,{Snap(cursor),Snap(y)-top/Scale()});
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
    sfFloatRect Display(const Item& item)const {
        auto area=item.area;const auto found=panels.find(item.panel);
        if(found!=panels.end())area.top-=found->second.drawOffset;
        return area;
    }
    bool Hit(const Item& item)const {
        const auto found=panels.find(item.panel);
        if(found!=panels.end()&&!Inside(found->second.viewport,pointer.x,pointer.y))return false;
        return Inside(Display(item),pointer.x,pointer.y);
    }
    void ScrollView(const Scroll& scroll) {
        auto area=scroll.viewport;area.top+=scroll.drawOffset;
        sfView_reset(child,area);
        const auto root=sfView_getViewport(view);
        sfView_setViewport(child,{root.left+scroll.viewport.left/context.width*root.width,
            root.top+scroll.viewport.top/context.height*root.height,
            scroll.viewport.width/context.width*root.width,scroll.viewport.height/context.height*root.height});
        sfRenderWindow_setView(window,child);
    }
    void Reveal(const Item& item) {
        const auto found=panels.find(item.panel);if(found==panels.end())return;
        auto& scroll=found->second;const float margin=4;
        if(item.area.top<scroll.viewport.top+scroll.offset+margin)scroll.offset=item.area.top-scroll.viewport.top-margin;
        else if(item.area.top+item.area.height>scroll.viewport.top+scroll.offset+scroll.viewport.height-margin)
            scroll.offset=item.area.top+item.area.height-scroll.viewport.top-scroll.viewport.height+margin;
        scroll.offset=std::max(0.f,std::min(scroll.offset,std::max(0.f,scroll.height-scroll.viewport.height)));
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
void Ui::Canvas::LoadSmoothFonts(const std::string& regularPath,const std::string& mediumPath,const std::string& coveragePath) {
    auto regularBytes=Storage::ReadBytes(regularPath,8*1024*1024);
    auto mediumBytes=Storage::ReadBytes(mediumPath,8*1024*1024);
    auto coverage=Coverage(Storage::ReadBytes(coveragePath,256*1024),regularBytes,mediumBytes);
    std::unique_ptr<sfFont,decltype(&sfFont_destroy)> regular(sfFont_createFromMemory(regularBytes.data(),regularBytes.size()),sfFont_destroy);
    std::unique_ptr<sfFont,decltype(&sfFont_destroy)> medium(sfFont_createFromMemory(mediumBytes.data(),mediumBytes.size()),sfFont_destroy);
    if(!regular||!medium)throw std::runtime_error("Cannot load the smooth interface fonts");
    sfFont_setSmooth(regular.get(),sfTrue);sfFont_setSmooth(medium.get(),sfTrue);
    if(impl_->regular)sfFont_destroy(impl_->regular);
    if(impl_->medium)sfFont_destroy(impl_->medium);
    impl_->regularBytes=std::move(regularBytes);impl_->mediumBytes=std::move(mediumBytes);
    impl_->regular=regular.release();impl_->medium=medium.release();impl_->coverage=std::move(coverage);
}
void Ui::Canvas::Configure(ThemeId theme,unsigned uiScale) {
    Theme(theme);
    if(uiScale<50||uiScale>400)throw std::invalid_argument("Interface scale must be between 50 and 400 percent");
    if(theme!=ThemeId::Retronix&&(!impl_->regular||!impl_->medium||!impl_->main||!impl_->upper))
        throw std::runtime_error("Smooth interface fonts are unavailable; choose Retronix or repair the installed fonts");
    if(impl_->theme!=theme||impl_->uiScale!=uiScale)impl_->revealFocus=true;
    impl_->theme=theme;impl_->uiScale=uiScale;
    impl_->context=MakeLayout(theme,impl_->window?sfRenderWindow_getSize(impl_->window):sfVector2u{640,360},uiScale);
}
const Ui::LayoutContext& Ui::Canvas::Context()const{return impl_->context;}
const Ui::Palette& Ui::Canvas::Colors()const{return impl_->Colors();}
void Ui::Canvas::Begin(sfRenderWindow* window,const Input::UiActions& input,const Pointer& pointer) {
    if(!window)throw std::invalid_argument("Interface window is missing");
    impl_->window=window;impl_->input=input;impl_->pointer=pointer;impl_->items.clear();impl_->panel.clear();impl_->wheelUsed=false;impl_->emphasis=false;
    const auto pixels=sfRenderWindow_getSize(window);
    if(pixels.x!=impl_->context.pixels.x||pixels.y!=impl_->context.pixels.y)impl_->revealFocus=true;
    impl_->context=MakeLayout(impl_->theme,pixels,impl_->uiScale);
    const float width=float(std::max(1u,pixels.x)),height=float(std::max(1u,pixels.y));
    sfView_reset(impl_->view,{0,0,impl_->context.width,impl_->context.height});
    if(impl_->Native())sfView_setViewport(impl_->view,{0,0,1,1});
    else {const auto scale=std::min(width/640.f,height/360.f);
        sfView_setViewport(impl_->view,{(width-640*scale)/(2*width),(height-360*scale)/(2*height),640*scale/width,360*scale/height});}
    sfRenderWindow_setView(window,impl_->view);
}
void Ui::Canvas::BeginScroll(const std::string& id,sfFloatRect viewport,float contentHeight) {
    if(!impl_->window||!impl_->panel.empty())throw std::logic_error("Scroll panels must be drawn without nesting");
    if(id.empty()||!Finite(viewport)||viewport.width<=0||viewport.height<=0||!std::isfinite(contentHeight)||contentHeight<0)
        throw std::invalid_argument("Invalid scroll panel geometry");
    if(viewport.left<0||viewport.top<0||viewport.left+viewport.width>impl_->context.width+0.01f||viewport.top+viewport.height>impl_->context.height+0.01f)
        throw std::invalid_argument("Scroll panel must fit inside the interface canvas");
    auto& scroll=impl_->panels[id];
    if(scroll.viewport.left!=viewport.left||scroll.viewport.top!=viewport.top||scroll.viewport.width!=viewport.width||scroll.viewport.height!=viewport.height)
        impl_->revealFocus=true;
    scroll.viewport=viewport;scroll.height=std::max(contentHeight,viewport.height);
    if(!impl_->wheelUsed&&impl_->pointer.wheel&&Inside(viewport,impl_->pointer.x,impl_->pointer.y)){
        scroll.offset-=impl_->pointer.wheel*(impl_->Native()?48.f:24.f);impl_->wheelUsed=true;}
    scroll.offset=std::max(0.f,std::min(scroll.offset,scroll.height-scroll.viewport.height));scroll.drawOffset=scroll.offset;
    impl_->panel=id;impl_->ScrollView(scroll);
}
void Ui::Canvas::EndScroll() {
    if(impl_->panel.empty())throw std::logic_error("No scroll panel is active");
    const auto scroll=impl_->panels.at(impl_->panel);impl_->panel.clear();sfRenderWindow_setView(impl_->window,impl_->view);
    if(scroll.height>scroll.viewport.height){
        const auto height=std::max(20.f,scroll.viewport.height*scroll.viewport.height/scroll.height);
        const auto y=scroll.viewport.top+(scroll.viewport.height-height)*scroll.drawOffset/(scroll.height-scroll.viewport.height);
        const float x=scroll.viewport.left+scroll.viewport.width-4;
        Rounded({x,scroll.viewport.top,3,scroll.viewport.height},Fade(Colors().border,80),sfTransparent,0,1.5f);
        Rounded({x,y,3,height},Fade(Colors().muted,160),sfTransparent,0,1.5f);
    }
}
void Ui::Canvas::Rect(sfFloatRect area,sfColor fill,sfColor outline,float border) {
    if(!impl_->window||area.width<=0||area.height<=0)return;
    sfRectangleShape_setPosition(impl_->rectangle,{impl_->Snap(area.left),impl_->Snap(area.top)});
    sfRectangleShape_setSize(impl_->rectangle,{impl_->Snap(area.width),impl_->Snap(area.height)});
    sfRectangleShape_setRotation(impl_->rectangle,0);
    sfRectangleShape_setFillColor(impl_->rectangle,fill);sfRectangleShape_setOutlineColor(impl_->rectangle,outline);
    sfRectangleShape_setOutlineThickness(impl_->rectangle,outline.a?border:0);
    sfRenderWindow_drawRectangleShape(impl_->window,impl_->rectangle,nullptr);
}
void Ui::Canvas::Rounded(sfFloatRect area,sfColor fill,sfColor outline,float border,float radius) {
    if(!impl_->Native()){Rect(area,fill,outline,border);return;}
    if(!impl_->window||area.width<=0||area.height<=0)return;
    if(radius<0)radius=Theme(impl_->theme).radius;
    radius=std::max(0.f,std::min(radius,std::min(area.width,area.height)/2));
    if(radius<0.5f){Rect(area,fill,outline,border);return;}
    impl_->roundPoints.clear();const unsigned segments=8;
    const float pi=3.14159265358979323846f;
    const sfVector2f centers[]={{area.width-radius,radius},{area.width-radius,area.height-radius},
        {radius,area.height-radius},{radius,radius}};
    for(unsigned corner=0;corner<4;++corner)for(unsigned i=0;i<=segments;++i){
        const auto angle=(-0.5f+float(corner)*0.5f+float(i)*0.5f/segments)*pi;
        impl_->roundPoints.push_back({centers[corner].x+std::cos(angle)*radius,centers[corner].y+std::sin(angle)*radius});}
    sfShape_update(impl_->rounded);sfShape_setPosition(impl_->rounded,{impl_->Snap(area.left),impl_->Snap(area.top)});
    sfShape_setFillColor(impl_->rounded,fill);sfShape_setOutlineColor(impl_->rounded,outline);
    sfShape_setOutlineThickness(impl_->rounded,outline.a?border:0);
    sfRenderWindow_drawShape(impl_->window,impl_->rounded,nullptr);
}
void Ui::Canvas::Wordmark(sfFloatRect area) {
    if(area.width<=0||area.height<=0)return;
    Weight weight(impl_->emphasis);const auto& colors=Colors();
    unsigned size=std::max(1u,unsigned(std::floor(area.height*0.8f)));
    const std::string pieces[]={"Driv","On","ix"};const sfColor inks[]={colors.red,colors.green,colors.blue};
    while(size>1&&impl_->Width(Decode("DrivOnix"),size)>area.width)--size;
    float x=area.left;for(unsigned i=0;i<3;++i){Text(pieces[i],x,area.top+(area.height-size)/2,size,inks[i]);x+=impl_->Width(Decode(pieces[i]),size);}
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
    sfSprite_setPosition(impl_->sprite,{impl_->Snap(area.left+(area.width-width)/2),impl_->Snap(area.top+(area.height-height)/2)});
    sfRenderWindow_drawSprite(impl_->window,impl_->sprite,nullptr);
}
void Ui::Canvas::Button(const std::string& id,const std::string& label,sfFloatRect area,bool enabled,Tone tone,unsigned textSize) {
    if(id.empty())throw std::invalid_argument("Interface action needs an identity");
    for(const auto& item:impl_->items)if(item.id==id)throw std::logic_error("Duplicate interface action: "+id);
    impl_->items.push_back({id,area,enabled,tone,impl_->panel});
    const bool focused=enabled&&id==impl_->focus;
    const auto& colors=Colors();const auto accent=Accent(tone,colors);
    if(impl_->Native()){
        const bool hover=enabled&&impl_->Hit(impl_->items.back());
        const auto fill=!enabled?Fade(colors.input,210):focused||hover?colors.hover:tone==Tone::Quiet?colors.input:colors.panel;
        Rounded({area.left,area.top+2,area.width,area.height},sfColor{0,0,0,35},sfTransparent,0);
        Rounded(area,fill,enabled?(focused?Fade(accent,180):colors.border):Fade(colors.border,100));
        if(tone==Tone::Accent&&enabled)Rounded({area.left+1,area.top+1,area.width-2,area.height-2},Fade(accent,focused?20:9),sfTransparent,0);
        if(!label.empty()){
            Weight weight(impl_->emphasis);const auto width=impl_->Width(Decode(label),textSize);
            Text(label,area.left+std::max(10.f,(area.width-width)/2),area.top+(area.height-textSize)/2,textSize,
                !enabled?Fade(colors.muted,125):tone==Tone::Danger?colors.red:colors.text,std::max(0.f,area.width-20));
        }
        return;
    }
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
    if(impl_->Native()){
        const auto& colors=Colors();const float padding=impl_->theme==ThemeId::Modern?12.f:9.f;
        const sfFloatRect art={area.left+padding,area.top+padding,area.width-2*padding,std::max(1.f,area.height-76-padding)};
        Rounded(art,colors.input,sfTransparent,0,std::max(2.f,Theme(impl_->theme).radius-3));
        if(image)Image(image,{art.left+3,art.top+3,art.width-6,art.height-6});
        else {
            const float markWidth=std::min(art.width-18,116.f);
            if(art.height<75){const auto markHeight=std::max(1.f,std::min(24.f,art.height-8));
                Wordmark({art.left+(art.width-markWidth)/2,art.top+(art.height-markHeight)/2,markWidth,markHeight});}
            else {
                Wordmark({art.left+(art.width-markWidth)/2,art.top+art.height/2-20,markWidth,24});
                Text(available?"Artwork unavailable":"Copy unavailable",art.left+10,art.top+art.height/2+16,11,colors.muted,std::max(0.f,art.width-20));
            }
        }
        {Weight weight(impl_->emphasis);Wrap(label,{area.left+padding,art.top+art.height+12,area.width-2*padding,38},15,available?colors.text:colors.muted);}
        Text(available?"MEGA DRIVE":"UNAVAILABLE",area.left+padding,area.top+area.height-20,10,colors.muted,area.width-2*padding);
        if(favorite){Rounded({area.left+area.width-36,area.top+padding+3,24,24},Fade(colors.background,235),sfTransparent,0,12);
            Text(u8"★",area.left+area.width-31,area.top+padding+8,14,colors.green);}
        return;
    }
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
    if(!impl_->panel.empty())throw std::logic_error("Close the scroll panel before finishing the interface");
    auto find=[&](const std::string& id)->const Impl::Item* {
        for(const auto& item:impl_->items)if(item.enabled&&item.id==id)return &item;
        return nullptr;
    };
    const Impl::Item* hovered=nullptr;
    if(impl_->pointer.moved||impl_->pointer.clicked)
        for(auto item=impl_->items.rbegin();item!=impl_->items.rend();++item)
            if(item->enabled&&impl_->Hit(*item)){hovered=&*item;break;}
    if(hovered)impl_->focus=hovered->id;
    const auto* current=find(impl_->focus);
    if(!current)for(const auto& item:impl_->items)if(item.enabled){impl_->focus=item.id;current=&item;break;}
    const int dx=int(impl_->input.right)-int(impl_->input.left),dy=int(impl_->input.down)-int(impl_->input.up);
    if(current&&(dx||dy)) {
        const auto currentArea=impl_->Display(*current);
        const auto x=currentArea.left+currentArea.width/2,y=currentArea.top+currentArea.height/2;
        const Impl::Item* next=nullptr;float best=std::numeric_limits<float>::max();
        bool withinPanel=false;
        if(dy&&!current->panel.empty())for(const auto& item:impl_->items){
            if(!item.enabled||item.id==current->id||item.panel!=current->panel)continue;
            const auto candidate=impl_->Display(item);
            if((candidate.top+candidate.height/2-y)*dy>0.5f){withinPanel=true;break;}
        }
        for(const auto& item:impl_->items) {
            if(!item.enabled||item.id==current->id)continue;
            if(withinPanel&&item.panel!=current->panel)continue;
            const auto candidate=impl_->Display(item);
            const float distanceX=candidate.left+candidate.width/2-x,distanceY=candidate.top+candidate.height/2-y;
            const float forward=dy?distanceY*dy:distanceX*dx,sideways=dy?std::abs(distanceX):std::abs(distanceY);
            if(forward<=0.5f)continue;
            const auto score=forward+sideways*4;
            if(score<best){best=score;next=&item;}
        }
        if(next){impl_->focus=next->id;current=next;}
    }
    if(current) {
        if(impl_->focus!=impl_->lastFocus||impl_->revealFocus)impl_->Reveal(*current);
        const auto found=impl_->panels.find(current->panel);if(found!=impl_->panels.end())impl_->ScrollView(found->second);
        const auto area=current->area;const auto accent=Accent(current->tone,Colors());
        if(impl_->Native())Rounded({area.left-2,area.top-2,area.width+4,area.height+4},sfTransparent,accent,2,Theme(impl_->theme).radius+2);
        else {
            Rect({area.left-2,area.top-2,area.width+4,area.height+4},sfTransparent,accent,1);
            Rect({area.left-3,area.top-3,5,2},accent);Rect({area.left-3,area.top-3,2,5},accent);
            Rect({area.left+area.width-2,area.top+area.height+1,5,2},accent);
            Rect({area.left+area.width+1,area.top+area.height-2,2,5},accent);
        }
        sfRenderWindow_setView(impl_->window,impl_->view);
    }
    impl_->lastFocus=impl_->focus;impl_->revealFocus=false;
    if(impl_->pointer.clicked&&hovered)return hovered->id;
    if(impl_->input.confirm&&current)return current->id;
    return {};
}
const std::string& Ui::Canvas::Focus()const{return impl_->focus;}
std::vector<Ui::ControlInfo> Ui::Canvas::Controls()const {
    std::vector<ControlInfo> result;result.reserve(impl_->items.size());
    for(const auto& item:impl_->items){const auto panel=impl_->panels.find(item.panel);
        result.push_back({item.id,impl_->Display(item),item.enabled,item.panel,
            panel==impl_->panels.end()?sfFloatRect{0,0,impl_->context.width,impl_->context.height}:panel->second.viewport});}
    return result;
}
void Ui::Canvas::Focus(const std::string& id){impl_->focus=id;impl_->revealFocus=true;}
void Ui::Canvas::ClearFocus(){impl_->focus.clear();impl_->revealFocus=false;}
sfVector2f Ui::Canvas::ToLogical(sfVector2i pixels)const {
    if(!impl_->window)return {-100,-100};
    const auto size=sfRenderWindow_getSize(impl_->window);
    if(!size.x||!size.y)return {-100,-100};
    if(impl_->Native())return sfRenderWindow_mapPixelToCoords(impl_->window,pixels,impl_->view);
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
