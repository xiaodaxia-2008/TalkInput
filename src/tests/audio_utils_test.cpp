#include "audio_utils.h"

#include <algorithm>
#include <climits>
#include <iostream>
#include <string_view>
#include <vector>

namespace
{

// A 1 kHz rate makes milliseconds map directly to sample counts.
constexpr int sampleRate = 1000;

bool expectSegments(std::string_view name, const std::vector<float> &samples,
                    int maxSeconds, const std::vector<int> &expectedSeconds)
{
    const auto segments = talkinput::segmentAudioBySilence(
        samples, sampleRate, maxSeconds, 10, 10, 100, 0.1F);
    if (segments.size() != expectedSeconds.size()) {
        std::cerr << name << ": expected " << expectedSeconds.size()
                  << " segments, got " << segments.size() << '\n';
        return false;
    }

    int expectedStart = 0;
    for (size_t i = 0; i < segments.size(); ++i) {
        const int expectedSamples = expectedSeconds[i] * sampleRate;
        if (segments[i].startSample != expectedStart ||
            segments[i].sampleCount != expectedSamples)
        {
            std::cerr << name << ": unexpected segment " << i << " (start "
                      << segments[i].startSample << ", size "
                      << segments[i].sampleCount << ")\n";
            return false;
        }
        expectedStart += expectedSamples;
    }
    return true;
}

void addSilence(std::vector<float> &samples, int startSecond, int endSecond)
{
    std::fill(samples.begin() + startSecond * sampleRate,
              samples.begin() + endSecond * sampleRate, 0.0F);
}

} // namespace

int main()
{
    bool passed = true;

    const std::vector<float> shortAudio(25 * sampleRate, 1.0F);
    passed &= expectSegments("short audio", shortAudio, 30, {25});

    const std::vector<float> noSilence(65 * sampleRate, 1.0F);
    passed &= expectSegments("hard limit", noSilence, 30, {30, 30, 5});
    passed &= expectSegments("unlimited", noSilence, INT_MAX, {65});

    std::vector<float> pausedAudio(70 * sampleRate, 1.0F);
    addSilence(pausedAudio, 15, 16);
    addSilence(pausedAudio, 25, 27);
    addSilence(pausedAudio, 45, 46);
    addSilence(pausedAudio, 50, 52);
    passed &= expectSegments("longest silence", pausedAudio, 30, {26, 25, 19});

    const std::vector<float> tenFrames(10 * 30, 0.0F);
    const std::vector<float> elevenFrames(11 * 30, 0.0F);
    passed &= talkinput::findSilenceSplits(tenFrames, sampleRate, 30, 305, 0.1F)
                  .empty();
    passed &=
        talkinput::findSilenceSplits(elevenFrames, sampleRate, 30, 305, 0.1F)
            .size() == 1;
    passed &=
        talkinput::findBestSilenceSplit(tenFrames, sampleRate, 10 * 30 + 1,
                                        11 * 30, 30, 300, 0.1F) == 0;

    return passed ? 0 : 1;
}
