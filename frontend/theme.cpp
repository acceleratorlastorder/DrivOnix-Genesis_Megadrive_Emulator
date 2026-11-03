#include "theme.hpp"
#include <stdexcept>

namespace {
const Ui::ThemeSpec retronix={Ui::ThemeId::Retronix,"retronix","Retronix",
    "Original pixel interface with the classic RGB identity.",
    {{6,8,15,255},{15,21,34,255},{49,65,91,255},{233,239,250,255},{148,165,189,255},
     {89,255,116,255},{255,65,84,255},{88,119,255,255},{25,36,51,255},{9,13,22,255}},0};
const Ui::ThemeSpec hd={Ui::ThemeId::RetronixHd,"retronix_hd","Retronix HD",
    "Crisp proportional text, comfortable spacing and the familiar RGB colors.",
    {{9,14,24,255},{18,27,42,255},{49,66,91,255},{236,242,252,255},{157,176,201,255},
     {91,233,132,255},{255,100,116,255},{111,147,255,255},{29,45,65,255},{12,20,33,255}},6};
const Ui::ThemeSpec modern={Ui::ThemeId::Modern,"modern","Modern",
    "A spacious dark library with smooth typography and a restrained blue accent.",
    {{11,16,25,255},{21,29,43,255},{48,62,82,255},{239,244,252,255},{159,174,197,255},
     {93,211,164,255},{247,110,124,255},{102,166,255,255},{34,49,69,255},{15,23,36,255}},10};
}
const Ui::ThemeSpec& Ui::Theme(ThemeId id) {
    switch(id){case ThemeId::Retronix:return retronix;case ThemeId::RetronixHd:return hd;case ThemeId::Modern:return modern;}
    throw std::invalid_argument("Unknown interface theme");
}
Ui::ThemeId Ui::ParseTheme(const std::string& key) {
    if(key==retronix.key)return ThemeId::Retronix;
    if(key==hd.key)return ThemeId::RetronixHd;
    if(key==modern.key)return ThemeId::Modern;
    throw std::invalid_argument("Unknown interface theme: "+key);
}
