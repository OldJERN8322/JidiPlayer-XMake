#include "midioutput.hpp"
#include <iostream>

extern "C" {
    void SendDirectData(unsigned long data);
}

MidiOutputEngine::MidiOutputEngine() : 
    threadRunning(false), isPlaying(false), isPaused(false), isFinished(false), isLooping(false),
    eventList(nullptr), currentPpq(480), currentVisualizerTick(0), playbackSpeed(1.0f) {
}

MidiOutputEngine::~MidiOutputEngine() {
    Stop();
}

void MidiOutputEngine::Start(const std::vector<MidiEvent>& events, int ppq, uint32_t initialTempo) {
    Stop();
    eventList = &events;
    currentPpq = ppq;
    currentTempo = initialTempo;
    microsecondsPerTick = MidiTiming::CalculateMicrosecondsPerTick(currentTempo, ppq);
    accumulatedMicroseconds = 0;
    eventPos = 0;
    lastProcessedTick = 0;
    currentVisualizerTick = 0;
    isFinished = false;
    isPaused = false;
    isPlaying = true;
    threadRunning = true;
    playbackStartTime = std::chrono::steady_clock::now();
    workerThread = std::thread(&MidiOutputEngine::PlaybackThread, this);
}

void MidiOutputEngine::Stop() {
    if (threadRunning) {
        threadRunning = false;
        if (workerThread.joinable()) {
            workerThread.join();
        }
    }
    SilenceAllChannels();
    isPlaying = false;
}

void MidiOutputEngine::Pause() {
    if (!isPaused && isPlaying) {
        isPaused = true;
        SilenceAllChannels();
    }
}

void MidiOutputEngine::Resume() {
    if (isPaused && isPlaying) {
        playbackStartTime = std::chrono::steady_clock::now() - std::chrono::microseconds(
            (uint64_t)(accumulatedMicroseconds / playbackSpeed.load())
        );
        isPaused = false;
    }
}

void MidiOutputEngine::SilenceAllChannels() {
    for (int ch = 0; ch < 16; ++ch) {
        SendDirectData((0xB0 | ch) | (123 << 8)); // All Notes Off
        SendDirectData((0xB0 | ch) | (121 << 8)); // Reset All Controllers
    }
}

void MidiOutputEngine::SetSpeed(float newSpeed) {
    if (isPlaying && !isPaused) {
        auto now = std::chrono::steady_clock::now();
        uint64_t elapsedRealMicros = std::chrono::duration_cast<std::chrono::microseconds>(now - playbackStartTime).count();
        uint64_t elapsedVirtualMicros = (uint64_t)(elapsedRealMicros * playbackSpeed.load());
        playbackSpeed = newSpeed;
        playbackStartTime = now - std::chrono::microseconds((uint64_t)(elapsedVirtualMicros / playbackSpeed.load()));
    } else {
        playbackSpeed = newSpeed;
    }
    microsecondsPerTick = MidiTiming::CalculateMicrosecondsPerTick(currentTempo, currentPpq) / playbackSpeed.load();
}

void MidiOutputEngine::SetLooping(bool loop) {
    isLooping = loop;
}

uint64_t MidiOutputEngine::GetCurrentTick() const {
    return currentVisualizerTick.load();
}

size_t MidiOutputEngine::GetEventPos() const {
    return eventPos.load();
}

bool MidiOutputEngine::IsFinished() const {
    return isFinished.load();
}

bool MidiOutputEngine::IsPaused() const {
    return isPaused.load();
}

