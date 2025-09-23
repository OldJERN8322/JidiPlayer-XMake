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

// Notification structure
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
    const float ANIMATION_DURATION = 0.5f; // Animation duration in seconds
    const float NOTIFICATION_SPACING = 10.0f;
    const float TOP_MARGIN = 20.0f;
    
public:
    // Send notification function
    void SendNotification(float width, float height, Color backgroundColor, const std::string& text, float seconds);
    
    // Update and draw notifications
    void Update();
    void Draw();
    
    // Helper function to wrap text within given width
    std::vector<std::string> WrapText(const std::string& text, int fontSize, float maxWidth);
    
    // Calculate text bounds
    Rectangle MeasureTextBounds(const std::string& text, int fontSize, float maxWidth);
    
    // Clear all notifications
    void ClearAll();
};

// Global notification manager instance
extern NotificationManager g_NotificationManager;

// Convenience function for easy use
void SendNotification(float width, float height, Color backgroundColor, const std::string& text, float seconds);

#endif

// ===== Input values =====
extern bool inputActive;
extern std::string inputBuffer;

// ===== PFA Color Definitions =====
#define MCOLOR1     CLITERAL(Color){  51, 102, 255, 255 }
#define MCOLOR2     CLITERAL(Color){ 255, 102,  51, 255 }
#define MCOLOR3     CLITERAL(Color){  51, 255, 102, 255 }
#define MCOLOR4     CLITERAL(Color){ 255,  51, 129, 255 }
#define MCOLOR5     CLITERAL(Color){  51, 255, 255, 255 }
#define MCOLOR6     CLITERAL(Color){ 228,  51, 255, 255 }
#define MCOLOR7     CLITERAL(Color){ 153, 255,  51, 255 }
#define MCOLOR8     CLITERAL(Color){  75,  51, 255, 255 }
#define MCOLOR9     CLITERAL(Color){ 255, 204,  51, 255 }
#define MCOLOR10    CLITERAL(Color){  51, 180, 255, 255 }
#define MCOLOR11    CLITERAL(Color){ 255,  51,  51, 255 }
#define MCOLOR12    CLITERAL(Color){  51, 255, 177, 255 }
#define MCOLOR13    CLITERAL(Color){ 255,  51, 204, 255 }
#define MCOLOR14    CLITERAL(Color){  78, 255,  51, 255 }
#define MCOLOR15    CLITERAL(Color){ 153,  51, 255, 255 }
#define MCOLOR16    CLITERAL(Color){ 231, 255,  51, 255 }

// ===== Custom Colors =====
#define JGRAY      CLITERAL(Color){ 32, 32, 32, 255 }
#define JBLACK     CLITERAL(Color){ 8, 8, 8, 255 }
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
    uint8_t channel;        // Original MIDI channel for audio
    uint8_t visualTrack;    // Track index for visual coloring
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
enum class EventType { NOTE_ON, NOTE_OFF, CC, TEMPO, PITCH_BEND, PROGRAM_CHANGE, CHANNEL_PRESSURE, MARKER };

struct MidiEvent {
    uint32_t tick;
    EventType type;
    uint8_t channel;        // Original MIDI channel for audio
    uint8_t data1;          // Note number, CC controller, or Pitch Bend LSB
    uint8_t data2;          // Velocity, CC value, or Pitch Bend MSB
    uint32_t tempo;         // Only for tempo events
    uint8_t visualTrack;    // Track index for visual coloring (default 0)
    std::string text;

    // Constructor with default visualTrack
    MidiEvent(uint32_t t, EventType et, uint8_t ch, uint8_t d1, uint8_t d2, uint32_t tmp, uint8_t vt = 0)
        : tick(t), type(et), channel(ch), data1(d1), data2(d2), tempo(tmp), visualTrack(vt) {}

    // Comparator for sorting
    bool operator<(const MidiEvent& other) const {
        if (tick != other.tick) return tick < other.tick;
        return type < other.type;
    }
};

// ===== Function Declarations =====
std::vector<CCEvent> loadStreamingMidiData(const std::string& filename, std::vector<OptimizedTrackData>& tracks, int& ppq, int& initialTempo, uint64_t& totalNoteCount);
std::vector<TempoEvent> collectGlobalTempoEvents(const std::string& filename);
void DrawStreamingVisualizerNotes(const std::vector<OptimizedTrackData>& tracks, uint64_t currentTick, int ppq, uint32_t currentTempo);