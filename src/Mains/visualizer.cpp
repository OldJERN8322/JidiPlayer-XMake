// visualizer.cpp (With Pitch Bend and Restart Feature)

#include "visualizer.hpp"
#include "midi_timing_alt.hpp"
#include <fstream>
#include <iostream>
#include <algorithm>
#include <random>
#include <chrono>
#include <map>
#include <vector>
#include <cstdint>
#include <cstring>
#include <queue>
#include <tuple>
#include "raylib.h"

// ===================================================================
// PLATFORM AND EXTERNAL DEFS
// ===================================================================
#ifdef _WIN32
inline uint32_t ntohl(uint32_t n) { return ((n & 0xFF000000) >> 24) | ((n & 0x00FF0000) >> 8) | ((n & 0x0000FF00) << 8) | ((n & 0x000000FF) << 24); }
inline uint16_t ntohs(uint16_t n) { return ((n & 0xFF00) >> 8) | ((n & 0x00FF) << 8); }
#else
#include <arpa/inet.h>
#endif

extern "C" {
    bool InitializeKDMAPIStream();
    void TerminateKDMAPIStream();
    void SendDirectData(unsigned long data);
}

// Global state variables
static bool showNoteOutlines = false; // Toggle for note borders/outlines
static bool showNoteGlow = true; // Toggle for note glow
static bool showGuide = true; // Toggle for guide
static bool showDebug = false; // Toggle for debug
static AppState currentState = STATE_MENU;
static std::string selectedMidiFile = "test.mid"; 
float ScrollSpeed = 0.5f;

// Other variables
float DWidth = 300.0f, DHeight = 115.0f;
uint64_t noteCounter = 0;

// ===================================================================
// IMPROVED COLOR MANAGEMENT
// ===================================================================

// Keep the original colors as a reference
static Color originalColors[] = { 
    MCOLOR1, MCOLOR2, MCOLOR3, MCOLOR4, MCOLOR5, MCOLOR6, MCOLOR7, MCOLOR8, MCOLOR9, MCOLOR10, MCOLOR11, MCOLOR12, MCOLOR13, MCOLOR14, MCOLOR15, MCOLOR16 
};

// Create a separate array for current track colors
static Color currentTrackColors[16];
static bool colorsInitialized = false;

// Initialize colors on first use
void InitializeTrackColors() {
    if (!colorsInitialized) {
        for (int i = 0; i < 16; i++) {
            currentTrackColors[i] = originalColors[i];
        }
        colorsInitialized = true;
    }
}

// Get color for a specific track/channel
inline Color GetTrackColorPFA(int channel) {
    InitializeTrackColors();
    return currentTrackColors[channel % 16];
}

// Improved randomization function
void RandomizeTrackColors() {
    InitializeTrackColors();
    
    // Create a copy of original colors to shuffle
    std::vector<Color> colorPool;
    for (int i = 0; i < 16; i++) {
        colorPool.push_back(originalColors[i]);
    }
    
    // Shuffle the color pool
    std::random_device rd;
    std::mt19937 g(rd());
    std::shuffle(colorPool.begin(), colorPool.end(), g);
    
    // Assign shuffled colors to channels
    for (int i = 0; i < 16; i++) {
        currentTrackColors[i] = colorPool[i];
    }
    
    std::cout << "- Channel color change to randomized" << std::endl;
}

// Optional: Reset colors to original
void ResetTrackColors() {
    for (int i = 0; i < 16; i++) {
        currentTrackColors[i] = originalColors[i];
    }
    std::cout << "- Channel color change to default" << std::endl;
}

// Alternative: Generate completely random colors
void GenerateRandomTrackColors() {
    InitializeTrackColors();
    std::random_device rd;
    std::mt19937 g(rd());
    std::uniform_int_distribution<int> colorDist(0, 255); // Avoid too dark colors
    
    for (int i = 0; i < 16; i++) {
        currentTrackColors[i] = {
            static_cast<unsigned char>(colorDist(g)),
            static_cast<unsigned char>(colorDist(g)),
            static_cast<unsigned char>(colorDist(g)),
            255
        };
    }
    
    std::cout << "- Channel color change to Generate random" << std::endl;
}