void MidiOutputEngine::Seek(int64_t microsecondOffset) {
    bool wasPlaying = !isPaused.load();
    Pause();
    SilenceAllChannels();
    int64_t targetMicros = (int64_t)accumulatedMicroseconds + microsecondOffset;
    if (targetMicros < 0) targetMicros = 0;
    accumulatedMicroseconds = (uint64_t)targetMicros;
    eventPos = 0;
    lastProcessedTick = 0;
    uint64_t scanAccumulatedMicros = 0;
    uint32_t tempTempo = MidiTiming::DEFAULT_TEMPO_MICROSECONDS;
    if (!eventList->empty() && (*eventList)[0].type == (uint8_t)EventType::TEMPO) {
        tempTempo = (*eventList)[0].data.tempo;
    }
    double tempMicrosPerTick = MidiTiming::CalculateMicrosecondsPerTick(tempTempo, currentPpq) / playbackSpeed.load();
    while (eventPos < eventList->size()) {
        const auto& event = (*eventList)[eventPos];
        uint64_t eventScheduledTime = scanAccumulatedMicros + (uint64_t)((event.tick - lastProcessedTick) * tempMicrosPerTick);
        if (eventScheduledTime >= accumulatedMicroseconds) break;
        scanAccumulatedMicros = eventScheduledTime;
        lastProcessedTick = event.tick;
        if (event.type == (uint8_t)EventType::TEMPO) {
            tempTempo = event.data.tempo;
            tempMicrosPerTick = MidiTiming::CalculateMicrosecondsPerTick(tempTempo, currentPpq) / playbackSpeed.load();
        }
        eventPos++;
    }
    currentTempo = tempTempo;
    microsecondsPerTick = tempMicrosPerTick;
	uint64_t microsSinceLastEvent = (accumulatedMicroseconds > scanAccumulatedMicros) ? accumulatedMicroseconds - scanAccumulatedMicros : 0;
	currentVisualizerTick = lastProcessedTick + (uint64_t)(microsSinceLastEvent / tempMicrosPerTick);
	lastProcessedTick = (uint32_t)currentVisualizerTick.load(); // Re-anchors the engine time
    if (isFinished && eventPos < eventList->size()) {
        isFinished = false;
    }
    if (wasPlaying) Resume();
}

void MidiOutputEngine::PlaybackThread() {
    while (threadRunning) {
        if (isPaused || isFinished) {
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
            continue;
        }
        auto now = std::chrono::steady_clock::now();
        uint64_t elapsedRealMicros = std::chrono::duration_cast<std::chrono::microseconds>(now - playbackStartTime).count();
        uint64_t elapsedVirtualMicros = (uint64_t)(elapsedRealMicros * playbackSpeed.load());
        uint64_t microsSinceLastEvent = (elapsedVirtualMicros > accumulatedMicroseconds) ? elapsedVirtualMicros - accumulatedMicroseconds : 0;
        if (microsecondsPerTick > 0) {
            currentVisualizerTick = lastProcessedTick + (uint64_t)(microsSinceLastEvent / microsecondsPerTick);
        }
        while (eventPos < eventList->size() && threadRunning && !isPaused) {
            const auto& event = (*eventList)[eventPos];
            uint64_t scheduledTime = accumulatedMicroseconds + (uint64_t)((event.tick - lastProcessedTick) * microsecondsPerTick);
            if (scheduledTime > elapsedVirtualMicros) {
                uint64_t waitTimeMicros = scheduledTime - elapsedVirtualMicros;
                if (waitTimeMicros > 2000) {
                    std::this_thread::sleep_for(std::chrono::microseconds(waitTimeMicros - 1500));
                }
                break;
            }
            accumulatedMicroseconds = scheduledTime;
            lastProcessedTick = event.tick;
            if (event.type == (uint8_t)EventType::TEMPO) {
                currentTempo = event.data.tempo;
                microsecondsPerTick = MidiTiming::CalculateMicrosecondsPerTick(currentTempo, currentPpq) / playbackSpeed.load();
            } else if (event.type == (uint8_t)EventType::NOTE_OFF) {
                SendDirectData((0x80 | event.channel) | (event.data.note.n << 8) | (event.data.note.v << 16));
            } else if (event.type == (uint8_t)EventType::NOTE_ON) {
                SendDirectData((0x90 | event.channel) | (event.data.note.n << 8) | (event.data.note.v << 16));
            } else if (event.type == (uint8_t)EventType::CC) {
                SendDirectData((0xB0 | event.channel) | (event.data.cc.c << 8) | (event.data.cc.v << 16));
            } else if (event.type == (uint8_t)EventType::PITCH_BEND) {
                SendDirectData((0xE0 | event.channel) | (event.data.raw.l1 << 8) | (event.data.raw.m2 << 16));
            } else if (event.type == (uint8_t)EventType::PROGRAM_CHANGE) {
                SendDirectData((0xC0 | event.channel) | (event.data.val << 8));
            }
            eventPos++;
        }
        if (eventPos >= eventList->size()) {
            if (isLooping.load()) {
                Seek(-(int64_t)accumulatedMicroseconds); // Jump to beginning
            } else {
                isFinished = true;
            }
        }
    }
}