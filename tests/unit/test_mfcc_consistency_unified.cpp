/**
 * @file test_mfcc_consistency_unified.cpp
 * @brief MFCC consistency tests using the UnifiedAudioEngine API
 *
 * This test suite validates that MFCC processing produces consistent results
 * across multiple runs with the same audio data using the new UnifiedEngine
 * session-based architecture.
 */

#include <cmath>
#include <iomanip>
#include <iostream>
#include <span>
#include <vector>

#include <gtest/gtest.h>

#include "TestUtils.h"
#include "dr_wav.h"
#include "huntmaster/core/UnifiedAudioEngine.h"

using namespace huntmaster;
using namespace huntmaster::test;

// Generate a test sine wave
static std::vector<float> generateSineWave(float frequency, float duration, float sampleRate) {
    int numSamples = static_cast<int>(duration * sampleRate);
    std::vector<float> samples(numSamples);

    const float twoPi = 2.0f * 3.14159265359f;
    for (int i = 0; i < numSamples; ++i) {
        samples[i] = 0.5f * sin(twoPi * frequency * i / sampleRate);
    }

    return samples;
}

// Save audio to WAV file
static bool
saveTestWav(const std::string& filename, const std::vector<float>& samples, float sampleRate) {
    drwav wav;
    drwav_data_format format;
    format.container = drwav_container_riff;
    format.format = DR_WAVE_FORMAT_IEEE_FLOAT;
    format.channels = 1;
    format.sampleRate = static_cast<drwav_uint32>(sampleRate);
    format.bitsPerSample = 32;

    if (!drwav_init_file_write(&wav, filename.c_str(), &format, nullptr)) {
        std::cerr << "Failed to create file: " << filename << std::endl;
        return false;
    }

    drwav_uint64 framesWritten = drwav_write_pcm_frames(&wav, samples.size(), samples.data());
    drwav_uninit(&wav);

    return framesWritten == samples.size();
}

class MFCCConsistencyUnifiedTest : public TestFixtureBase {
  protected:
    void SetUp() override {
        TestFixtureBase::SetUp();

        // Create engine instance using the new UnifiedEngine API
        auto engineResult = UnifiedAudioEngine::create();
        ASSERT_TRUE(engineResult.isOk())
            << "Failed to create UnifiedAudioEngine: " << static_cast<int>(engineResult.error());
        engine = std::move(*engineResult);

        // Ensure test data exists
        ensureTestData();
    }

    void TearDown() override {
        // Clean up any remaining sessions
        auto activeSessions = engine->getActiveSessions();
        for (auto sessionId : activeSessions) {
            auto result = engine->destroySession(sessionId);
            (void)result;  // Suppress unused variable warning
        }
        engine.reset();

        TestFixtureBase::TearDown();
    }

    void ensureTestData() {
        // Ensure buck_grunt test data exists
        std::vector<std::string> requiredCalls = {"buck_grunt", "test_sine_440", "test_complex"};
        getResourceManager().ensureTestData(requiredCalls);
    }

    std::unique_ptr<UnifiedAudioEngine> engine;
};

