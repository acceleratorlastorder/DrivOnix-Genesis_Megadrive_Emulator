#include "crt.hpp"
#include "../ym7101/ym7101.hpp"
#include <algorithm>
#include <cmath>
#include <stdexcept>
CRT::CRT() : width(320), height(224), pixelSize(1)
#ifndef DRIVONIX_HEADLESS
    , window(nullptr), texture(nullptr), sprite(nullptr), fullscreen(false)
#endif
{ pixels.fill(0); }
CRT& CRT::get() { static CRT instance; return instance; }
void CRT::Init(const Settings::Values& settings) {
    Settings::Validate(settings);
#ifndef DRIVONIX_HEADLESS
    Shutdown();
    auto& s = get();
    s.pixelSize = settings.scale;
    s.externalWindow=false;
    s.fullscreen = settings.fullscreen;
    const unsigned displayHeight=s.height>240 ? s.height/2:s.height;
    const sfVideoMode mode = s.fullscreen ? sfVideoMode_getDesktopMode() : sfVideoMode{320*s.pixelSize, displayHeight*s.pixelSize, 32};
    s.window = sfRenderWindow_create(mode, "DrivOnix - Chili Hot Dog Version", s.fullscreen ? sfFullscreen : sfClose, nullptr);
    if (!s.window) throw std::runtime_error("Cannot create emulator window");
    s.texture = sfTexture_create(320, 480);
    s.sprite = sfSprite_create();
    if (!s.texture || !s.sprite) throw std::runtime_error("Cannot create emulator framebuffer");
    sfTexture_setSmooth(s.texture, sfFalse);
    sfSprite_setTexture(s.sprite, s.texture, sfTrue);
    // A fresh sprite otherwise has the full 480-line texture rectangle, which
    // can skip interlace/fullscreen layout when reapplying display settings.
    sfSprite_setTextureRect(s.sprite, sfIntRect{0,0,0,0});
    sfSprite_setScale(s.sprite, sfVector2f{float(s.pixelSize), float(s.pixelSize)});
    SetResolution(s.width, s.height);
#endif
    ResetScreen();
}
void CRT::Shutdown() {
#ifndef DRIVONIX_HEADLESS
    auto& s = get();
    if (s.sprite) sfSprite_destroy(s.sprite);
    if (s.texture) sfTexture_destroy(s.texture);
    if (s.window && !s.externalWindow) sfRenderWindow_destroy(s.window);
    s.sprite = nullptr; s.texture = nullptr; s.window = nullptr;
    s.externalWindow=false;
#endif
}
void CRT::SetResolution(unsigned width, unsigned height) {
    auto& s = get();
    if ((width != 256 && width != 320) || (height != 224 && height != 240 && height != 448 && height != 480))
        throw std::runtime_error("Unsupported framebuffer dimensions");
#ifndef DRIVONIX_HEADLESS
    if(s.width==width&&s.height==height&&s.sprite&&sfSprite_getTextureRect(s.sprite).height==int(height)) return;
#else
    if(s.width==width&&s.height==height) return;
#endif
    const bool resize = s.height != height;
    const bool changed = resize || s.width != width;
    const unsigned displayHeight=height>240?height/2:height;
    s.width = width; s.height = height;
    if(changed) ResetScreen();
#ifndef DRIVONIX_HEADLESS
    if (s.sprite && !s.externalWindow) {
        sfTexture_setSmooth(s.texture,height>240?sfTrue:sfFalse);
        sfSprite_setTextureRect(s.sprite, sfIntRect{0, 0, 320, int(height)});
        sfVector2f position{0,0};
        if (s.fullscreen) {
            const auto size = sfRenderWindow_getSize(s.window);
            s.pixelSize=std::max(1u,std::min(size.x/320,size.y/displayHeight));
            sfSprite_setScale(s.sprite,sfVector2f{float(s.pixelSize),float(s.pixelSize)});
            position = sfVector2f{(float(size.x)-320*s.pixelSize)/2, (float(size.y)-displayHeight*s.pixelSize)/2};
        } else if (resize) sfRenderWindow_setSize(s.window, sfVector2u{320*s.pixelSize, displayHeight*s.pixelSize});
        sfSprite_setScale(s.sprite,sfVector2f{float(s.pixelSize),float(s.pixelSize)*(height>240?0.5f:1.0f)});
        const auto viewSize=sfRenderWindow_getSize(s.window);
        sfView* view=sfView_createFromRect(sfFloatRect{0,0,float(viewSize.x),float(viewSize.y)});
        sfRenderWindow_setView(s.window,view);sfView_destroy(view);
        sfSprite_setPosition(s.sprite, position);
    }
#else
    (void)resize; (void)displayHeight;
#endif
}
void CRT::Render() {
#ifndef DRIVONIX_HEADLESS
    auto& s = get();
    if (!s.window) return;
    sfTexture_updateFromPixels(s.texture, s.pixels.data(), 320, s.height, 0, 0);
    sfRenderWindow_clear(s.window, sfBlack);
    sfRenderWindow_drawSprite(s.window, s.sprite, nullptr);
    sfRenderWindow_display(s.window);
#endif
}
void CRT::ResetScreen() {
    auto& p = get().pixels;
    p.fill(0);
    for (unsigned i = 3; i < p.size(); i += 4) p[i] = 255;
}
const byte* CRT::Pixels() { return get().pixels.data(); }
unsigned CRT::Height() { return get().height; }
unsigned CRT::Width() { return get().width; }
#ifndef DRIVONIX_HEADLESS
void CRT::Attach(sfRenderWindow* window) {
    Shutdown();auto& s=get();s.window=window;s.externalWindow=true;
    s.texture=sfTexture_create(320,480);s.sprite=sfSprite_create();
    if(!s.texture||!s.sprite) throw std::runtime_error("Cannot create the game framebuffer");
    sfSprite_setTexture(s.sprite,s.texture,sfTrue);
}
void CRT::Draw(sfFloatRect viewport,bool integerFit,unsigned fixedScale) {
    auto& s=get();if(!s.window||!s.texture||!s.sprite) return;
    const unsigned height=s.height>240?s.height/2:s.height;
    const float available=std::max(1.f,std::floor(std::min(viewport.width/320.f,viewport.height/float(height))));
    const float scale=integerFit?available:std::min(available,float(std::max(1u,fixedScale)));
    sfTexture_updateFromPixels(s.texture,s.pixels.data(),320,s.height,0,0);
    sfTexture_setSmooth(s.texture,s.height>240?sfTrue:sfFalse);
    sfSprite_setTextureRect(s.sprite,{0,0,320,int(s.height)});
    sfSprite_setScale(s.sprite,{scale,scale*(s.height>240?0.5f:1.f)});
    sfSprite_setPosition(s.sprite,{viewport.left+(viewport.width-320*scale)/2,
        viewport.top+(viewport.height-height*scale)/2});
    sfRenderWindow_drawSprite(s.window,s.sprite,nullptr);
}
#endif
void CRT::Write(int x, int y, byte red, byte green, byte blue) {
    auto& s = get();
    if (x < 0 || x >= int(s.width) || y < 0 || y >= int(s.height)) return;
    const unsigned index = (unsigned(y)*320 + unsigned(x) + (320-s.width)/2)*4;
    s.pixels[index] = red; s.pixels[index+1] = green; s.pixels[index+2] = blue;
}
void CRT::ApplyIntensity(int x, int y, int intensity) {
    auto& s = get();
    if (x < 0 || x >= int(s.width) || y < 0 || y >= int(s.height)) return;
    const unsigned index = (unsigned(y)*320 + unsigned(x) + (320-s.width)/2)*4;
    for (unsigned c = 0; c < 3; ++c) {
        auto& value = s.pixels[index+c];
        if (intensity == INTENSITY_SHADOW) value /= 2;
        else if (intensity == INTENSITY_LIGHT) value = byte(128 + value/2);
    }
}
sfRenderWindow* CRT::GetWindow() {
#ifndef DRIVONIX_HEADLESS
    return get().window;
#else
    return nullptr;
#endif
}
