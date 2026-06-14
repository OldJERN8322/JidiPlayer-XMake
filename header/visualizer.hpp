#pragma once

#ifndef NOTIFICATION_SYSTEM_H
#define NOTIFICATION_SYSTEM_H

#include <string>
#include <vector>
#include <cstdint>
#include <iostream>
#include <chrono>
#include <atomic>
#include <thread>
#include <string>
#include <cstring>
#include "raylib.h"

struct LoadProgress {
    std::atomic<bool> isFinished{false};
    std::atomic<bool> hasError{false};
    
    std::atomic<size_t> bytesRead{0};
    std::atomic<size_t> totalBytes{0};
    std::atomic<uint64_t> currentNotes{0};
    std::atomic<int> currentTrack{0};
    std::atomic<int> totalTracks{0};
    std::atomic<int> loadPhase{0}; // 0 = Idle, 1 = Reading, 2 = Optimizing/Sorting

    // Add this Reset method:
    void Reset() {
        isFinished.store(false, std::memory_order_relaxed);
        hasError.store(false, std::memory_order_relaxed);
        bytesRead.store(0, std::memory_order_relaxed);
        totalBytes.store(0, std::memory_order_relaxed);
        currentNotes.store(0, std::memory_order_relaxed);
        currentTrack.store(0, std::memory_order_relaxed);
        totalTracks.store(0, std::memory_order_relaxed);
        loadPhase.store(0, std::memory_order_relaxed);
    }
};


// ===================================================================
// EASING FUNCTIONS
// ===================================================================
float EaseInBack(float t);
float EaseOutBack(float t);

// ===================================================================
// NOTIFICATION SYSTEM DECLARATIONS
// ===================================================================
struct Notification {
    std::string text;
    Color backgroundColor;
    float width;
    float height;
    float targetY;
    float currentY;
    std::chrono::time_point<std::chrono::steady_clock> startTime;
    std::chrono::time_point<std::chrono::steady_clock> dismissTime;
    float duration;
    bool isVisible;
    bool isDismissing;
    Notification(const std::string& txt, Color bgColor, float w, float h, float dur);
};

class NotificationManager {
private:
    std::vector<Notification> notifications;
    const float ANIMATION_DURATION = 0.5f;
    const float NOTIFICATION_SPACING = 10.0f;
    const float TOP_MARGIN = 20.0f;

public:
    void SendNotification(float width, float height, Color backgroundColor, const std::string& text, float seconds);
    void Update();
    void Draw();
    std::vector<std::string> WrapText(const std::string& text, int fontSize, float maxWidth);
    Rectangle MeasureTextBounds(const std::string& text, int fontSize, float maxWidth);
    void ClearAll();
};
extern NotificationManager g_NotificationManager;
void SendNotification(float width, float height, Color backgroundColor, const std::string& text, float seconds);

#endif

// ===== Input values =====
extern bool inputActive;
extern std::string inputBuffer;

// ===== Custom Colors =====
#define JGRAY          CLITERAL(Color){ 32, 32, 32, 255 }
#define JBLACK         CLITERAL(Color){ 8, 8, 8, 255 }
#define JBG1A          CLITERAL(Color){ 16, 24, 32, 255 }
#define JBG1B          CLITERAL(Color){ 32, 48, 64, 255 }
#define JBG1C          CLITERAL(Color){ 48, 64, 96, 255 }
#define JLIGHTPINK     CLITERAL(Color){ 255, 192, 255, 255 }
#define JLIGHTBLUE     CLITERAL(Color){ 192, 224, 255, 255 }
#define JLIGHTLIME     CLITERAL(Color){ 192, 255, 192, 255 }
#define JLIGHTYELLOW   CLITERAL(Color){ 255, 255, 192, 255 }

// ===== Status color (Background) ======
#define SDEBUG         CLITERAL(Color){ 96, 48, 96, 255 }
#define SINFORMATION   CLITERAL(Color){ 48, 64, 96, 255 }
#define SSUCCESS       CLITERAL(Color){ 48, 96, 48, 255 }
#define SWARNING       CLITERAL(Color){ 96, 96, 48, 255 }
#define SERROR         CLITERAL(Color){ 96, 48, 48, 255 }

