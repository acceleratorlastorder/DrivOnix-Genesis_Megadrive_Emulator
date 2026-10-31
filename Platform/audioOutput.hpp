#ifndef DRIVONIX_AUDIO_OUTPUT_HPP
#define DRIVONIX_AUDIO_OUTPUT_HPP
#include <cstdint>
#include <vector>
namespace AudioOutput {
void Init(bool enabled=true);
void Shutdown();
void Submit(const std::vector<std::int16_t>& samples);
}
#endif