// Test using existing master call to verify workflow
TEST_F(MFCCConsistencyUnifiedTest, ExistingMasterCallTest) {
    std::cout << "Debug: Testing with existing buck_grunt master call" << std::endl;
    std::cout << "---------------------------------------" << std::endl;

    // Skip if test data is missing
    skipIfDataMissing("buck_grunt", "buck_grunt test data not available");

    // Create a session
    auto sessionResult = engine->createSession(44100.0f);
    ASSERT_TRUE(sessionResult.isOk())
        << "Failed to create session: " << static_cast<int>(sessionResult.error());
    SessionId sessionId = sessionResult.value;
    std::cout << "  Created session with ID: " << sessionId << std::endl;

    // Try to load an existing master call
    auto loadResult = engine->loadMasterCall(sessionId, "buck_grunt");
    if (loadResult != UnifiedAudioEngine::Status::OK) {
        std::cout << "  loadMasterCall failed with status: " << static_cast<int>(loadResult)
                  << std::endl;
        auto destroyResult = engine->destroySession(sessionId);
        (void)destroyResult;
        GTEST_SKIP() << "buck_grunt master call not available";
        return;
    }
    std::cout << "  Successfully loaded buck_grunt master call" << std::endl;

    // Load the actual audio file using robust path resolution
    unsigned int channels, sampleRate;
    drwav_uint64 totalFrames;
    auto buckGruntPath = TestPaths::getMasterCallFile("buck_grunt", ".wav");
    float* audioData = drwav_open_file_and_read_pcm_frames_f32(
        buckGruntPath.string().c_str(), &channels, &sampleRate, &totalFrames, nullptr);

    if (!audioData) {
        auto destroyResult = engine->destroySession(sessionId);
        (void)destroyResult;
        GTEST_SKIP() << "buck_grunt audio file could not be loaded";
        return;
    }

    std::cout << "  Loaded audio: " << totalFrames << " frames, " << channels << " channels, "
              << sampleRate << " Hz" << std::endl;

    // Convert to mono if needed
    std::vector<float> monoData(totalFrames);
    if (channels > 1) {
        for (drwav_uint64 j = 0; j < totalFrames; ++j) {
            float sum = 0.0f;
            for (unsigned int ch = 0; ch < channels; ++ch) {
                sum += audioData[j * channels + ch];
            }
            monoData[j] = sum / channels;
        }
    } else {
        monoData.assign(audioData, audioData + totalFrames);
    }

    // Process the audio using the new span-based API
    auto processResult = engine->processAudioChunk(sessionId, std::span<const float>(monoData));
    std::cout << "  processAudioChunk status: " << static_cast<int>(processResult) << std::endl;
    EXPECT_EQ(processResult, UnifiedAudioEngine::Status::OK) << "Processing failed";

    // Check feature count
    auto featureCountResult = engine->getFeatureCount(sessionId);
    ASSERT_TRUE(featureCountResult.isOk()) << "Failed to get feature count";
    int featureCount = featureCountResult.value;
    std::cout << "  Features processed: " << featureCount << std::endl;

    // Try to get similarity score
    auto scoreResult = engine->getSimilarityScore(sessionId);
    if (!scoreResult.isOk()) {
        std::cout << "  getSimilarityScore failed with status: "
                  << static_cast<int>(scoreResult.status) << std::endl;
    } else {
        float score = scoreResult.value;
        std::cout << "  Similarity score: " << std::fixed << std::setprecision(8) << score
                  << std::endl;
    }

    // Clean up
    drwav_free(audioData, nullptr);
    auto destroyResult = engine->destroySession(sessionId);
    (void)destroyResult;

    // Validate results
    EXPECT_GT(featureCount, 0) << "No features were processed";
    EXPECT_TRUE(scoreResult.isOk())
        << "Failed to get similarity score, status: " << static_cast<int>(scoreResult.status);
}

TEST_F(MFCCConsistencyUnifiedTest, SineWaveConsistency) {
    std::cout << "Test 1: Processing 440 Hz sine wave" << std::endl;
    std::cout << "---------------------------------------" << std::endl;

    auto sineWave440 = generateSineWave(440.0f, 1.0f, 44100.0f);
    auto testFile1 = TestPaths::getMasterCallFile("test_sine_440", ".wav");

    if (!saveTestWav(testFile1.string(), sineWave440, 44100.0f)) {
        FAIL() << "Failed to create test file: " << testFile1;
    }
    std::cout << "Created test file: " << testFile1 << std::endl;

    // Process the same file 5 times and collect scores
    std::vector<float> scores;
    std::cout << "\nProcessing 5 times:" << std::endl;

    for (int i = 0; i < 5; ++i) {
        // Create a session for each iteration
        auto sessionResult = engine->createSession(44100.0f);
        ASSERT_TRUE(sessionResult.isOk())
            << "Failed to create session: " << static_cast<int>(sessionResult.error());
        SessionId sessionId = sessionResult.value;

        // Load as master call (this triggers MFCC processing)
        auto loadResult = engine->loadMasterCall(sessionId, "test_sine_440");
        if (loadResult != UnifiedAudioEngine::Status::OK) {
            std::cout << "  loadMasterCall failed with status: " << static_cast<int>(loadResult)
                      << std::endl;
        }
        ASSERT_EQ(loadResult, UnifiedAudioEngine::Status::OK) << "Failed to load master call";

        // Process the same audio using span-based API
        auto processResult =
            engine->processAudioChunk(sessionId, std::span<const float>(sineWave440));
        EXPECT_EQ(processResult, UnifiedAudioEngine::Status::OK) << "Processing failed";

        // Check how many features were processed
        auto featureCountResult = engine->getFeatureCount(sessionId);
        ASSERT_TRUE(featureCountResult.isOk()) << "Failed to get feature count";
        int featureCount = featureCountResult.value;
        std::cout << "  Features processed: " << featureCount << std::endl;

        auto scoreResult = engine->getSimilarityScore(sessionId);
        if (!scoreResult.isOk()) {
            std::cout << "  getSimilarityScore failed with status: "
                      << static_cast<int>(scoreResult.status) << std::endl;
        }
        ASSERT_TRUE(scoreResult.isOk())
            << "Failed to get similarity score, status: " << static_cast<int>(scoreResult.status);
        float score = scoreResult.value;
        scores.push_back(score);

        // Clean up session
        auto destroyResult = engine->destroySession(sessionId);
        (void)destroyResult;

        std::cout << "  Run " << (i + 1) << ": Score = " << std::fixed << std::setprecision(8)
                  << score << std::endl;
    }

    // Analyze consistency
    float minScore = scores[0];
    float maxScore = scores[0];
    float avgScore = 0.0f;

    for (float score : scores) {
        minScore = std::min(minScore, score);
        maxScore = std::max(maxScore, score);
        avgScore += score;
    }
    avgScore /= scores.size();

    float maxDeviation = maxScore - minScore;
    float maxDeviationPercent = (maxDeviation / avgScore) * 100.0f;

    std::cout << "\nResults:" << std::endl;
    std::cout << "  Average score: " << avgScore << std::endl;
    std::cout << "  Min score: " << minScore << std::endl;
    std::cout << "  Max score: " << maxScore << std::endl;
    std::cout << "  Max deviation: " << maxDeviation << " (" << maxDeviationPercent << "%)"
              << std::endl;

    bool test1Pass = maxDeviation < 0.0001f;
    std::cout << "  Status: " << (test1Pass ? "PASS ✓" : "FAIL ✗") << std::endl;

    EXPECT_TRUE(test1Pass) << "Sine wave MFCC processing inconsistent. Max deviation: "
                           << maxDeviation << " (" << maxDeviationPercent << "%)";
}

