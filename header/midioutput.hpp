#pragma once
#include "visualizer.hpp"
#include "midi_timing_alt.hpp"
#include <vector>
#include <thread>
#include <atomic>
#include <chrono>

class MidiOutputEngine {
public:
    MidiOutputEngine();
    ~MidiOutputEngine();
    void Start(const std::vector<MidiEvent>& events, int ppq, uint32_t initialTempo);
    void Stop();
    void Pause();
    void Resume();
    void Seek(int64_t microsecondOffset);
    void SetSpeed(float newSpeed);
    void SetLooping(bool loop);
    uint64_t GetCurrentTick() const;
    size_t GetEventPos() const;
    bool IsFinished() const;
    bool IsPaused() const;

private:
    void PlaybackThread();
    void SilenceAllChannels();
    std::thread workerThread;
    std::atomic<bool> threadRunning;
    std::atomic<bool> isPlaying;
    std::atomic<bool> isPaused;
    std::atomic<bool> isFinished;
    std::atomic<bool> isLooping;
    const std::vector<MidiEvent>* eventList;
    int currentPpq;
    std::atomic<uint64_t> currentVisualizerTick;
    std::atomic<float> playbackSpeed;
    std::chrono::steady_clock::time_point playbackStartTime;
    uint64_t accumulatedMicroseconds;
    std::atomic<size_t> eventPos;
    uint32_t lastProcessedTick;
    double microsecondsPerTick;
    uint32_t currentTempo;
};