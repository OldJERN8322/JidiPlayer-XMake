#pragma once

#include <string>
#include <vector>
#include <cstdint>
#include <iostream>
#include "raylib.h"

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

// ===== Struct Definitions =====

struct NoteEvent {
    uint32_t startTick;
    uint32_t endTick;
    uint8_t note;
    uint8_t velocity;
    uint8_t channel;
};

struct TrackData {
    std::vector<NoteEvent> notes;
};

struct TempoEvent {
    uint32_t tick;
    uint32_t tempoMicroseconds;
};

struct TempoMapEntry {
    uint32_t tick;
    uint32_t tempoMicroseconds;
};

struct PlaybackEvent {
    enum Type { NOTE, TEMPO };
    Type type;
    uint32_t tick;
    uint8_t status;
    uint8_t note;
    uint8_t velocity;
    uint32_t tempoValue;

    bool operator<(const PlaybackEvent& other) const {
        if (tick == other.tick) {
            // Tempo events should come before note events at the same tick
            return type == TEMPO && other.type == NOTE;
        }
        return tick < other.tick;
    }
};

// ===== Utility Functions =====

inline Color GetTrackColorPFA(int index) {
    static Color pfaColors[] = {
        MCOLOR1, MCOLOR2, MCOLOR3, MCOLOR4,
        MCOLOR5, MCOLOR6, MCOLOR7, MCOLOR8,
        MCOLOR9, MCOLOR10, MCOLOR11, MCOLOR12,
        MCOLOR13, MCOLOR14, MCOLOR15, MCOLOR16
    };
    int count = sizeof(pfaColors) / sizeof(Color);
    return pfaColors[index % count];
}

// ===== Function Declarations =====

// Main drawing function that chooses the appropriate render mode
void DrawVisualizerNotes(const std::vector<TrackData>& tracks, int currentTick, int ppq, std::vector<size_t>& searchStartIndices);

// The actual drawing implementation for the default mode
void DrawVisualizerNotesDefault(const std::vector<TrackData>& tracks, int currentTick, int ppq, std::vector<size_t>& searchStartIndices);

// The drawing implementation for the tracks mode
void DrawVisualizerNotesTracks(const std::vector<TrackData>& tracks, int currentTick, int ppq);

bool loadVisualizerMidiData(const std::string& filename, std::vector<TrackData>& tracks, int& ppq, int& tempoMicroseconds);

// Function to collect tempo events from MIDI file
std::vector<TempoEvent> collectGlobalTempoEvents(const std::string& filename);