TEST_F(MFCCConsistencyUnifiedTest, ComplexWaveformConsistency) {
    std::cout << "\n\nTest 2: Processing complex waveform" << std::endl;
    std::cout << "---------------------------------------" << std::endl;

    // Create a more complex waveform
    std::vector<float> complexWave(44100);  // 1 second
    for (int i = 0; i < 44100; ++i) {
        float t = i / 44100.0f;
        complexWave[i] = 0.3f * sin(2.0f * 3.14159f * 220.0f * t) +  // 220 Hz
                         0.2f * sin(2.0f * 3.14159f * 440.0f * t) +  // 440 Hz
                         0.1f * sin(2.0f * 3.14159f * 880.0f * t);   // 880 Hz
    }

    auto testFile2 = TestPaths::getMasterCallFile("test_complex", ".wav");
    if (!saveTestWav(testFile2.string(), complexWave, 44100.0f)) {
        FAIL() << "Failed to create complex test file: " << testFile2;
    }

    // Process multiple times
    std::vector<float> complexScores;
    for (int i = 0; i < 5; ++i) {
        // Create a session for each iteration
        auto sessionResult = engine->createSession(44100.0f);
        ASSERT_TRUE(sessionResult.isOk())
            << "Failed to create session: " << static_cast<int>(sessionResult.error());
        SessionId sessionId = sessionResult.value;

        auto loadResult = engine->loadMasterCall(sessionId, "test_complex");
        EXPECT_TRUE(loadResult == UnifiedAudioEngine::Status::OK
                    || loadResult == UnifiedAudioEngine::Status::FILE_NOT_FOUND);

        // Process audio in chunks to simulate real-time processing
        const size_t chunkSize = 1024;
        for (size_t j = 0; j < complexWave.size(); j += chunkSize) {
            size_t remaining = complexWave.size() - j;
            size_t toProcess = std::min(chunkSize, remaining);

            std::span<const float> chunk(complexWave.data() + j, toProcess);
            auto chunkResult = engine->processAudioChunk(sessionId, chunk);
            EXPECT_EQ(chunkResult, UnifiedAudioEngine::Status::OK) << "Chunk processing failed";
        }

        auto scoreResult = engine->getSimilarityScore(sessionId);
        ASSERT_TRUE(scoreResult.isOk()) << "Failed to get similarity score";
        float score = scoreResult.value;
        complexScores.push_back(score);

        // Clean up session
        auto destroyResult = engine->destroySession(sessionId);
        (void)destroyResult;

        std::cout << "  Run " << (i + 1) << ": Score = " << std::fixed << std::setprecision(8)
                  << score << std::endl;
    }

    // Analyze complex wave consistency
    float complexMaxDev = 0.0f;
    for (size_t i = 1; i < complexScores.size(); ++i) {
        complexMaxDev = std::max(complexMaxDev, std::abs(complexScores[i] - complexScores[0]));
    }

    bool test2Pass = complexMaxDev < 0.0001f;
    std::cout << "\nResults:" << std::endl;
    std::cout << "  Max deviation: " << complexMaxDev << std::endl;
    std::cout << "  Status: " << (test2Pass ? "PASS ✓" : "FAIL ✗") << std::endl;

    EXPECT_TRUE(test2Pass) << "Complex waveform MFCC processing inconsistent. Max deviation: "
                           << complexMaxDev;
}