// ===================================================================
// GUI FUNCTIONS
// ===================================================================

bool DrawButton(Rectangle bounds, const char* text) {
    bool isHovered = CheckCollisionPointRec(GetMousePosition(), bounds);
    DrawRectangleRec(bounds, isHovered ? GRAY : JGRAY);
    DrawRectangleLinesEx(bounds, 2, DARKGRAY);
    int textWidth = MeasureText(text, 20);
    DrawText(text, (int)(bounds.x + (bounds.width - textWidth) / 2), (int)(bounds.y + (bounds.height - 20) / 2), 20, WHITE);
    return isHovered && IsMouseButtonPressed(MOUSE_BUTTON_LEFT);
}
void DrawModeSelectionMenu() {
    ClearBackground(JGRAY);
    DrawText("JIDI Player", GetScreenWidth() / 2 - MeasureText("JIDI Player", 40) / 2, 50, 40, WHITE);
    if (DrawButton({(float)GetScreenWidth() / 2 - 150, 200, 300, 50}, "Select File (Cycle)")) {
        if (selectedMidiFile == "test.mid") selectedMidiFile = "TACMERGEFINAL.mid";
        else if (selectedMidiFile == "TACMERGEFINAL.mid") selectedMidiFile = "tau2.5.9.mid";
        else selectedMidiFile = "test.mid";
    }
    DrawText(TextFormat("File: %s", GetFileName(selectedMidiFile.c_str())), GetScreenWidth()/2 - MeasureText(TextFormat("File: %s", GetFileName(selectedMidiFile.c_str())), 20)/2, 260, 20, LIGHTGRAY);
    if (DrawButton({(float)GetScreenWidth() / 2 - 150, 300, 300, 50}, "Start Playback")) {
        currentState = STATE_LOADING;
    }
}
void DrawLoadingScreen() {
    ClearBackground(JGRAY);
    DrawText("Loading File...", GetScreenWidth() / 2 - MeasureText("Loading File...", 40) / 2, 200, 40, WHITE);
    DrawText("Memory usage optimized", GetScreenWidth() / 2 - MeasureText("Memory usage optimized", 20) / 2, 250, 20, LIGHTGRAY);
}

// ===================================================================
// MIDI LOADER
// ===================================================================
uint32_t readVarLen(const std::vector<uint8_t>& data, size_t& pos) {
    uint32_t value = 0;
    uint8_t byte;
    if (pos >= data.size()) return 0;
    do {
        byte = data[pos++];
        value = (value << 7) | (byte & 0x7F);
    } while ((byte & 0x80) && (pos < data.size()));
    return value;
}

