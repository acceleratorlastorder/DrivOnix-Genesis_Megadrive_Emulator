#include "audio.hpp"
#include "../thirdparty/ymfm/ymfm_opn.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <deque>
#include <limits>
#include <memory>
#include <stdexcept>

namespace
{
// Original Sega PSG implementation. Tone divider ticks at PSG clock / 16;
// the Sega noise generator is a 16-bit shift register with taps 0 and 3.
class Psg
{
public:
    void Write(std::uint8_t value)
    {
        if (value & 0x80) latch = (value >> 4) & 7;
        const unsigned channel = latch >> 1;
        if (latch & 1)
            attenuation[channel] = value & 15;
        else if (channel < 3)
        {
            if (value & 0x80) period[channel] = (period[channel] & 0x3f0) | (value & 15);
            else period[channel] = (period[channel] & 15) | ((value & 63) << 4);
        }
        else
        {
            noiseControl = value & 7;
            shift = 0x8000;
            noiseCounter = 16u << (noiseControl & 3);
            noiseClock = false;
        }
    }

    void Tick()
    {
        bool tone2Rising = false;
        for (unsigned channel = 0; channel < 3; ++channel)
        {
            if (--counter[channel] == 0)
            {
                counter[channel] = period[channel] ? period[channel] : 0x400;
                tone[channel] = !tone[channel];
                if (channel == 2 && tone[channel]) tone2Rising = true;
            }
        }
        bool shiftNoise = false;
        if ((noiseControl & 3) == 3)
            shiftNoise = tone2Rising;
        else if (--noiseCounter == 0)
        {
            noiseCounter = 16u << (noiseControl & 3);
            noiseClock = !noiseClock;
            shiftNoise = noiseClock;
        }
        if (shiftNoise)
        {
            const unsigned feedback = (noiseControl & 4) ? ((shift ^ (shift >> 3)) & 1) : (shift & 1);
            shift = (shift >> 1) | (feedback << 15);
        }
    }

    int Output() const
    {
        // 2 dB attenuation per step; level 15 is mute.
        static const int levels[16] = {2048,1627,1292,1026,815,648,514,408,324,258,205,163,129,103,82,0};
        int result = 0;
        for (unsigned i = 0; i < 3; ++i)
            result += tone[i] ? levels[attenuation[i]] : -levels[attenuation[i]];
        result += (shift & 1) ? levels[attenuation[3]] : -levels[attenuation[3]];
        return result;
    }

private:
    std::array<unsigned, 3> period{{0,0,0}};
    std::array<unsigned, 3> counter{{0x400,0x400,0x400}};
    std::array<bool, 3> tone{{false,false,false}};
    std::array<unsigned, 4> attenuation{{15,15,15,15}};
    unsigned latch = 0;
    unsigned noiseControl = 0;
    unsigned noiseCounter = 16;
    unsigned shift = 0x8000;
    bool noiseClock = false;
};

constexpr std::uint64_t Never = std::numeric_limits<std::uint64_t>::max();

class State : public ymfm::ymfm_interface
{
public:
    State(std::uint32_t clock, std::uint32_t rate) : masterClock(clock), sampleRate(rate), fm(*this)
    {
        fm.reset();
        nextOutput = (masterClock + sampleRate - 1) / sampleRate;
        // Analog coupling removes the chip's DC offset. Preserve the same corner
        // frequency when changing output sample rate.
        dcDecay = std::exp(-2.0 * 3.14159265358979323846 * 20.0 / sampleRate);
    }

    void ymfm_set_timer(std::uint32_t timer, std::int32_t clocks) override
    {
        if (timer < timerEnd.size()) timerEnd[timer] = clocks < 0 ? Never : now + static_cast<std::uint64_t>(clocks) * 7;
    }
    void ymfm_set_busy_end(std::uint32_t clocks) override { busyEnd = now + static_cast<std::uint64_t>(clocks) * 7; }
    bool ymfm_is_busy() override { return now < busyEnd; }
    void ymfm_update_irq(bool asserted) override { irq = asserted; }

