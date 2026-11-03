#ifndef DRIVONIX_THEME_HPP
#define DRIVONIX_THEME_HPP
#include <SFML/Graphics.h>
#include <cstddef>
#include <string>
namespace Ui {
enum class ThemeId { Retronix, RetronixHd, Modern };
struct Palette {sfColor background,panel,border,text,muted,green,red,blue,hover,input;};
struct ThemeSpec {ThemeId id;const char* key;const char* name;const char* description;Palette colors;float radius;};
const ThemeSpec& Theme(ThemeId id);
ThemeId ParseTheme(const std::string& key);
struct LayoutContext {
    ThemeId theme=ThemeId::Retronix;
    sfVector2u pixels{640,360};
    float width=640,height=360,scale=1;
    unsigned requestedScale=100;
    bool compact=false;
};
LayoutContext MakeLayout(ThemeId theme,sfVector2u pixels,unsigned uiScale=100);
struct PageLayout {
    sfFloatRect grid{0,0,0,0};
    std::size_t columns=1,rows=1,capacity=1;
    float cardWidth=0,cardHeight=0,gap=0,rowGap=0;
    sfFloatRect Card(std::size_t index) const;
};
enum class ListKind { Variants,Copies,Archive,Browser,Matches,Folders };
PageLayout LibraryLayout(const LayoutContext& context);
std::size_t ListCapacity(const LayoutContext& context,ListKind kind);
}
#endif