bool loadMidiFile(const std::string& filename, std::vector<OptimizedTrackData>& noteTracks, std::vector<MidiEvent>& eventList, int& ppq) {
    noteTracks.assign(16, OptimizedTrackData());
    eventList.clear();
    std::ifstream file(filename, std::ios::binary);
    if (!file.is_open()) return false;

    char header[14];
    file.read(header, 14);
    if (!file || strncmp(header, "MThd", 4) != 0) return false;
    
    uint16_t nTracks = ntohs(*reinterpret_cast<uint16_t*>(header + 10));
    ppq = ntohs(*reinterpret_cast<uint16_t*>(header + 12));
    if (ppq <= 0) ppq = 480;

    std::map<uint8_t, NoteEvent> activeNotes[16];

    for (uint16_t t = 0; t < nTracks; ++t) {
        char chunkHeader[8];
        file.read(chunkHeader, 8);
        if (!file || strncmp(chunkHeader, "MTrk", 4) != 0) continue;
        uint32_t length = ntohl(*reinterpret_cast<uint32_t*>(chunkHeader + 4));
        std::vector<uint8_t> trackData(length);
        file.read(reinterpret_cast<char*>(trackData.data()), length);

        size_t pos = 0;
        uint32_t tick = 0;
        uint8_t runningStatus = 0;

        while (pos < trackData.size()) {
            tick += readVarLen(trackData, pos);
            if (pos >= trackData.size()) break;

            uint8_t status = trackData[pos];
            if (status < 0x80) { status = runningStatus; } 
            else { pos++; runningStatus = status; }

            uint8_t eventType = status & 0xF0;
            uint8_t channel = status & 0x0F;

            if (eventType == 0x90 && pos + 1 < trackData.size() && trackData[pos+1] > 0) { // Note On
                uint8_t note = trackData[pos];
                uint8_t vel = trackData[pos+1];
                activeNotes[channel][note] = { tick, 0, note, vel, channel };
                pos += 2;
            } else if (eventType == 0x80 || (eventType == 0x90 && pos + 1 < trackData.size() && trackData[pos+1] == 0)) { // Note Off
                uint8_t note = trackData[pos];
                auto it = activeNotes[channel].find(note);
                if (it != activeNotes[channel].end()) {
                    it->second.endTick = tick;
                    noteTracks[channel].notes.push_back(it->second);
                    activeNotes[channel].erase(it);
                }
                pos += 2;
            } else if (eventType == 0xB0 && pos + 1 < trackData.size()) { // Control Change
                eventList.push_back({tick, EventType::CC, channel, trackData[pos], trackData[pos+1], 0});
                pos += 2;
            } else if (eventType == 0xE0 && pos + 1 < trackData.size()) { // Pitch Bend
                eventList.push_back({tick, EventType::PITCH_BEND, channel, trackData[pos], trackData[pos+1], 0});
                pos += 2;
            } else if (status == 0xFF) { // Meta Event
                uint8_t metaType = trackData[pos++];
                uint32_t len = readVarLen(trackData, pos);
                if (metaType == 0x51 && len == 3) { // Tempo Change
                    uint32_t tempo = (trackData[pos] << 16) | (trackData[pos + 1] << 8) | trackData[pos + 2];
                    eventList.push_back({tick, EventType::TEMPO, 0, 0, 0, tempo});
                }
                pos += len;
            } else { // Other events to skip
                if (eventType == 0xC0 || eventType == 0xD0) pos += 1;
                else if (eventType == 0xA0) pos += 2;
                else if (status == 0xF0 || status == 0xF7) pos += readVarLen(trackData, pos);
                else if (pos < trackData.size()) pos++;
            }
        }
    }
    for (const auto& track : noteTracks) {
        for (const auto& note : track.notes) {
            eventList.push_back({note.startTick, EventType::NOTE_ON, note.channel, note.note, note.velocity, 0});
            eventList.push_back({note.endTick, EventType::NOTE_OFF, note.channel, note.note, 0, 0});
        }
    }
    std::sort(eventList.begin(), eventList.end());
    for (auto& track : noteTracks) {
        std::sort(track.notes.begin(), track.notes.end(), [](const NoteEvent& a, const NoteEvent& b){ return a.startTick < b.startTick; });
    }
    return true;
}

