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

// ===== MIDI Loader Function (Dummy or Real) =====

bool loadVisualizerMidiData(const std::string& filename, std::vector<TrackData>& tracks, int& ppq);