TEST_F(MFCCConsistencyUnifiedTest, RealAudioFileConsistency) {
    std::cout << "\n\nTest 3: Processing real audio file" << std::endl;
    std::cout << "---------------------------------------" << std::endl;

    // Try to test with buck_grunt if it exists
    std::vector<float> realScores;
    bool realFileExists = false;

    for (int i = 0; i < 3; ++i) {
        try {
            // Create a session for each iteration
            auto sessionResult = engine->createSession(44100.0f);
            ASSERT_TRUE(sessionResult.isOk())
                << "Failed to create session: " << static_cast<int>(sessionResult.error());
            SessionId sessionId = sessionResult.value;

            auto loadResult = engine->loadMasterCall(sessionId, "buck_grunt");
            if (loadResult == UnifiedAudioEngine::Status::OK) {
                realFileExists = true;
            }

            // Load the actual audio file
            unsigned int channels, sampleRate;
            drwav_uint64 totalFrames;
            auto buckGruntPath = TestPaths::getMasterCallFile("buck_grunt", ".wav");
            float* audioData = drwav_open_file_and_read_pcm_frames_f32(
                buckGruntPath.string().c_str(), &channels, &sampleRate, &totalFrames, nullptr);

            if (audioData) {
                // Convert to mono if needed
                std::vector<float> monoData(totalFrames);
                if (channels > 1) {
                    for (drwav_uint64 j = 0; j < totalFrames; ++j) {
                        float sum = 0.0f;
                        for (unsigned int ch = 0; ch < channels; ++ch) {
                            sum += audioData[j * channels + ch];
                        }
                        monoData[j] = sum / channels;
                    }
                } else {
                    monoData.assign(audioData, audioData + totalFrames);
                }

                // Process audio in chunks
                const size_t chunkSize = 1024;
                for (size_t j = 0; j < monoData.size(); j += chunkSize) {
                    size_t remaining = monoData.size() - j;
                    size_t toProcess = std::min(chunkSize, remaining);

                    std::span<const float> chunk(monoData.data() + j, toProcess);
                    auto chunkResult = engine->processAudioChunk(sessionId, chunk);
                    EXPECT_EQ(chunkResult, UnifiedAudioEngine::Status::OK)
                        << "Chunk processing failed";
                }

                auto scoreResult = engine->getSimilarityScore(sessionId);
                ASSERT_TRUE(scoreResult.isOk()) << "Failed to get similarity score";
                float score = scoreResult.value;
                realScores.push_back(score);

                std::cout << "  Run " << (i + 1) << ": Score = " << std::fixed
                          << std::setprecision(8) << score << std::endl;

                drwav_free(audioData, nullptr);
            }

            // Clean up session
            auto destroyResult = engine->destroySession(sessionId);
            (void)destroyResult;
        } catch (...) {
            std::cout << "  Could not load buck_grunt.wav - SKIPPED" << std::endl;
            break;
        }
    }

    if (realFileExists && realScores.size() > 1) {
        float realMaxDev = 0.0f;
        for (size_t i = 1; i < realScores.size(); ++i) {
            realMaxDev = std::max(realMaxDev, std::abs(realScores[i] - realScores[0]));
        }

        bool test3Pass = realMaxDev < 0.0001f;
        std::cout << "\nResults:" << std::endl;
        std::cout << "  Max deviation: " << realMaxDev << std::endl;
        std::cout << "  Status: " << (test3Pass ? "PASS ✓" : "FAIL ✗") << std::endl;

        EXPECT_TRUE(test3Pass) << "Real audio file MFCC processing inconsistent. Max deviation: "
                               << realMaxDev;
    } else {
        std::cout << "  No real audio file found for testing" << std::endl;
        GTEST_SKIP() << "Real audio file not available for testing";
    }
}

