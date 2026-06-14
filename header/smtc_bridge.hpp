#pragma once
#ifdef _WIN32

#include <string>
#include <cstdint>
#include <functional>

// Callbacks your main loop fills in
struct SmtcCallbacks {
    std::function<void()>         onPlay;
    std::function<void()>         onPause;
    std::function<void()>         onStop;
    std::function<void(int64_t)>  onSeek;  // absolute microseconds
};

class SmtcBridge {
public:
    SmtcBridge();
    ~SmtcBridge();

    // Call once after window is created
    bool Init(SmtcCallbacks callbacks);

    // Call every frame (or whenever state changes)
    void UpdatePlaybackState(bool isPlaying, bool isPaused, bool isFinished);
    void UpdatePosition(uint64_t currentMicros, uint64_t totalMicros);
    void UpdateMetadata(const std::string& title,   // e.g. filename stem
                        const std::string& artist);  // e.g. "JIDI Player"

    void Shutdown();

private:
    struct Impl;
    Impl* impl = nullptr;
};

extern SmtcBridge g_Smtc;

#endif // _WIN32