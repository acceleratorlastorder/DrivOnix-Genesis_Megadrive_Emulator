#include "../../cathoderaytube/crt.hpp"

#include <iostream>
#include <memory>
#include <stdexcept>

namespace {
void Need(bool condition, const char* why) {
    if (!condition) throw std::runtime_error(why);
}
struct DisplayCleanup {
    ~DisplayCleanup() { CRT::Shutdown(); }
};
}

int main() {
    DisplayCleanup cleanup;
    try {
        Settings::Values settings;
        settings.scale = 2;
        settings.fullscreen = false;
        CRT::Init(settings);
        CRT::SetResolution(320, 480);

        // Reapplying preferences must recompute the layout even when the new
        // sprite's default texture rectangle already matches the current mode.
        CRT::Init(settings);
        Need(CRT::Height() == 480, "Reapplying settings changed the framebuffer mode");
        auto* window = CRT::GetWindow();
        Need(window != nullptr, "Reapplying settings lost the display window");
        const auto size = sfRenderWindow_getSize(window);
        Need(size.x == 640 && size.y == 480, "Interlace settings produced the wrong window size");

        for (int y = 0; y < 480; ++y)
            for (int x = 0; x < 320; ++x)
                CRT::Write(x, y, y < 240 ? 255 : 0, y < 240 ? 0 : 255, 0);
        // Populate both swap buffers before capturing the real rendered image.
        CRT::Render();
        CRT::Render();
        std::unique_ptr<sfTexture, decltype(&sfTexture_destroy)> capture(
            sfTexture_create(size.x, size.y), sfTexture_destroy);
        Need(bool(capture), "Cannot create display capture texture");
        sfTexture_updateFromRenderWindow(capture.get(), window, 0, 0);
        std::unique_ptr<sfImage, decltype(&sfImage_destroy)> image(
            sfTexture_copyToImage(capture.get()), sfImage_destroy);
        Need(bool(image), "Cannot capture the rendered display");
        const auto top = sfImage_getPixel(image.get(), 100, 80);
        const auto bottom = sfImage_getPixel(image.get(), 100, 400);
        Need(top.r > 240 && top.g < 15 && top.b < 15,
             "Reapplied interlace display lost its red upper half");
        Need(bottom.g > 240 && bottom.r < 15 && bottom.b < 15,
             "Reapplied interlace display cropped its green lower half");
        std::cout << "Display settings reapply passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "Display settings test: " << error.what() << '\n';
        return 1;
    }
}
