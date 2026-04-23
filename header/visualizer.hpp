#pragma once

#ifndef NOTIFICATION_SYSTEM_H
#define NOTIFICATION_SYSTEM_H

#include <string>
#include <vector>
#include <cstdint>
#include <iostream>
#include <chrono>
#include "raylib.h"

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
#define JGRAY      CLITERAL(Color){ 32, 32, 32, 255 }
#define JBLACK     CLITERAL(Color){ 8, 8, 8, 255 }
#define JBG1A      CLITERAL(Color){ 16, 24, 32, 255 }
#define JBG1B      CLITERAL(Color){ 32, 48, 64, 255 }
#define JLIGHTPINK     CLITERAL(Color){ 255, 192, 255, 255 }
#define JLIGHTBLUE     CLITERAL(Color){ 192, 224, 255, 255 }
#define JLIGHTLIME     CLITERAL(Color){ 192, 255, 192, 255 }

// ===== Status color (Background) ======
#define SDEBUG         CLITERAL(Color){ 96, 48, 96, 255 }
#define SINFORMATION   CLITERAL(Color){ 48, 64, 96, 255 }
#define SSUCCESS       CLITERAL(Color){ 48, 96, 48, 255 }
#define SWARNING       CLITERAL(Color){ 96, 96, 48, 255 }
#define SERROR         CLITERAL(Color){ 96, 48, 48, 255 }

// ===== Enums for State Management =====
enum AppState { STATE_MENU, STATE_LOADING, STATE_PLAYING };

// ===== Data Structures =====
struct NoteEvent {
    uint32_t startTick;
    uint32_t endTick;
    uint8_t note;
    uint8_t velocity;
    uint8_t channel;
    uint8_t visualTrack;
};

struct CCEvent {
    uint32_t tick;
    uint8_t channel;
    uint8_t controller;
    uint8_t value;
};

struct OptimizedTrackData {
    std::vector<NoteEvent> notes;
};

struct TempoEvent {
    uint32_t tick;
    uint32_t tempoMicroseconds;
};

// ===== NEW UNIFIED MIDI EVENT STRUCTURE =====
enum class EventType : uint8_t { NOTE_ON, NOTE_OFF, CC, TEMPO, PITCH_BEND, PROGRAM_CHANGE, CHANNEL_PRESSURE };

struct MidiEvent {
    uint32_t tick;
    uint8_t type;    
    uint8_t channel;
    
    union {
        struct { uint8_t n; uint8_t v; } note;   // Note On/Off (Note, Velocity)
        struct { uint8_t c; uint8_t v; } cc;     // CC (Controller, Value)
        struct { uint8_t l1; uint8_t m2; } raw;  // Pitch Bend (LSB, MSB)
        uint8_t val;                             // Program Change / Pressure
        uint32_t tempo;                          // Tempo
    } data;

    MidiEvent(uint32_t t, EventType et, uint8_t ch) : tick(t), type((uint8_t)et), channel(ch) {
        memset(&data, 0, sizeof(data));
    }

    bool operator<(const MidiEvent& other) const {
        if (tick != other.tick) return tick < other.tick;
        return type < other.type;
    }
};

// ===== Function Declarations =====
std::vector<CCEvent> loadStreamingMidiData(const std::string& filename, std::vector<OptimizedTrackData>& tracks, int& ppq, int& initialTempo, uint64_t& totalNoteCount);
std::vector<TempoEvent> collectGlobalTempoEvents(const std::string& filename);
void DrawStreamingVisualizerNotes(const std::vector<OptimizedTrackData>& tracks, uint64_t currentTick, int ppq, uint32_t currentTempo);