// ===================================================================
// IMPROVED VISUALIZER
// ===================================================================
void DrawStreamingVisualizerNotes(const std::vector<OptimizedTrackData>& tracks, uint64_t currentTick, int ppq, uint32_t currentTempo) {
    int screenWidth = GetScreenWidth();
    int screenHeight = GetScreenHeight();
    double microsecondsPerTick = MidiTiming::CalculateMicrosecondsPerTick(MidiTiming::DEFAULT_TEMPO_MICROSECONDS, ppq);
    
    // Increased view window for better note visibility
    const uint32_t viewWindow = std::max(1U, static_cast<uint32_t>((ScrollSpeed * 1250000.0) / microsecondsPerTick));
    
    // Draw a reference line at the current playback position
    int playbackLine = screenWidth / 2; // Notes will approach this line (centered)
    
    for (int trackIndex = 0; trackIndex < (int)tracks.size(); ++trackIndex) {
        const auto& track = tracks[trackIndex];
        if (track.notes.empty()) continue;
        
        // Find notes in the visible range more efficiently
        auto startIt = std::lower_bound(track.notes.begin(), track.notes.end(), 
            (currentTick > viewWindow) ? (currentTick - viewWindow) : 0, 
            [](const NoteEvent& note, uint64_t tick) { 
                return note.endTick < tick; 
            });
        
        for (auto it = startIt; it != track.notes.end(); ++it) {
            const NoteEvent& note = *it;
            
            // Skip notes that are too far in the future
            if (note.startTick > currentTick + viewWindow) break;
            
            // Calculate note position (notes move from right to left)
            float startX = playbackLine + ((float)((int64_t)note.startTick - (int64_t)currentTick) / (float)viewWindow) * (screenWidth - playbackLine);
            float endX = playbackLine + ((float)((int64_t)note.endTick - (int64_t)currentTick) / (float)viewWindow) * (screenWidth - playbackLine);
            
            float width = endX - startX;
            if (width < 2.0f) width = 2.0f; // Minimum width for visibility
            
            // Skip notes that are completely off-screen
            if (startX > screenWidth || endX < 0) continue;
            
            // Full 128 MIDI keys mapping
            float normalizedNote = (note.note + 1) / 128.0f; // Fixed by down issues keys, Full 128 keys is perfect
            
            float y = screenHeight - (normalizedNote * screenHeight); // Use full screen height
            
            // Scale note height based on screen height and key density
            float height = std::max(1.0f, screenHeight / 128.0f); // Scale height for 128 keys
            
            // Check if note is currently playing
            bool isActive = (note.startTick <= currentTick && note.endTick > currentTick);
            
            // Get track color - no transparency to avoid overlap issues
            Color noteColor = GetTrackColorPFA(note.channel);
            
            // Glow notes
            if (isActive && showNoteGlow) {
                noteColor = {255, 255, 255, 255};
            }
            
            // Draw the note
            DrawRectangleRec({startX, y, width, height}, noteColor);
            
            // Add a border for better definition (toggleable with V)
            if (showNoteOutlines && width > 1.0f && height > 2.0f) {
                DrawRectangleLinesEx({startX, y, width, height}, 1.0f, {0, 0, 0, 128});
            }
        }
    }

    // Draw guidelines at important MIDI keys
    if (showGuide) {
        int importantKeys[] = {0, 12, 24, 36, 48, 60, 72, 84, 96, 108, 120}; // C notes
        for (int i = 0; i < 12; ++i) {
            int key = importantKeys[i];
            float normalizedNote = key / 128.0f;
            float y = screenHeight - (normalizedNote * screenHeight);
            
            // Highlight middle C (60) differently
            Color lineColor = (key == 60) ? Color{255, 255, 128, 64} : Color{128, 128, 128, 64};
            DrawLine(0, (int)y, screenWidth, (int)y, lineColor);
            
            // Add key labels for important notes
            if (key == 60) {
                DrawText("C4 (60)", 5, (int)y - 10, 10, Color{255, 255, 128, 192});
            } else if (key % 12 == 0 && key > 0) {
                DrawText(TextFormat("C%d (%d)", (key / 12) - 1, key), 5, (int)y - 10, 10, Color{255, 255, 255, 128});
            }
        }
    }

    DrawLine(playbackLine, 0, playbackLine, screenHeight, {255, 255, 192, 128});
}

void DrawDebugPanel(uint64_t currentVisualizerTick, int ppq, uint32_t currentTempo, 
                   size_t eventListPos, size_t totalEvents, bool isPaused, 
                   float scrollSpeed, const std::vector<OptimizedTrackData>& tracks) {
    
    // Debug panel dimensions
    float panelX = (GetScreenWidth() - DWidth) - 10.0f;
    float panelY = 40.0f;
    float lineHeight = 12.0f;
    float padding = 10.0f;
    
    // Draw debug panel background
    DrawRectangleRounded(Rectangle{panelX, panelY, DWidth, DHeight}, 0.25f, 0, Color{64, 64, 64, 128});
    
    // Title
    DrawText("Debug Info", (int)(panelX + padding), (int)(panelY + padding), 20, WHITE);
    
    float currentY = panelY + padding + 25.0f;
    
    // Playback status
    const char* statusText = isPaused ? "PAUSED" : "PLAYING";
    Color statusColor = isPaused ? RED : GREEN;
    DrawText(TextFormat("Playback status: %s", statusText), (int)(panelX + padding), (int)currentY, 15, statusColor);
    currentY += lineHeight + 10.0f;
    
    // Ticks and PPQ
    DrawText(TextFormat("Ticks: %llu (PPQ: %d)", currentVisualizerTick, ppq), (int)(panelX + padding), (int)currentY, 10, WHITE);
    currentY += lineHeight;
    
    // Tempo
    DrawText(TextFormat("Tempo: %u us", currentTempo), (int)(panelX + padding), (int)currentY, 10, WHITE);
    currentY += lineHeight;
    
    // Event progress
    float progress = totalEvents > 0 ? ((float)eventListPos / (float)totalEvents) * 100.0f : 0.0f;
    DrawText(TextFormat("Event: %zu / %zu (%.3f%%)", eventListPos, totalEvents, progress), (int)(panelX + padding), (int)currentY, 10, WHITE);
    currentY += lineHeight;
    
    // Scroll speed
    DrawText(TextFormat("Scroll speed: %.2fx", scrollSpeed), (int)(panelX + padding), (int)currentY, 10, WHITE);
    currentY += lineHeight;
}

