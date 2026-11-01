#include "audiooutput.hpp"
#include "settings.hpp"
#include <SFML/Audio.h>
#include <algorithm>
#include <array>
#include <deque>
#include <mutex>
namespace {
struct Output {
    sfSoundStream* stream=nullptr;
    std::mutex mutex;
    std::deque<sfInt16> queue;
    std::array<sfInt16,2048> chunk;
};
Output& Get() { static Output state; return state; }
sfBool Fetch(sfSoundStreamChunk* chunk,void* user) {
    auto& s=*static_cast<Output*>(user);
    std::lock_guard<std::mutex> lock(s.mutex);
    for (auto& sample:s.chunk) {
        sample=s.queue.empty()?0:s.queue.front();
        if (!s.queue.empty()) s.queue.pop_front();
    }
    chunk->samples=s.chunk.data();chunk->sampleCount=s.chunk.size();
    return sfTrue;
}
void Seek(sfTime,void*) {}
}
void AudioOutput::Init(bool enabled) {
    Shutdown();
    if (!enabled) return;
    auto& s=Get();
    s.stream=sfSoundStream_create(Fetch,Seek,2,48000,&s);
    if (s.stream) sfSoundStream_play(s.stream);
}
void AudioOutput::ApplySettings(const Settings::Values& settings) {
    Settings::Validate(settings);
    if (!settings.audioEnabled) { Shutdown(); return; }
    auto& s=Get();
    if (!s.stream) Init(true);
    if (s.stream) sfSoundStream_setVolume(s.stream,static_cast<float>(settings.volume));
}
void AudioOutput::Submit(const std::vector<std::int16_t>& samples) {
    auto& s=Get();
    if (!s.stream) return;
    std::lock_guard<std::mutex> lock(s.mutex);
    s.queue.insert(s.queue.end(),samples.begin(),samples.end());
    while (s.queue.size()>24000) {s.queue.pop_front();s.queue.pop_front();}
}
void AudioOutput::Shutdown() {
    auto& s=Get();
    if (s.stream) {sfSoundStream_destroy(s.stream);s.stream=nullptr;}
    std::lock_guard<std::mutex> lock(s.mutex);
    s.queue.clear();
}