    void Advance(std::uint64_t cycles)
    {
        const std::uint64_t target = now + cycles;
        while (now < target)
        {
            const std::uint64_t event = std::min({target, nextFm, nextPsg, nextOutput, timerEnd[0], timerEnd[1]});
            const std::uint64_t elapsed = event - now;
            const int mono = psg.Output();
            for (unsigned channel = 0; channel < 2; ++channel)
                accumulated[channel] += static_cast<double>(lastFm[channel] + mono) * elapsed;
            accumulatedClocks += elapsed;
            now = event;
            // Timers can reschedule themselves in engine_timer_expired().
            for (unsigned timer = 0; timer < 2; ++timer)
                if (timerEnd[timer] == now)
                {
                    timerEnd[timer] = Never;
                    m_engine->engine_timer_expired(timer);
                }
            if (nextPsg == now)
            {
                psg.Tick();
                nextPsg += 15 * 16;
            }
            if (nextFm == now)
            {
                ymfm::ym2612::output_data output;
                fm.generate(&output);
                lastFm[0] = output.data[0];
                lastFm[1] = output.data[1];
                nextFm += 7 * 144;
            }
            if (nextOutput == now)
            {
                for (unsigned channel = 0; channel < 2; ++channel)
                {
                    const double mixed = accumulatedClocks ? accumulated[channel] / accumulatedClocks : 0.0;
                    const double filtered = mixed - previousInput[channel] + dcDecay * previousOutput[channel];
                    previousInput[channel] = mixed;
                    previousOutput[channel] = filtered;
                    samples.push_back(static_cast<std::int16_t>(std::max(-32768.0, std::min(32767.0, filtered))));
                    accumulated[channel] = 0.0;
                }
                accumulatedClocks = 0;
                ++outputFrames;
                nextOutput = ((outputFrames + 1) * masterClock + sampleRate - 1) / sampleRate;
                // A headless/unconsumed producer cannot grow memory without bound.
                while (samples.size() > static_cast<std::size_t>(sampleRate) * 4)
                {
                    samples.pop_front();
                    samples.pop_front();
                }
            }
        }
    }

    const std::uint32_t masterClock;
    const std::uint32_t sampleRate;
    ymfm::ym2612 fm;
    Psg psg;
    std::uint64_t now = 0, busyEnd = 0;
    std::uint64_t nextFm = 7 * 144, nextPsg = 15 * 16, nextOutput = 0;
    std::array<std::uint64_t, 2> timerEnd{{Never,Never}};
    std::array<int, 2> lastFm{{0,0}};
    std::array<double, 2> accumulated{{0,0}}, previousInput{{0,0}}, previousOutput{{0,0}};
    std::uint64_t accumulatedClocks = 0, outputFrames = 0;
    double dcDecay = 0;
    bool irq = false;
    std::deque<std::int16_t> samples;
};

std::unique_ptr<State> state;
State& Get()
{
    if (!state) state.reset(new State(53693175,48000));
    return *state;
}
}

void Audio::Init(std::uint32_t masterClockHz, std::uint32_t outputSampleRate)
{
    if (masterClockHz < 1000000 || outputSampleRate < 8000 || outputSampleRate > 192000)
        throw std::invalid_argument("Invalid audio clock or output sample rate");
    state.reset(new State(masterClockHz, outputSampleRate));
}
void Audio::Advance(std::uint64_t masterCycles) { Get().Advance(masterCycles); }
std::uint8_t Audio::ReadFM(unsigned port)
{
    // The Genesis exposes the YM2612 status on all four mirrored ports.
    (void)port;
    return Get().fm.read_status();
}
void Audio::WriteFM(unsigned port, std::uint8_t value) { Get().fm.write(port & 3, value); }
void Audio::WritePSG(std::uint8_t value) { Get().psg.Write(value); }
std::vector<std::int16_t> Audio::DrainSamples()
{
    auto& source = Get().samples;
    std::vector<std::int16_t> result(source.begin(), source.end());
    source.clear();
    return result;
}
bool Audio::FMInterrupt() { return Get().irq; }
