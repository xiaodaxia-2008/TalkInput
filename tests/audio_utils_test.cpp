#include "audio_utils.h"

#include <QFileInfo>
#include <QTemporaryDir>

#include <algorithm>
#include <climits>
#include <string_view>
#include <vector>

#include <catch2/catch_test_macros.hpp>

namespace
{

// A 1 kHz rate makes milliseconds map directly to sample counts.
constexpr int sampleRate = 1000;

void checkSegments(const std::vector<float> &samples, int maxSeconds,
                   const std::vector<int> &expectedSeconds)
{
    const auto segments = talkinput::segmentAudioBySilence(
        samples, sampleRate, maxSeconds, 10, 100, 0.1F);
    REQUIRE(segments.size() == expectedSeconds.size());
    int expectedStart = 0;
    for (size_t i = 0; i < segments.size(); ++i) {
        const int expectedSamples = expectedSeconds[i] * sampleRate;
        INFO("segment " << i << " start=" << segments[i].startSample
                        << " count=" << segments[i].sampleCount);
        REQUIRE(segments[i].startSample == expectedStart);
        REQUIRE(segments[i].sampleCount == expectedSamples);
        expectedStart += expectedSamples;
    }
}

void addSilence(std::vector<float> &samples, int startSecond, int endSecond)
{
    std::fill(samples.begin() + startSecond * sampleRate,
              samples.begin() + endSecond * sampleRate, 0.0F);
}

} // namespace

TEST_CASE("savePcm16ToM4a creates valid file", "[audio_utils]")
{
    QTemporaryDir tempDir;
    REQUIRE(tempDir.isValid());

    const QString path = tempDir.filePath("audio.m4a");
    const QByteArray pcm16(16000 * 2, '\0');
    REQUIRE(talkinput::savePcm16ToM4a(pcm16, 16000, 1, path));
    REQUIRE(QFileInfo::exists(path));
    REQUIRE(QFileInfo(path).size() > 0);
}

TEST_CASE("segmentAudioBySilence", "[audio_utils]")
{
    SECTION("short audio below limit stays single segment")
    {
        const std::vector<float> shortAudio(25 * sampleRate, 1.0F);
        checkSegments(shortAudio, 30, {25});
    }
    SECTION("hard limit splits continuously voiced audio")
    {
        const std::vector<float> noSilence(65 * sampleRate, 1.0F);
        checkSegments(noSilence, 30, {30, 30, 5});
    }
    SECTION("unlimited maxSeconds keeps single segment")
    {
        const std::vector<float> noSilence(65 * sampleRate, 1.0F);
        checkSegments(noSilence, INT_MAX, {65});
    }
    SECTION("longest silence wins within window")
    {
        std::vector<float> pausedAudio(70 * sampleRate, 1.0F);
        addSilence(pausedAudio, 15, 16);
        addSilence(pausedAudio, 25, 27);
        addSilence(pausedAudio, 45, 46);
        addSilence(pausedAudio, 50, 52);
        checkSegments(pausedAudio, 30, {26, 25, 19});
    }
}

TEST_CASE("findSilenceSplits thresholds", "[audio_utils]")
{
    const std::vector<float> tenFrames(10 * 30, 0.0F);
    const std::vector<float> elevenFrames(11 * 30, 0.0F);
    REQUIRE(talkinput::findSilenceSplits(tenFrames, sampleRate, 30, 305, 0.1F)
                .empty());
    REQUIRE(talkinput::findSilenceSplits(elevenFrames, sampleRate, 30, 305, 0.1F)
                .size()
            == 1);
}

TEST_CASE("findBestSilenceSplit returns first valid index", "[audio_utils]")
{
    const std::vector<float> tenFrames(10 * 30, 0.0F);
    REQUIRE(talkinput::findBestSilenceSplit(tenFrames, sampleRate, 10 * 30 + 1,
                                            11 * 30, 30, 300, 0.1F)
            == 0);
}
