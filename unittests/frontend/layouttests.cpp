#include "../../frontend/theme.hpp"
#include <algorithm>
#include <cmath>
#include <iostream>
#include <stdexcept>

namespace {
void Require(bool value,const char* message){if(!value)throw std::runtime_error(message);}
bool Near(float a,float b){return std::abs(a-b)<0.025f;}
void Legacy(){
    for(auto pixels:{sfVector2u{640,360},sfVector2u{1280,720},sfVector2u{1920,1080},sfVector2u{1024,768},sfVector2u{3840,2160}})
        for(unsigned scale:{75u,100u,150u,200u}){
            const auto context=Ui::MakeLayout(Ui::ThemeId::Retronix,pixels,scale);
            Require(Near(context.width,640)&&Near(context.height,360),"Retronix logical view changed");
            Require(Near(context.scale,std::min(float(pixels.x)/640,float(pixels.y)/360)),"Retronix letterbox scale changed");
            const auto page=Ui::LibraryLayout(context);Require(page.columns==6&&page.rows==2&&page.capacity==12,"Retronix page capacity changed");
            for(std::size_t index=0;index<12;++index){const auto card=page.Card(index);Require(Near(card.left,14+float(index%6)*103)&&Near(card.top,79+float(index/6)*118)&&Near(card.width,96)&&Near(card.height,112),"Retronix card rectangle changed");}
            Require(Ui::ListCapacity(context,Ui::ListKind::Variants)==9&&Ui::ListCapacity(context,Ui::ListKind::Copies)==4&&Ui::ListCapacity(context,Ui::ListKind::Archive)==9&&Ui::ListCapacity(context,Ui::ListKind::Browser)==10&&Ui::ListCapacity(context,Ui::ListKind::Matches)==6&&Ui::ListCapacity(context,Ui::ListKind::Folders)==5,"Retronix list paging changed");
        }
}
void Native(){
    const sfVector2u sizes[]={{640,360},{800,600},{960,540},{1280,720},{1920,1080},{3840,2160},{7680,4320},{360,640},{320,180}};
    for(auto theme:{Ui::ThemeId::RetronixHd,Ui::ThemeId::Modern})for(auto pixels:sizes)for(unsigned scale:{75u,100u,125u,150u,200u}){
        const auto context=Ui::MakeLayout(theme,pixels,scale);const auto page=Ui::LibraryLayout(context);
        Require(context.width>=639.99f&&context.height>=359.99f&&context.scale>0,"Native logical minimum or scale invalid");
        Require(Near(context.width*context.scale,float(pixels.x))&&Near(context.height*context.scale,float(pixels.y)),"Native logical view does not map to the pixel viewport");
        Require(page.columns>=1&&page.columns<=12&&page.rows>=1&&page.rows<=5&&page.capacity==page.columns*page.rows&&page.capacity<=60,"Native page capacity exceeds query/render bounds");
        Require(page.cardWidth>0&&page.cardHeight>0&&page.gap>=0&&page.rowGap>=0,"Native card geometry invalid");
        Require(page.grid.top+page.grid.height<=context.height-58+0.02f,"Native grid overlaps its pager/footer reservation");
        for(std::size_t index=0;index<page.capacity;++index){const auto card=page.Card(index);Require(card.left>=page.grid.left&&card.top>=page.grid.top&&card.left+card.width<=page.grid.left+page.grid.width+0.02f&&card.top+card.height<=page.grid.top+page.grid.height+0.02f,"Native cover card crosses its grid or pager/footer reservation");
            if(index%page.columns){const auto previous=page.Card(index-1);Require(previous.left+previous.width<=card.left+0.02f,"Native cover columns overlap");}
            if(index>=page.columns){const auto previous=page.Card(index-page.columns);Require(previous.top+previous.height<=card.top+0.02f,"Native cover rows overlap");}}
        if(theme==Ui::ThemeId::Modern&&!context.compact)Require(Near(page.grid.left,232),"Modern desktop grid overlaps its sidebar");
        else Require(Near(page.grid.left,24),"Compact/HD grid lost its outer margin");
        for(auto kind:{Ui::ListKind::Variants,Ui::ListKind::Copies,Ui::ListKind::Archive,Ui::ListKind::Browser,Ui::ListKind::Matches,Ui::ListKind::Folders}){const auto count=Ui::ListCapacity(context,kind);Require(count>=1&&count<=30,"Native list capacity exceeds paging bounds");}
    }
    const auto hd=Ui::LibraryLayout(Ui::MakeLayout(Ui::ThemeId::RetronixHd,{1280,720}));
    const auto modern=Ui::LibraryLayout(Ui::MakeLayout(Ui::ThemeId::Modern,{1280,720}));
    Require(hd.columns==6&&hd.rows==2&&hd.capacity==12&&modern.columns==4&&modern.rows==2&&modern.capacity==8,"Native desktop paging changed unexpectedly");
    Require(modern.grid.left>hd.grid.left&&modern.grid.top<hd.grid.top,"Native themes did not produce distinct desktop navigation geometry");
    const auto compact=Ui::LibraryLayout(Ui::MakeLayout(Ui::ThemeId::Modern,{640,360}));
    Require(compact.grid.top>=170&&compact.grid.width>500,"Modern compact navigation does not leave a usable body");
    const auto compactHd=Ui::LibraryLayout(Ui::MakeLayout(Ui::ThemeId::RetronixHd,{640,360}));
    Require(compactHd.columns==3&&compactHd.capacity==3&&compact.columns==2&&compact.capacity==2,"Native minimum-window paging changed unexpectedly");
}
}
int main(){try{Legacy();Native();Require(Ui::ParseTheme("retronix")==Ui::ThemeId::Retronix&&Ui::ParseTheme("retronix_hd")==Ui::ThemeId::RetronixHd&&Ui::ParseTheme("modern")==Ui::ThemeId::Modern,"Theme keys changed");bool rejected=false;try{Ui::ParseTheme("unknown");}catch(const std::invalid_argument&){rejected=true;}Require(rejected,"Unknown stored theme accepted");std::cout<<"Legacy exact layout and native HD/Modern card/list geometry across small, portrait and UHD scales passed\n";return 0;}catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}}
