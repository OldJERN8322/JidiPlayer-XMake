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

// ===== Enums for State Management =====
enum RenderMode { RENDER_DEFAULT, RENDER_TRACKS };
enum AppState { STATE_MENU, STATE_LOADING, STATE_PLAYING };

// ===== Data Structures =====
struct NoteEvent {
    uint32_t startTick;
    uint32_t endTick;
    uint8_t note;
    uint8_t velocity;
    uint8_t channel;
};

struct TrackData {
    std::vector<NoteEvent> notes;
    uint8_t channel;
};

struct TempoEvent {
    uint32_t tick;
    uint32_t tempoMicroseconds;
};

// ===== Utility Functions =====
inline Color GetTrackColorPFA(int index);

// ===== Function Declarations =====
bool loadVisualizerMidiData(const std::string& filename, std::vector<TrackData>& tracks, int& ppq, int& initialTempo);
std::vector<TempoEvent> collectGlobalTempoEvents(const std::string& filename);
void DrawVisualizerNotes(const std::vector<TrackData>& tracks, int currentTick, int ppq, std::vector<size_t>& searchStartIndices, uint32_t currentTempo);