// ===== Performance color =====
#define PDarkerRed		CLITERAL(Color){16, 4, 4, 255}
#define PDarkRed		CLITERAL(Color){64, 16, 16, 255}
#define PRed			CLITERAL(Color){255, 64, 64, 255}
#define POrange			CLITERAL(Color){255, 128, 64, 255}
#define PYellow			CLITERAL(Color){255, 255, 64, 255}
#define PGreen			CLITERAL(Color){64, 255, 64, 255}
#define PCyan			CLITERAL(Color){64, 255, 255, 255}
#define PBlue			CLITERAL(Color){64, 128, 255, 255}
#define PMagenta		CLITERAL(Color){255, 128, 255, 255}
#define PWhite			CLITERAL(Color){255, 255, 255, 255}

// ===== Enums for State Management =====
enum AppState { STATE_MENU, STATE_LOADING, STATE_PLAYING };

// ===== Data Structures =====
// NoteEvent: naturally 12 bytes with zero padding (4+4+1+1+1+1).
// No #pragma pack needed — fields already align perfectly.
// DO NOT add pack(1) here: it breaks SIMD auto-vectorization in the renderer.
struct NoteEvent {
    uint32_t startTick;   // 4  offset 0
    uint32_t endTick;     // 4  offset 4
    uint8_t  note;        // 1  offset 8
    uint8_t  velocity;    // 1  offset 9
    uint8_t  channel;     // 1  offset 10
    uint8_t  visualTrack; // 1  offset 11 → total 12 bytes, zero padding
};

struct CCEvent {
    uint32_t tick;
    uint8_t  channel;
    uint8_t  controller;
    uint8_t  value;
};

struct OptimizedTrackData {
    std::vector<NoteEvent> notes;
};

struct TempoEvent {
    uint32_t tick;
    uint32_t tempoMicroseconds;
};

// ===== UNIFIED MIDI EVENT STRUCTURE =====
enum class EventType : uint8_t { NOTE_ON, NOTE_OFF, CC, TEMPO, PITCH_BEND, PROGRAM_CHANGE, CHANNEL_PRESSURE };
enum class ViewerType : uint8_t { ChannelTrackLayer, TickLayer };
enum class InputMode : uint8_t { Normal, Simulate };

// MidiEvent: 12 bytes.
// Layout: tick(4) + type(1) + channel(1) + _pad(2) + data(4) = 12B
// Field order is IDENTICAL to the original — do NOT reorder.
// midioutput.hpp and any other TU that uses MidiEvent by raw offset must
// see exactly this layout. The _pad field just makes the compiler-inserted
// padding explicit; it does not change sizeof or any field offset.
struct MidiEvent {
    uint32_t tick;      // offset 0 (4B)
    uint8_t  type;      // offset 4 (1B)
    uint8_t  channel;   // offset 5 (1B)
    uint16_t _pad{0};   // offset 6 (2B) — explicit; was implicit compiler padding before
    union {             // offset 8 (4B)
        struct { uint8_t n; uint8_t v; } note;  // NOTE_ON / NOTE_OFF
        struct { uint8_t c; uint8_t v; } cc;    // CC
        struct { uint8_t l1; uint8_t m2; } raw; // PITCH_BEND (LSB, MSB)
        uint8_t  val;                           // PROGRAM_CHANGE / CHANNEL_PRESSURE
        uint32_t tempo;                         // TEMPO (24-bit value in low 3 bytes)
    } data;

    MidiEvent(uint32_t t, EventType et, uint8_t ch)
        : tick(t), type((uint8_t)et), channel(ch), _pad(0) {
        memset(&data, 0, sizeof(data));
    }

    bool operator<(const MidiEvent& other) const {
        if (tick != other.tick) return tick < other.tick;
        return type < other.type;
    }
};

// ===== load.cpp — streaming MIDI parser (1:1 memory, uint24 tempo) =====
std::vector<CCEvent> loadStreamingMidiData(
    const std::string&              filename,
    std::vector<OptimizedTrackData>& tracks,
    int&                            ppq,
    int&                            initialTempo,
    uint64_t&                       totalNoteCount,
    uint16_t&                       outTimeSigNumerator,    // filled from meta 0x58; default 4
    uint16_t&                       outTimeSigDenominator,  // filled from meta 0x58; default 4
    LoadProgress*                   progress = nullptr);

std::vector<TempoEvent> collectGlobalTempoEvents(const std::string& filename);

// Sorted MidiEvent list produced by loadStreamingMidiData().
// Call after loading; pass directly to MidiOutputEngine::Start().
const std::vector<MidiEvent>& GetGlobalMidiEvents();