// ===================================================================
// PLAYBACK RESET FUNCTION
// ===================================================================
void ResetPlayback(
    const std::vector<MidiEvent>& eventList,
    int ppq,
    std::chrono::steady_clock::time_point& playbackStartTime,
    std::chrono::steady_clock::time_point& pauseTime,
    uint64_t& totalPausedTime,
    bool& isPaused,
    uint32_t& currentTempo,
    double& microsecondsPerTick,
    uint64_t& currentVisualizerTick,
    uint32_t& lastProcessedTick,
    uint64_t& accumulatedMicroseconds,
    size_t& eventListPos) 
{
    // Send MIDI reset messages to all channels to prevent stuck notes
    for (int ch = 0; ch < 16; ++ch) {
        SendDirectData((0xB0 | ch) | (123 << 8)); // All notes off
        SendDirectData((0xB0 | ch) | (121 << 8)); // Reset all controllers
    }

    // Reset all timing and state variables to their initial values
    playbackStartTime = std::chrono::steady_clock::now();
    pauseTime = playbackStartTime;
    totalPausedTime = 0;
    noteCounter = 0;
    isPaused = false;
    currentVisualizerTick = 0;
    lastProcessedTick = 0;
    accumulatedMicroseconds = 0;
    eventListPos = 0;

    // Reset the tempo to the file's initial tempo
    currentTempo = MidiTiming::DEFAULT_TEMPO_MICROSECONDS;
    if (!eventList.empty() && eventList[0].type == EventType::TEMPO) {
        currentTempo = eventList[0].tempo;
    }
    microsecondsPerTick = MidiTiming::CalculateMicrosecondsPerTick(currentTempo, ppq);
    
    std::cout << "Playback Restarted" << std::endl;
}

