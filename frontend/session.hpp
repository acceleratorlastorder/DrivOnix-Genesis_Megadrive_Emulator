#ifndef DRIVONIX_FRONTEND_SESSION_HPP
#define DRIVONIX_FRONTEND_SESSION_HPP
#include "../library/rominfo.hpp"
#include "../genesis/cartridge/cartridge.hpp"
#include "../audio/audio.hpp"
#include "../platform/controller.hpp"
#include <memory>
#include <string>

namespace Frontend {
struct PreparedGame {
    RomInfo::Prepared rom;
    Cartridge::Prepared cartridge,bios;
    std::shared_ptr<Audio::PreparedState> audio,biosAudio;
    std::string path,member;
    bool showBios=false;
};
class Session {
public:
    explicit Session(bool hostAudio=true):hostAudio_(hostAudio){}
    static PreparedGame Prepare(const std::string& path,const std::string& member,bool showBios,
                                const std::string& biosPath,const Process::CancelToken& cancel={});
    void Start(PreparedGame prepared);
    void Pause();
    void Resume();
    void Restart();
    void Stop();
    void Step(const Controller::Pads& buttons);
    void SkipBios();
    bool Active() const {return active_;}
    bool Paused() const {return paused_;}
    bool Booting() const {return bios_;}
    const RomInfo::Prepared& Rom() const {return current_.rom;}
    const std::string& Path() const {return current_.path;}
    const std::string& Member() const {return current_.member;}
    double FrameSeconds() const;
    uint64_t ActiveFrames() const {return activeFrames_;}
private:
    void CommitGame();
    void ResetMachine(Cartridge::Prepared cartridge,std::shared_ptr<Audio::PreparedState> audio);
    PreparedGame current_;
    bool active_=false,paused_=true,bios_=false,hostAudio_=true;
    uint64_t activeFrames_=0;
};
}
#endif
