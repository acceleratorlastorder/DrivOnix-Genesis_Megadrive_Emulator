#include "theme.hpp"
#include <algorithm>
#include <cmath>
namespace Ui {
LayoutContext MakeLayout(ThemeId theme,sfVector2u pixels,unsigned uiScale){
    LayoutContext result;result.theme=theme;result.pixels=pixels;result.requestedScale=uiScale;
    const float width=float(std::max(1u,pixels.x)),height=float(std::max(1u,pixels.y));
    if(theme==ThemeId::Retronix){result.scale=std::min(width/640.f,height/360.f);return result;}
    // Keep a usable logical minimum in small windows. The requested preference
    // remains intact and takes effect as space becomes available on resize.
    result.scale=std::max(0.01f,std::min(float(uiScale)/100.f,std::min(width/640.f,height/360.f)));
    result.width=width/result.scale;result.height=height/result.scale;result.compact=result.width<960;
    return result;
}
sfFloatRect PageLayout::Card(std::size_t index)const {
    if(!columns)return {0,0,0,0};
    return {grid.left+float(index%columns)*(cardWidth+gap),grid.top+float(index/columns)*(cardHeight+rowGap),cardWidth,cardHeight};
}
PageLayout LibraryLayout(const LayoutContext& c){
    PageLayout p;
    if(c.theme==ThemeId::Retronix){p.grid={14,79,611,230};p.columns=6;p.rows=2;p.capacity=12;p.cardWidth=96;p.cardHeight=112;p.gap=7;p.rowGap=6;return p;}
    const bool sidebar=c.theme==ThemeId::Modern&&!c.compact;
    const float left=sidebar?232.f:24.f;
    const float top=c.theme==ThemeId::Modern?(c.compact?178.f:132.f):156.f;
    p.grid={left,top,std::max(1.f,c.width-left-24),std::max(1.f,c.height-top-58)};
    p.gap=c.theme==ThemeId::Modern?18.f:16.f;p.rowGap=p.gap;
    const float minimum=c.theme==ThemeId::Modern?200.f:176.f;
    p.columns=std::max<std::size_t>(1,std::min<std::size_t>(12,std::size_t((p.grid.width+p.gap)/(minimum+p.gap))));
    p.cardWidth=(p.grid.width-float(p.columns-1)*p.gap)/float(p.columns);
    p.cardHeight=std::min(c.theme==ThemeId::Modern?244.f:230.f,p.grid.height);
    p.rows=std::max<std::size_t>(1,std::min<std::size_t>(5,std::size_t((p.grid.height+p.gap)/(p.cardHeight+p.gap))));
    p.capacity=p.columns*p.rows;return p;
}
std::size_t ListCapacity(const LayoutContext& c,ListKind kind){
    if(c.theme==ThemeId::Retronix){switch(kind){
        case ListKind::Variants:case ListKind::Archive:return 9;
        case ListKind::Copies:return 4;case ListKind::Browser:return 10;
        case ListKind::Matches:return 6;case ListKind::Folders:return 5;}}
    const float row=kind==ListKind::Copies?78.f:kind==ListKind::Folders?62.f:40.f;
    return std::max<std::size_t>(1,std::min<std::size_t>(30,std::size_t(std::max(row,c.height-240)/row)));
}
}
