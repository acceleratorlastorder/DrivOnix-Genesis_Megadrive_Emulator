#ifndef DRIVONIX_AUDIO_OUTPUT_HPP
#define DRIVONIX_AUDIO_OUTPUT_HPP
#include <cstdint>
#include <vector>
namespace Settings { struct Values; }
namespace AudioOutput {
void Init(bool enabled=true);
void ApplySettings(const Settings::Values& settings);
void Shutdown();
void Submit(const std::vector<std::int16_t>& samples);
}
#endif