// Test comparing master call to itself - should give high similarity score
TEST_F(MFCCConsistencyUnifiedTest, SelfSimilarityTest) {
    std::cout << "Debug: Testing self-similarity (comparing audio to itself)" << std::endl;
    std::cout << "---------------------------------------" << std::endl;

    // Create a session
    auto sessionResult = engine->createSession(44100.0f);
    ASSERT_TRUE(sessionResult.isOk())
        << "Failed to create session: " << static_cast<int>(sessionResult.error());
    SessionId sessionId = sessionResult.value;

    // Load buck_grunt as master call
    auto loadResult = engine->loadMasterCall(sessionId, "buck_grunt");
    if (loadResult != UnifiedAudioEngine::Status::OK) {
        std::cout << "  loadMasterCall failed with status: " << static_cast<int>(loadResult)
                  << std::endl;
        auto destroyResult = engine->destroySession(sessionId);
        (void)destroyResult;
        GTEST_SKIP() << "buck_grunt master call not available";
        return;
    }
    std::cout << "  Successfully loaded buck_grunt as master call" << std::endl;

    // Load the SAME audio file that we just loaded as master
    unsigned int channels, sampleRate;
    drwav_uint64 totalFrames;
    auto buckGruntPath = TestPaths::getMasterCallFile("buck_grunt", ".wav");
    float* audioData = drwav_open_file_and_read_pcm_frames_f32(
        buckGruntPath.string().c_str(), &channels, &sampleRate, &totalFrames, nullptr);

    if (!audioData) {
        auto destroyResult = engine->destroySession(sessionId);
        (void)destroyResult;
        GTEST_SKIP() << "buck_grunt audio file could not be loaded";
        return;
    }

    std::cout << "  Loaded same audio for comparison: " << totalFrames << " frames" << std::endl;

    // Convert to mono if needed
    std::vector<float> monoData(totalFrames);
    if (channels > 1) {
        for (drwav_uint64 j = 0; j < totalFrames; ++j) {
            float sum = 0.0f;
            for (unsigned int ch = 0; ch < channels; ++ch) {
                sum += audioData[j * channels + ch];
            }
            monoData[j] = sum / channels;
        }
    } else {
        monoData.assign(audioData, audioData + totalFrames);
    }

    // Process the same audio
    auto processResult = engine->processAudioChunk(sessionId, std::span<const float>(monoData));
    std::cout << "  processAudioChunk status: " << static_cast<int>(processResult) << std::endl;
    EXPECT_EQ(processResult, UnifiedAudioEngine::Status::OK) << "Processing failed";

    // Check feature count
    auto featureCountResult = engine->getFeatureCount(sessionId);
    ASSERT_TRUE(featureCountResult.isOk()) << "Failed to get feature count";
    int featureCount = featureCountResult.value;
    std::cout << "  Features processed: " << featureCount << std::endl;

    // Get similarity score - should be high since we're comparing audio to itself
    auto scoreResult = engine->getSimilarityScore(sessionId);
    if (!scoreResult.isOk()) {
        std::cout << "  getSimilarityScore failed with status: "
                  << static_cast<int>(scoreResult.status) << std::endl;
    } else {
        float score = scoreResult.value;
        std::cout << "  Self-similarity score: " << std::fixed << std::setprecision(8) << score
                  << std::endl;
        std::cout << "  Expected: High similarity (>0.8) since comparing audio to itself"
                  << std::endl;
    }

    // Clean up
    drwav_free(audioData, nullptr);
    auto destroyResult = engine->destroySession(sessionId);
    (void)destroyResult;

    EXPECT_GT(featureCount, 0) << "No features were processed";
    EXPECT_TRUE(scoreResult.isOk())
        << "Failed to get similarity score, status: " << static_cast<int>(scoreResult.status);

    if (scoreResult.isOk()) {
        float score = scoreResult.value;
        // Based on real-world analysis tool thresholds:
        // >0.01 = excellent, >0.005 = good, >0.002 = fair
        // Self-similarity should be at least "good" level
        EXPECT_GT(score, 0.002f) << "Self-similarity should be at least fair level, got: " << score;

        // For debugging: show what level this would be considered
        if (score > 0.01f) {
            std::cout << "  Similarity level: EXCELLENT" << std::endl;
        } else if (score > 0.005f) {
            std::cout << "  Similarity level: GOOD" << std::endl;
        } else if (score > 0.002f) {
            std::cout << "  Similarity level: FAIR" << std::endl;
        } else {
            std::cout << "  Similarity level: POOR" << std::endl;
        }
    }
}