// ===================================================================
// MAIN FUNCTION
// ===================================================================
int main(int argc, char* argv[]) {
    std::cout << "Starting..." << std::endl;
    if (argc > 1) {
        selectedMidiFile = argv[1];
        std::cout << "File selection alived!" << std::endl;
    }
    SetConfigFlags(FLAG_WINDOW_RESIZABLE);
    InitWindow(1280, 720, "JIDI Player");
    
    std::vector<OptimizedTrackData> noteTracks;
    std::vector<MidiEvent> eventList;
    size_t eventListPos = 0;
    int ppq = 480;
    
    auto playbackStartTime = std::chrono::steady_clock::now();
    auto pauseTime = std::chrono::steady_clock::now();
    uint64_t totalPausedTime = 0;
    uint32_t currentTempo = MidiTiming::DEFAULT_TEMPO_MICROSECONDS;
    double microsecondsPerTick = MidiTiming::CalculateMicrosecondsPerTick(currentTempo, ppq);
    bool isPaused = false;
    uint64_t currentVisualizerTick = 0;
    uint32_t lastProcessedTick = 0;
    uint64_t accumulatedMicroseconds = 0;

    while (!WindowShouldClose()) {
        switch (currentState) {
            case STATE_MENU: {
                BeginDrawing();
                DrawModeSelectionMenu();
                EndDrawing();
                break;
            }
            case STATE_LOADING: {
                BeginDrawing();
                DrawLoadingScreen();
                EndDrawing();
                
                if (InitializeKDMAPIStream()) {
                    std::cout << "Midi selection: " << selectedMidiFile << std::endl;
                    std::cout << "Please wait..." << std::endl;
                    loadMidiFile(selectedMidiFile, noteTracks, eventList, ppq);
                    
                    ResetPlayback(eventList, ppq, playbackStartTime, pauseTime, totalPausedTime, isPaused, 
                                  currentTempo, microsecondsPerTick, currentVisualizerTick, lastProcessedTick, 
                                  accumulatedMicroseconds, eventListPos);

                    std::cout << "--- Help controller ---" << std::endl;

                    std::cout << "--- Playback ---" << std::endl;
                    std::cout << "BACKSPACE = Return menu" << std::endl;
                    std::cout << "SPACE = Pause / Resume" << std::endl;
                    std::cout << "R = Restart playback" << std::endl;

                    std::cout << "--- Render ---" << std::endl;
                    std::cout << "UP (Hold), RIGHT (Pressed) = Slower scroll speed (+0.05x)" << std::endl;
                    std::cout << "DOWN (Hold), LEFT (Pressed) = Faster scroll speeds (-0.05x)" << std::endl;
                    std::cout << "N = Toggle outline notes (More notes = Lag)" << std::endl;
                    std::cout << "G = Toggle glow notes" << std::endl;
                    std::cout << "V = Toggle guide" << std::endl;

                    std::cout << "--- Color ---" << std::endl;
                    std::cout << "C = Randomize track colors" << std::endl;
                    std::cout << "X = Reset track colors to original" << std::endl; 
                    std::cout << "Z = Generate completely random colors" << std::endl;

                    std::cout << "--- Debug ---" << std::endl;
                    std::cout << "CTRL (Control) = Show debug" << std::endl << std::endl;
                    
                    std::cout << "- Scroll speed default set: " << ScrollSpeed << "x" << std::endl;
                    std::cout << "+ Midi loaded!" << std::endl << std::endl;
                    
                    currentState = STATE_PLAYING;
                    SetWindowTitle(TextFormat("JIDI Player - %s", GetFileName(selectedMidiFile.c_str())));
                } else {
                    currentState = STATE_MENU;
                } 
                break;
            }
            case STATE_PLAYING: {
                if (IsKeyPressed(KEY_R)) {
                    ResetPlayback(eventList, ppq, playbackStartTime, pauseTime, totalPausedTime, isPaused, 
                                  currentTempo, microsecondsPerTick, currentVisualizerTick, lastProcessedTick, 
                                  accumulatedMicroseconds, eventListPos);
                }
                if (IsKeyPressed(KEY_SPACE)) {
                    isPaused = !isPaused;
                    if (isPaused) {
                        pauseTime = std::chrono::steady_clock::now();
                    } else {
                        totalPausedTime += std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now() - pauseTime).count();
                    }
                }
                if (IsKeyPressed(KEY_BACKSPACE)) { std::cout << "Returning menu..." << std::endl; currentState = STATE_MENU; TerminateKDMAPIStream(); continue; }
                if (IsKeyDown(KEY_DOWN)) { ScrollSpeed = std::max(0.05f, ScrollSpeed - 0.05f); }
                if (IsKeyDown(KEY_UP)) { ScrollSpeed += 0.05f; }
                if (IsKeyPressed(KEY_LEFT)) { ScrollSpeed = std::max(0.05f, ScrollSpeed - 0.05f); std::cout << "- Scroll speed changed to " << ScrollSpeed << "x" << std::endl; }
                if (IsKeyPressed(KEY_RIGHT)) { ScrollSpeed += 0.05f; std::cout << "+ Scroll speed changed to " << ScrollSpeed << "x" << std::endl; }
                if (IsKeyPressed(KEY_N)) { 
                    showNoteOutlines = !showNoteOutlines; 
                    std::cout << "- Note outlines " << (showNoteOutlines ? "enabled" : "disabled") << std::endl; }
                if (IsKeyPressed(KEY_G)) { 
                    showNoteGlow = !showNoteGlow; 
                    std::cout << "- Note glow " << (showNoteGlow ? "enabled" : "disabled") << std::endl; }
                if (IsKeyPressed(KEY_V)) { 
                    showGuide = !showGuide; 
                    std::cout << "- Guide " << (showGuide ? "enabled" : "disabled") << std::endl; }
                if (IsKeyPressed(KEY_C)) { RandomizeTrackColors(); }
                if (IsKeyPressed(KEY_X)) { ResetTrackColors(); }
                if (IsKeyPressed(KEY_Z)) { GenerateRandomTrackColors(); }
                if (IsKeyPressed(KEY_LEFT_CONTROL)) { showDebug = !showDebug; 
                    std::cout << "- Debug " << (showDebug ? "enabled" : "disabled") << std::endl; }

                if (!isPaused) {
                    uint64_t elapsedMicroseconds = std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now() - playbackStartTime).count() - totalPausedTime;
                    
                    while (eventListPos < eventList.size()) {
                        const auto& event = eventList[eventListPos];
                        uint64_t scheduledTime = accumulatedMicroseconds + (uint64_t)((event.tick - lastProcessedTick) * microsecondsPerTick);
                        
                        if (elapsedMicroseconds >= scheduledTime) {
                            accumulatedMicroseconds = scheduledTime;
                            lastProcessedTick = event.tick;
                            
                            if (event.type == EventType::TEMPO) {
                                currentTempo = event.tempo;
                                microsecondsPerTick = MidiTiming::CalculateMicrosecondsPerTick(currentTempo, ppq);
                            } else if (event.type == EventType::CC) {
                                SendDirectData((0xB0 | event.channel) | (event.data1 << 8) | (event.data2 << 16));
                            } else if (event.type == EventType::PITCH_BEND) {
                                SendDirectData((0xE0 | event.channel) | (event.data1 << 8) | (event.data2 << 16));
                            } else {
                                uint8_t status = (event.type == EventType::NOTE_ON) ? (0x90 | event.channel) : (0x80 | event.channel);
                                SendDirectData(status | (event.data1 << 8) | (event.data2 << 16));

                                if (event.type == EventType::NOTE_ON && event.data2 > 0) {
                                    noteCounter++;
                                }
                            }
                            eventListPos++;
                        } else {
                            break; 
                        }
                    }
                }

                if (!isPaused) {
                    uint64_t visualizerElapsed = std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now() - playbackStartTime).count() - totalPausedTime;
                    uint64_t microsSinceLastEvent = (visualizerElapsed > accumulatedMicroseconds) ? visualizerElapsed - accumulatedMicroseconds : 0;
                    
                    if (microsecondsPerTick > 0) {
                        currentVisualizerTick = lastProcessedTick + (uint64_t)(microsSinceLastEvent / microsecondsPerTick);
                    } else {
                        currentVisualizerTick = lastProcessedTick;
                    }
                }
                
                BeginDrawing();
                ClearBackground(JBLACK);
                DrawStreamingVisualizerNotes(noteTracks, currentVisualizerTick, ppq, currentTempo);
                DrawText(TextFormat("Notes: %llu", noteCounter), 10, 10, 20, JLIGHTBLUE);
                DrawText(TextFormat("%.3f BPM", MidiTiming::MicrosecondsToBPM(currentTempo)), 10, 35, 15, JLIGHTBLUE);
                if(isPaused) DrawText("PAUSED", GetScreenWidth()/2 - MeasureText("PAUSED", 20)/2, 20, 20, RED);

                if (showDebug) {
                    DrawDebugPanel(currentVisualizerTick, ppq, currentTempo, eventListPos, eventList.size(), isPaused, ScrollSpeed, noteTracks);
                }

                DrawText(TextFormat("FPS: %llu", GetFPS()), (GetScreenWidth() - MeasureText(TextFormat("FPS: %llu", GetFPS()), 20)) - 10, 10, 20, JLIGHTLIME);
                EndDrawing();
                break;
            }
        }
    }
    std::cout << "Exiting..." << std::endl;
    TerminateKDMAPIStream();
    CloseWindow();
    return 0;
}
