#ifndef DRIVONIX_FRONTEND_UI_HPP
#define DRIVONIX_FRONTEND_UI_HPP
#include "input.hpp"
#include "theme.hpp"
#include <SFML/Graphics.h>
#include <memory>
#include <string>
#include <vector>
namespace Ui {
enum class Tone { Normal, Accent, Danger, Quiet };
const sfColor Background={6,8,15,255},Panel={15,21,34,255},Border={49,65,91,255};
const sfColor White={233,239,250,255},Muted={148,165,189,255};
const sfColor Green={89,255,116,255},Red={255,65,84,255},Blue={88,119,255,255};
struct Pointer {float x=-100,y=-100;bool moved=false,clicked=false;float wheel=0;};
struct ControlInfo {
    // area is displayed root-logical geometry. Offscreen items remain enabled
    // for controller navigation; pointer hits must also lie inside clip.
    std::string id;sfFloatRect area;bool enabled;
    // Vertical navigation stays in this group until its actual content end.
    // An empty group denotes a fixed root control; horizontal movement may
    // cross groups. The clipping rectangle itself never scrolls.
    std::string panel;
    sfFloatRect clip{0,0,0,0};
};
class Canvas {
public:
    Canvas();~Canvas();
    Canvas(const Canvas&)=delete;Canvas& operator=(const Canvas&)=delete;
    void LoadFonts(const std::string& mainFont,const std::string& upperFont);
    // New fonts are staged as one transaction; a failure retains prior fonts.
    void LoadSmoothFonts(const std::string& regular,const std::string& medium,const std::string& coveragePath);
    void Configure(ThemeId theme,unsigned uiScale=100);
    const LayoutContext& Context() const;
    const Palette& Colors() const;
    // Retronix retains its 640x360 letterboxed view. Other themes use the
    // native layout dimensions. Pointer coordinates use the active root view.
    void Begin(sfRenderWindow* window,const Input::UiActions& input,const Pointer& pointer={});
    void Wordmark(sfFloatRect area);
    // Children keep root logical coordinates; scrolling adds an offset and
    // clips rendering/hit testing. Nested scroll panels are not supported.
    void BeginScroll(const std::string& id,sfFloatRect viewport,float contentHeight);
    void EndScroll();
    void Rect(sfFloatRect area,sfColor fill,sfColor outline=sfTransparent,float border=1);
    void Rounded(sfFloatRect area,sfColor fill,sfColor outline=sfTransparent,float border=1,float radius=-1);
    void Text(const std::string& text,float x,float y,unsigned size=14,sfColor color=White,float maxWidth=0);
    void Wrap(const std::string& text,sfFloatRect area,unsigned size=14,sfColor color=White);
    void Image(const sfTexture* texture,sfFloatRect area,sfIntRect source={0,0,0,0});
    void Button(const std::string& id,const std::string& label,sfFloatRect area,
                bool enabled=true,Tone tone=Tone::Normal,unsigned textSize=13);
    // A card can supply an image while retaining the button's focus/action area.
    void Card(const std::string& id,const std::string& label,sfFloatRect area,
              const sfTexture* image,bool favorite=false,bool available=true);
    std::string Finish();
    const std::string& Focus() const;
    std::vector<ControlInfo> Controls() const;
    void Focus(const std::string& id);
    void ClearFocus();
    sfVector2f ToLogical(sfVector2i pixels) const;
    static std::vector<sfUint32> Decode(const std::string& utf8);
    static std::string Encode(const std::vector<sfUint32>& text);
private:
    struct Impl;std::unique_ptr<Impl> impl_;
};
}
#endif
