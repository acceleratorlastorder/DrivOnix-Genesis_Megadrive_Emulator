#include "crt.hpp"
#include "../YM7101/ym7101.hpp"
#ifndef DRIVONIX_HEADLESS
#include "../Platform/platform.hpp"
#include <fstream>
#endif
#include <algorithm>
#include <stdexcept>
CRT::CRT() : width(320), height(224), pixelSize(1)
#ifndef DRIVONIX_HEADLESS
    , window(nullptr), texture(nullptr), sprite(nullptr), fullscreen(false)
#endif
{ pixels.fill(0); }
CRT& CRT::get() { static CRT instance; return instance; }
void CRT::Init() {
#ifndef DRIVONIX_HEADLESS
    auto& s = get();
    std::ifstream config(Platform::DataFile("Config/config.txt"));
    std::string line;
    while (std::getline(config, line)) {
        if (line.compare(0, 11, "ScreenSize=") == 0 && line.size() > 11) {
            s.pixelSize = line[11] >= '1' && line[11] <= '3' ? unsigned(line[11] - '0') : 1;
            break;
        }
    }
    s.fullscreen = s.pixelSize == 3;
    const sfVideoMode mode = s.fullscreen ? sfVideoMode_getDesktopMode() : sfVideoMode{320*s.pixelSize, s.height*s.pixelSize, 32};
    s.window = sfRenderWindow_create(mode, "DrivOnix - Chili Hot Dog Version", s.fullscreen ? sfFullscreen : sfClose, nullptr);
    if (!s.window) throw std::runtime_error("Cannot create emulator window");
    s.texture = sfTexture_create(320, 480);
    s.sprite = sfSprite_create();
    if (!s.texture || !s.sprite) throw std::runtime_error("Cannot create emulator framebuffer");
    sfTexture_setSmooth(s.texture, sfFalse);
    sfSprite_setTexture(s.sprite, s.texture, sfTrue);
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
    if (s.window) sfRenderWindow_destroy(s.window);
    s.sprite = nullptr; s.texture = nullptr; s.window = nullptr;
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
    if (s.sprite) {
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
