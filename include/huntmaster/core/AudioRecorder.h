/**
 * @file AudioRecorder.h
 * @brief Real-time audio recording interface for wildlife call capture
 *
 * This file provides the AudioRecorder class which handles real-time audio
 * capture from microphones or other audio input devices. The recorder is
 * designed for wildlife call analysis with optimized settings for voice
 * frequency ranges and minimal latency recording.
 *
 * @author Huntmaster Development Team
 * @date 2024
 * @copyright All Rights Reserved
 * @version 4.1
 */

#pragma once
#include <atomic>
#include <memory>
#include <string>
#include <thread>
#include <vector>

namespace huntmaster {

/**
 * @class AudioRecorder
 * @brief Real-time audio recording interface optimized for wildlife call capture
 *
 * The AudioRecorder class provides a high-level interface for capturing audio
 * from input devices with minimal latency and optimal settings for wildlife
 * call analysis. It supports mono recording at various sample rates and
 * provides real-time audio level monitoring.
 *
 * Key features:
 * - Real-time audio capture with configurable parameters
 * - Live audio level monitoring for UI feedback
 * - Automatic silence trimming for clean recordings
 * - WAV file export with proper formatting
 * - Thread-safe recording operations
 *
 * @example
 * @code
 * huntmaster::AudioRecorder recorder;
 * huntmaster::AudioRecorder::Config config;
 * config.sampleRate = 44100;
 * config.channels = 1;
 * config.bufferSize = 512;
 *
 * if (recorder.startRecording(config)) {
 *     // Monitor recording level
 *     while (recorder.isRecording()) {
 *         float level = recorder.getCurrentLevel();
 *         std::cout << "Audio level: " << level << std::endl;
 *         std::this_thread::sleep_for(std::chrono::milliseconds(100));
 *     }
 *
 *     // Save the recording
 *     recorder.saveToWavTrimmed("wildlife_call.wav");
 * }
 * @endcode
 */
class AudioRecorder {
  public:
    /**
     * @struct Config
     * @brief Configuration parameters for audio recording
     *
     * This structure contains all parameters needed to configure the audio
     * recording session, including sample rate, channel configuration, and
     * buffer settings for optimal performance.
     */
    struct Config {
        /** @brief Sample rate in Hz (typically 44100 or 48000) */
        int sampleRate = 44100;

        /** @brief Number of audio channels (1 for mono, 2 for stereo) */
        int channels = 1;  // Mono for voice

        /** @brief Buffer size in samples (affects latency and performance) */
        int bufferSize = 512;
    };

    /**
     * @brief Save recorded audio to WAV file with automatic silence trimming
     *
     * Exports the recorded audio data to a WAV file, automatically removing
     * silence from the beginning and end of the recording for cleaner analysis.
     *
     * @param filename Path to the output WAV file
     * @return true if the file was saved successfully, false otherwise
     *
     * @note This method applies automatic silence detection and trimming
     * @see saveToWav() for saving without trimming
     */
    bool saveToWavTrimmed(const std::string& filename) const;

    /**
     * @brief Construct a new AudioRecorder object
     *
     * Creates an AudioRecorder instance with default configuration.
     * The recorder is initially in a stopped state.
     */
    AudioRecorder();

    /**
     * @brief Destroy the AudioRecorder object
     *
     * Automatically stops any active recording and cleans up resources.
     */
    ~AudioRecorder();

    /**
     * @brief Start audio recording with the specified configuration
     *
     * Begins real-time audio capture from the default input device using
     * the provided configuration parameters. This method is non-blocking
     * and returns immediately after starting the recording thread.
     *
     * @param config Recording configuration parameters
     * @return true if recording started successfully, false otherwise
     *
     * @note Only one recording session can be active at a time
     * @warning Ensure proper audio permissions are granted on mobile platforms
     */
    bool startRecording(const Config& config);

    /**
     * @brief Stop the current recording session
     *
     * Stops the active recording session and finalizes the captured audio data.
     * This method blocks until the recording thread has been properly shut down.
     *
     * @note Safe to call even if no recording is active
     */
    void stopRecording();

    /**
     * @brief Check if a recording session is currently active
     *
     * @return true if recording is in progress, false otherwise
     */
    bool isRecording() const;

    /**
     * @brief Get the complete recorded audio data
     *
     * Returns all audio samples captured since the recording started.
     * The data is normalized to the range [-1.0, 1.0] and ready for
     * further processing or analysis.
     *
     * @return Vector of normalized audio samples
     *
     * @note The returned data remains valid until the next recording session starts
     * @see getCurrentLevel() for real-time audio level monitoring
     */
    std::vector<float> getRecordedData() const;

    /**
     * @brief Save recorded audio to WAV file without processing
     *
     * Exports the raw recorded audio data to a WAV file without any
     * preprocessing or silence trimming.
     *
     * @param filename Path to the output WAV file
     * @return true if the file was saved successfully, false otherwise
     *
     * @see saveToWavTrimmed() for automatic silence removal
     */
    bool saveToWav(const std::string& filename) const;

    /**
     * @brief Get the current real-time audio level
     *
     * Returns the current audio input level as a normalized value between
     * 0.0 (silence) and 1.0 (maximum level). This can be used for real-time
     * UI feedback such as level meters or recording indicators.
     *
     * @return Current audio level in the range [0.0, 1.0]
     *
     * @note This method provides real-time feedback and can be called frequently
     * @note Returns 0.0 when no recording is active
     */
    float getCurrentLevel() const;

    /**
     * @brief Get the duration of the current recording
     *
     * Returns the total duration of the current recording session in seconds.
     *
     * @return Recording duration in seconds, or 0.0 if no recording is active
     */
    double getDuration() const;

  private:
    class Impl;
    std::unique_ptr<Impl> pImpl;
};

}  // namespace huntmaster
