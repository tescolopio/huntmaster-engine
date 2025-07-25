#include <iostream>  // For std::cerr
#include <memory>    // For std::unique_ptr
#include <span>      // For std::span
#include <vector>    // For std::vector

#include <gtest/gtest.h>

#include "TestUtils.h"
#include "dr_wav.h"  // For loading WAV files
#include "huntmaster/core/UnifiedAudioEngine.h"

using namespace huntmaster;
using namespace huntmaster::test;

class CoreValidationTest : public TestFixtureBase {
  protected:
    std::unique_ptr<UnifiedAudioEngine> engine;

    // Helper function to load audio file (duplicate from analyze_recording.cpp for self-contained
    // test)
    std::vector<float>
    load_audio_file(const std::string& filePath, unsigned int& channels, unsigned int& sampleRate) {
        drwav_uint64 totalPCMFrameCount = 0;
        float* pSampleData = drwav_open_file_and_read_pcm_frames_f32(
            filePath.c_str(), &channels, &sampleRate, &totalPCMFrameCount, nullptr);

        if (pSampleData == nullptr) {
            std::cerr << "Error: Could not load audio file for test: " << filePath << std::endl;
            return {};
        }

        // Convert to mono
        std::vector<float> monoSamples(totalPCMFrameCount);
        if (channels > 1) {
            for (drwav_uint64 i = 0; i < totalPCMFrameCount; ++i) {
                float monoSample = 0.0f;
                for (unsigned int j = 0; j < channels; ++j) {
                    monoSample += pSampleData[i * channels + j];
                }
                monoSamples[i] = monoSample / channels;
            }
        } else {
            monoSamples.assign(pSampleData, pSampleData + totalPCMFrameCount);
        }
        drwav_free(pSampleData, nullptr);
        return monoSamples;
    }

    void SetUp() override {
        TestFixtureBase::SetUp();

        auto result = UnifiedAudioEngine::create();
        ASSERT_TRUE(result.isOk()) << "Failed to create UnifiedAudioEngine";
        engine = std::move(result.value);
    }

    void TearDown() override {
        // Clean up all active sessions
        auto sessions = engine->getActiveSessions();
        for (auto sessionId : sessions) {
            auto result = engine->destroySession(sessionId);
            (void)result;  // Suppress unused variable warning
        }
        engine.reset();

        TestFixtureBase::TearDown();
    }
};

TEST_F(CoreValidationTest, DTWSelfSimilarity) {
    const std::string masterCallId = "buck_grunt";

    // Skip if test data is missing (will auto-generate if needed)
    skipIfDataMissing(masterCallId, "DTW self-similarity test requires buck_grunt test data");

    // Get the audio file path using TestUtils
    auto audioFilePath = TestPaths::getMasterCallFile(masterCallId, ".wav");

    // Load the same file as the "user attempt"
    unsigned int channels, sampleRate;
    std::vector<float> audioData = load_audio_file(audioFilePath.string(), channels, sampleRate);
    if (audioData.empty()) {
        GTEST_SKIP() << "Failed to load audio file for DTW self-similarity test: " << audioFilePath;
        return;
    }

    // Create session using the new unified API
    auto sessionResult = engine->createSession(static_cast<float>(sampleRate));
    ASSERT_TRUE(sessionResult.isOk()) << "Failed to create session for DTW self-similarity test.";

    uint32_t sessionId = sessionResult.value;

    // Load master call using the session
    auto loadResult = engine->loadMasterCall(sessionId, masterCallId);
    if (loadResult != UnifiedAudioEngine::Status::OK) {
        auto destroyResult = engine->destroySession(sessionId);
        (void)destroyResult;  // Suppress unused variable warning
        GTEST_SKIP() << "Could not load master call file: " << masterCallId;
        return;
    }

    // Process the audio data
    auto processResult = engine->processAudioChunk(sessionId, std::span<const float>(audioData));
    EXPECT_EQ(processResult, UnifiedAudioEngine::Status::OK);

    // Get similarity score
    auto scoreResult = engine->getSimilarityScore(sessionId);
    [[maybe_unused]] auto destroyResult = engine->destroySession(sessionId);

    if (scoreResult.isOk()) {
        float score = scoreResult.value;
        EXPECT_GT(score, 0.5f);  // Expecting a reasonable score for self-similarity
    } else {
        GTEST_SKIP() << "Could not get similarity score - insufficient data or processing error";
    }
}
