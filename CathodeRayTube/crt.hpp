#ifndef CRT_HPP
#define CRT_HPP
#include <array>
#include "../Bits/bitsUtils.hpp"
#ifndef DRIVONIX_HEADLESS
#include <SFML/Graphics.h>
#else
struct sfRenderWindow;
#endif
class CRT {
public:
    CRT();
    static void Init();
    static void Shutdown();
    static void Render();
    static void ResetScreen();
    static void SetResolution(unsigned width, unsigned height);
    static const byte* Pixels();
    static unsigned Height();
    static void Write(int x, int y, byte red, byte green, byte blue);
    static void ApplyIntensity(int x, int y, int intensity);
    static sfRenderWindow* GetWindow();
private:
    static CRT& get();
    std::array<byte, 320 * 480 * 4> pixels;
    unsigned width, height, pixelSize;
#ifndef DRIVONIX_HEADLESS
    sfRenderWindow* window;
    sfTexture* texture;
    sfSprite* sprite;
    bool fullscreen;
#endif
};
#endif
