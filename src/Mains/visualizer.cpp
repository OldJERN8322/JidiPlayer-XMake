// visualizer.cpp

#include "visualizer.hpp"
#include "midi_timing.hpp"
#include <fstream>
#include <iostream>
#include <algorithm> // Required for std::sort
#include <chrono>    // Required for high-precision timer
#include <map>       // Required for note tracking
#include <vector>
#include <cstdint>
#include <thread>
#include <cstring>

// Include raylib after other headers to avoid conflicts
#include "raylib.h"

// Note: Windows file dialog removed due to header conflicts with raylib
// Will implement simple file cycling for now

// Define byte order conversion functions for Windows
#ifdef _WIN32
inline uint32_t ntohl(uint32_t netlong) {
    return ((netlong & 0xFF000000) >> 24) |
           ((netlong & 0x00FF0000) >> 8) |
           ((netlong & 0x0000FF00) << 8) |
           ((netlong & 0x000000FF) << 24);
}

inline uint16_t ntohs(uint16_t netshort) {
    return ((netshort & 0xFF00) >> 8) |
           ((netshort & 0x00FF) << 8);
}
#else
#include <arpa/inet.h>
#endif

extern "C" {
    bool InitializeKDMAPIStream();
    void TerminateKDMAPIStream();
    void SendDirectData(unsigned long data);
}

// Render modes
enum RenderMode {
    RENDER_DEFAULT = 0,  // All tracks combined into one view
    RENDER_TRACKS = 1    // Multi-track view (16 tracks)
};

// Application state
enum AppState {
    STATE_MENU = 0,      // Mode selection menu
    STATE_LOADING = 1,   // Loading MIDI file with progress
    STATE_PLAYING = 2    // MIDI playback and visualization
};

// Global state variables
static AppState currentState = STATE_MENU;
static RenderMode selectedRenderMode = RENDER_DEFAULT;
static std::string selectedMidiFile = "test.mid";
static bool showFileDialog = false;

// Global variables for performance optimization
static std::vector<size_t> searchStartIndices;  // This will hold the index for each track
//static size_t masterSearchIndex = 0; // Master search index for optimization

// Performance monitoring variables
static float frameTimeHistory[60] = {0}; // Store last 60 frame times
static int frameTimeIndex = 0;
static bool adaptiveQualityEnabled = true;
static float qualityScale = 1.0f; // 1.0 = full quality, 0.5 = half quality, etc.

// Scroll speed control
static float scrollSpeedMultiplier = 1.0f; // 1.0 = normal speed, 2.0 = double speed, 0.5 = half speed

// Loading progress variables
static std::string loadingStatus = "";
static int loadingProgress = 0;
static int totalTracks = 0;
static int processedTracks = 0;
static int totalEvents = 0;
static int processedEvents = 0;

// Performance monitoring and adaptive quality control
void UpdatePerformanceMetrics() {
    float frameTime = GetFrameTime();
    frameTimeHistory[frameTimeIndex] = frameTime;
    frameTimeIndex = (frameTimeIndex + 1) % 60;

    if (!adaptiveQualityEnabled) return;

    // Calculate average frame time over last 60 frames
    float avgFrameTime = 0.0f;
    for (int i = 0; i < 60; i++) {
        avgFrameTime += frameTimeHistory[i];
    }
    avgFrameTime /= 60.0f;

    float targetFrameTime = 1.0f / 60.0f; // Target 60 FPS
    float currentFPS = 1.0f / avgFrameTime;

    // Adjust quality based on performance
    if (currentFPS < 30.0f && qualityScale > 0.25f) {
        // Performance is poor, reduce quality
        qualityScale = std::max(0.25f, qualityScale - 0.05f);
    } else if (currentFPS > 55.0f && qualityScale < 1.0f) {
        // Performance is good, increase quality
        qualityScale = std::min(1.0f, qualityScale + 0.02f);
    }
}

// Simple file cycling function (replaces Windows file dialog for now)
std::string CycleTestFiles(const std::string& currentFile) {
    if (currentFile == "test.mid") {
        return "test1.mid";
    } else if (currentFile == "test1.mid") {
        return "test2.mid";
    } else if (currentFile == "test2.mid") {
        return "test3.mid";
    } else if (currentFile == "test3.mid") {
        return "test4.mid";
    } else {
        return "test.mid";
    }
}

// Simple button helper function
bool DrawButton(Rectangle bounds, const char* text, bool selected = false) {
    Vector2 mousePos = GetMousePosition();
    bool isHovered = CheckCollisionPointRec(mousePos, bounds);
    bool isClicked = isHovered && IsMouseButtonPressed(MOUSE_BUTTON_LEFT);

    Color bgColor = selected ? DARKGREEN : (isHovered ? GRAY : DARKGRAY);
    Color borderColor = selected ? GREEN : (isHovered ? LIGHTGRAY : GRAY);
    Color textColor = WHITE;

    DrawRectangleRec(bounds, bgColor);
    DrawRectangleLinesEx(bounds, 2, borderColor);

    int textWidth = MeasureText(text, 20);
    int textX = (int)(bounds.x + (bounds.width - textWidth) / 2);
    int textY = (int)(bounds.y + (bounds.height - 20) / 2);
    DrawText(text, textX, textY, 20, textColor);

    return isClicked;
}

// Menu GUI functions
void DrawModeSelectionMenu() {
    int screenWidth = GetScreenWidth();
    int screenHeight = GetScreenHeight();

    // Clear background
    ClearBackground(DARKGRAY);

    // Title
    const char* title = "JIDI MIDI Player - Mode Selection";
    int titleWidth = MeasureText(title, 40);
    DrawText(title, (screenWidth - titleWidth) / 2, 50, 40, WHITE);

    // Mode selection area
    Rectangle modePanel = {
        (float)(screenWidth / 2 - 200),
        150,
        400,
        300
    };

    // Draw mode panel background
    DrawRectangleRec(modePanel, Color{40, 40, 40, 255});
    DrawRectangleLinesEx(modePanel, 2, LIGHTGRAY);
    DrawText("Render Mode", (int)modePanel.x + 10, (int)modePanel.y + 10, 20, WHITE);

    // Mode selection buttons
    Rectangle defaultModeBtn = { modePanel.x + 20, modePanel.y + 40, 360, 60 };
    Rectangle tracksModeBtn = { modePanel.x + 20, modePanel.y + 120, 360, 60 };

    // Default mode button
    bool isDefaultSelected = (selectedRenderMode == RENDER_DEFAULT);
    const char* defaultText = isDefaultSelected ? "✓ Default Mode (Combined View)" : "Default Mode (Combined View)";
    if (DrawButton(defaultModeBtn, defaultText, isDefaultSelected)) {
        selectedRenderMode = RENDER_DEFAULT;
    }

    // Tracks mode button
    bool isTracksSelected = (selectedRenderMode == RENDER_TRACKS);
    const char* tracksText = isTracksSelected ? "✓ Tracks Mode (16 Track View)" : "Tracks Mode (16 Track View)";
    if (DrawButton(tracksModeBtn, tracksText, isTracksSelected)) {
        selectedRenderMode = RENDER_TRACKS;
    }

    // Description text
    const char* description = "";
    if (selectedRenderMode == RENDER_DEFAULT) {
        description = "All tracks combined into a single piano roll view.\nBetter for seeing overall composition.";
    } else {
        description = "Individual track lanes with 16 track limit.\nBetter for seeing track separation and mixing.";
    }

    DrawText(description, (int)modePanel.x + 20, (int)modePanel.y + 200, 16, LIGHTGRAY);

    // File selection area
    Rectangle filePanel = {
        (float)(screenWidth / 2 - 200),
        480,
        400,
        120
    };

    // Draw file panel background
    DrawRectangleRec(filePanel, Color{40, 40, 40, 255});
    DrawRectangleLinesEx(filePanel, 2, LIGHTGRAY);
    DrawText("MIDI File", (int)filePanel.x + 10, (int)filePanel.y + 10, 20, WHITE);

    DrawText(TextFormat("Selected: %s", selectedMidiFile.c_str()), (int)filePanel.x + 20, (int)filePanel.y + 40, 16, WHITE);

    // Show file complexity preview if available
    // Quick analysis for UI preview (simplified)
    std::ifstream quickCheck(selectedMidiFile, std::ios::binary);
    if (quickCheck.is_open()) {
        char header[14];
        quickCheck.read(header, 14);
        if (quickCheck.gcount() == 14 && std::string(header, 4) == "MThd") {
            uint16_t tracks = (static_cast<uint8_t>(header[10]) << 8) | static_cast<uint8_t>(header[11]);
            uint16_t ppq = (static_cast<uint8_t>(header[12]) << 8) | static_cast<uint8_t>(header[13]);

            Color complexityColor = (ppq > 10000 || tracks > 40) ? RED : (ppq > 2000 || tracks > 20) ? YELLOW : GREEN;
            DrawText(TextFormat("Tracks: %d, PPQ: %d", tracks, ppq), (int)filePanel.x + 20, (int)filePanel.y + 60, 14, complexityColor);

            const char* complexityText = (ppq > 10000 || tracks > 40) ? "HIGH COMPLEXITY" :
                                        (ppq > 2000 || tracks > 20) ? "MEDIUM COMPLEXITY" : "LOW COMPLEXITY";
            DrawText(complexityText, (int)filePanel.x + 20, (int)filePanel.y + 80, 12, complexityColor);
        }
        quickCheck.close();
    }

    Rectangle fileBrowseBtn = { filePanel.x + 310, filePanel.y + 40, 70, 30 };
    if (DrawButton(fileBrowseBtn, "Browse")) {
        selectedMidiFile = CycleTestFiles(selectedMidiFile);
    }

    // Start button
    Rectangle startBtn = {
        (float)(screenWidth / 2 - 100),
        630,
        200,
        50
    };

    if (DrawButton(startBtn, "Start Playback")) {
        currentState = STATE_LOADING;
        loadingProgress = 0;
        loadingStatus = "Initializing...";
    }

    // Instructions
    const char* instructions = "Select render mode and MIDI file, then click Start Playback";
    int instrWidth = MeasureText(instructions, 20);
    DrawText(instructions, (screenWidth - instrWidth) / 2, screenHeight - 50, 20, LIGHTGRAY);
}

// Loading screen with progress
void DrawLoadingScreen() {
    int screenWidth = GetScreenWidth();
    int screenHeight = GetScreenHeight();

    ClearBackground(DARKGRAY);

    // Title
    const char* title = "Loading MIDI File...";
    int titleWidth = MeasureText(title, 40);
    DrawText(title, (screenWidth - titleWidth) / 2, 100, 40, WHITE);

    // File name
    DrawText(TextFormat("File: %s", selectedMidiFile.c_str()),
             (screenWidth - MeasureText(selectedMidiFile.c_str(), 20)) / 2, 160, 20, LIGHTGRAY);

    // Mode
    const char* modeText = selectedRenderMode == RENDER_DEFAULT ? "Default Mode" : "Tracks Mode";
    DrawText(TextFormat("Mode: %s", modeText),
             (screenWidth - MeasureText(modeText, 20)) / 2, 190, 20, LIGHTGRAY);

    // Progress bar background
    Rectangle progressBg = { (float)(screenWidth / 2 - 200), 250, 400, 30 };
    DrawRectangleRec(progressBg, DARKGRAY);
    DrawRectangleLinesEx(progressBg, 2, LIGHTGRAY);

    // Progress bar fill
    if (loadingProgress > 0) {
        float fillWidth = (progressBg.width - 4) * (loadingProgress / 100.0f);
        Rectangle progressFill = { progressBg.x + 2, progressBg.y + 2, fillWidth, progressBg.height - 4 };
        DrawRectangleRec(progressFill, GREEN);
    }

    // Progress percentage
    DrawText(TextFormat("%d%%", loadingProgress),
             (int)(progressBg.x + progressBg.width / 2 - 15), (int)(progressBg.y + 5), 20, WHITE);

    // Status text
    DrawText(loadingStatus.c_str(),
             (screenWidth - MeasureText(loadingStatus.c_str(), 16)) / 2, 300, 16, YELLOW);

    // Detailed progress
    if (totalTracks > 0) {
        DrawText(TextFormat("Tracks: %d / %d", processedTracks, totalTracks),
                 screenWidth / 2 - 100, 330, 16, LIGHTGRAY);
    }

    if (totalEvents > 0) {
        DrawText(TextFormat("Events: %d", totalEvents),
                 screenWidth / 2 - 100, 350, 16, LIGHTGRAY);
    }

    // Cancel instruction
    DrawText("Press ESC to cancel",
             (screenWidth - MeasureText("Press ESC to cancel", 16)) / 2,
             screenHeight - 50, 16, LIGHTGRAY);
}

// Struct definitions moved to header file

// Function to validate MIDI file before loading
bool validateMidiFile(const std::string& filename) {
    std::ifstream file(filename, std::ios::binary);
    if (!file.is_open()) {
        std::cerr << "ERROR: Cannot open file " << filename << std::endl;
        return false;
    }

    // Check file size
    file.seekg(0, std::ios::end);
    std::streampos fileSize = file.tellg();
    file.seekg(0, std::ios::beg);

    if (fileSize < 14) {
        std::cerr << "ERROR: File too small to be a valid MIDI file" << std::endl;
        return false;
    }

    //if (fileSize > 100000000) { // 100MB limit
    //    std::cerr << "ERROR: File too large (over 100MB)" << std::endl;
    //    return false;
    //}

    // Check MIDI header
    char header[14];
    file.read(header, 14);
    if (std::string(header, 4) != "MThd") {
        std::cerr << "ERROR: Invalid MIDI file header" << std::endl;
        return false;
    }

    uint32_t headerLength = ntohl(*reinterpret_cast<uint32_t*>(header + 4));
    if (headerLength != 6) {
        std::cerr << "ERROR: Invalid MIDI header length: " << headerLength << std::endl;
        return false;
    }

    uint16_t format = ntohs(*reinterpret_cast<uint16_t*>(header + 8));
    uint16_t numTracks = ntohs(*reinterpret_cast<uint16_t*>(header + 10));
    uint16_t ppq = ntohs(*reinterpret_cast<uint16_t*>(header + 12));

    if (format > 2) {
        std::cerr << "ERROR: Unsupported MIDI format: " << format << std::endl;
        return false;
    }

    if (numTracks == 0 || numTracks > 1000) {
        std::cerr << "ERROR: Invalid number of tracks: " << numTracks << std::endl;
        return false;
    }

    if (ppq == 0) {
        std::cerr << "ERROR: Invalid PPQ value: " << ppq << std::endl;
        return false;
    }

    std::cout << "MIDI file validation passed: Format=" << format
              << ", Tracks=" << numTracks << ", PPQ=" << ppq << std::endl;
    return true;
}

// Function to collect all tempo events from the MIDI file
std::vector<TempoEvent> collectGlobalTempoEvents(const std::string& filename) {
    std::vector<TempoEvent> tempoEvents;

    std::ifstream file(filename, std::ios::binary);
    if (!file.is_open()) {
        std::cerr << "Error: Could not open file " << filename << std::endl;
        return tempoEvents;
    }

    // Read header
    char headerChunk[4];
    file.read(headerChunk, 4);
    if (std::string(headerChunk, 4) != "MThd") {
        std::cerr << "Error: Invalid MIDI file format" << std::endl;
        return tempoEvents;
    }

    uint32_t headerLength;
    file.read(reinterpret_cast<char*>(&headerLength), 4);
    headerLength = ntohl(headerLength);

    uint16_t format, numTracks, ppq;
    file.read(reinterpret_cast<char*>(&format), 2);
    file.read(reinterpret_cast<char*>(&numTracks), 2);
    file.read(reinterpret_cast<char*>(&ppq), 2);
    format = ntohs(format);
    numTracks = ntohs(numTracks);
    ppq = ntohs(ppq);

    // Process each track looking for tempo events
    for (int trackNum = 0; trackNum < numTracks; trackNum++) {
        char trackChunk[4];
        file.read(trackChunk, 4);
        if (std::string(trackChunk, 4) != "MTrk") {
            std::cerr << "Error: Expected track chunk" << std::endl;
            continue;
        }

        uint32_t trackLength;
        file.read(reinterpret_cast<char*>(&trackLength), 4);
        trackLength = ntohl(trackLength);

        uint32_t currentTick = 0;
        uint32_t bytesRead = 0;
        uint8_t runningStatus = 0;

        while (bytesRead < trackLength) {
            // Read delta time
            uint32_t deltaTime = 0;
            uint8_t byte;
            do {
                file.read(reinterpret_cast<char*>(&byte), 1);
                bytesRead++;
                deltaTime = (deltaTime << 7) | (byte & 0x7F);
            } while (byte & 0x80);

            currentTick += deltaTime;

            // Read event
            file.read(reinterpret_cast<char*>(&byte), 1);
            bytesRead++;

            if (byte == 0xFF) { // Meta event
                uint8_t metaType;
                file.read(reinterpret_cast<char*>(&metaType), 1);
                bytesRead++;

                uint32_t length = 0;
                do {
                    file.read(reinterpret_cast<char*>(&byte), 1);
                    bytesRead++;
                    length = (length << 7) | (byte & 0x7F);
                } while (byte & 0x80);

                if (metaType == 0x51 && length == 3) { // Tempo event
                    uint8_t tempoBytes[3];
                    file.read(reinterpret_cast<char*>(tempoBytes), 3);
                    bytesRead += 3;

                    uint32_t tempoMicroseconds = (tempoBytes[0] << 16) | (tempoBytes[1] << 8) | tempoBytes[2];
                    tempoEvents.push_back({currentTick, tempoMicroseconds});
                } else {
                    // Skip other meta events
                    file.seekg(length, std::ios::cur);
                    bytesRead += length;
                }
            } else {
                // Handle other events (skip them for tempo collection)
                if (byte >= 0x80) {
                    runningStatus = byte;
                } else {
                    file.seekg(-1, std::ios::cur);
                    bytesRead--;
                    byte = runningStatus;
                }

                // Skip event data based on status
                if ((byte & 0xF0) == 0x80 || (byte & 0xF0) == 0x90 || (byte & 0xF0) == 0xA0 || (byte & 0xF0) == 0xB0 || (byte & 0xF0) == 0xE0) {
                    file.seekg(2, std::ios::cur);
                    bytesRead += 2;
                } else if ((byte & 0xF0) == 0xC0 || (byte & 0xF0) == 0xD0) {
                    file.seekg(1, std::ios::cur);
                    bytesRead += 1;
                } else if (byte == 0xF0) { // SysEx
                    uint32_t sysexLength = 0;
                    do {
                        file.read(reinterpret_cast<char*>(&byte), 1);
                        bytesRead++;
                        sysexLength = (sysexLength << 7) | (byte & 0x7F);
                    } while (byte & 0x80);
                    file.seekg(sysexLength, std::ios::cur);
                    bytesRead += sysexLength;
                }
            }
        }
    }

    file.close();

    // Sort tempo events by tick
    std::sort(tempoEvents.begin(), tempoEvents.end(),
              [](const TempoEvent& a, const TempoEvent& b) {
                  return a.tick < b.tick;
              });

    return tempoEvents;
}

float GetNoteY(uint8_t note) {
    // Full MIDI range positioning (0-127)
    return GetScreenHeight() - 50 - ((float)note / 127.0f) * (GetScreenHeight() - 100);
}

void DrawVisualizerNotesDefault(const std::vector<TrackData>& tracks, int currentTick, int ppq, std::vector<size_t>& searchStartIndices) {
    // Adaptive performance settings...
    int adaptiveViewWindow = ppq * 4; // Simplified for clarity
    int maxNotesToDraw = 65536;
    const int viewWindow = adaptiveViewWindow;
    int notesDrawn = 0;
    int screenWidth = GetScreenWidth();
    int screenHeight = GetScreenHeight();
    //const int viewWindow = ppq * 4; // Simplified for this example

    // --- OPTIMIZATION START ---
    // Instead of looping all notes, find the relevant starting point.
    // First, let's find the first note that might be visible.
    // We can move the searchStartIndex backward a bit in case the player seeks backward.
    if (searchStartIndices.size() != tracks.size()) {
        searchStartIndices.assign(tracks.size(), 0);
    }

    for (size_t t = 0; t < tracks.size(); ++t) {
        const TrackData& track = tracks[t];
        if (track.notes.empty()) continue;

        // Move the search index back a bit in case we seek backward
        if (searchStartIndices[t] > 100) {
            searchStartIndices[t] -= 100;
        } else {
            searchStartIndices[t] = 0;
        }

        // Start searching from our last known position to be much faster.
        for (size_t i = searchStartIndices[t]; i < track.notes.size(); ++i) {
            const NoteEvent& note = track.notes[i];

            if (note.startTick > currentTick + viewWindow) {
                break; // Note is too far in the future
            }

            if (note.endTick < currentTick - (viewWindow / 4)) {
                searchStartIndices[t] = i + 1; // Update this track's index
                continue; // Note is too far in the past
            }

            // --- Your existing drawing logic for the note ---
            float x = ((float)(note.startTick - currentTick) / viewWindow) * screenWidth;
            float width = ((float)(note.endTick - note.startTick) / viewWindow) * screenWidth;
            float y = screenHeight - 50 - ((float)note.note / 127.0f) * (screenHeight - 100);
            float height = 3;
            if (width < 2) width = 2;

            if (x + width >= 0 && x <= screenWidth) {
                bool isActive = (note.startTick <= currentTick && note.endTick > currentTick);
                Color noteColor = isActive ? WHITE : GetTrackColorPFA((int)t);
                DrawRectangleRec({x, y, width, height}, noteColor);
                notesDrawn++;
            }
            if (notesDrawn >= maxNotesToDraw) break;
        }
        if (notesDrawn >= maxNotesToDraw) break;
    }
    // --- (The rest of your drawing logic for guides, etc.) ---
}

void DrawVisualizerNotesTracks(const std::vector<TrackData>& tracks, int currentTick, int ppq) {
    // ADVANCED: Full tracks support with adaptive performance scaling

    // Calculate complexity metrics
    size_t totalNotes = 0;
    int tracksWithNotes = 0;
    for (const auto& track : tracks) {
        if (!track.notes.empty()) {
            totalNotes += track.notes.size();
            tracksWithNotes++;
        }
    }

    // Adaptive scaling based on complexity
    int adaptiveViewWindow;
    int maxNotesToDraw;

    if (ppq > 10000 || totalNotes > 500000 || tracksWithNotes > 40) {
        // High complexity: aggressive optimization
        adaptiveViewWindow = std::max(static_cast<int>(ppq / 4 * qualityScale * scrollSpeedMultiplier), 480);
        maxNotesToDraw = static_cast<int>(16384 * qualityScale);
    } else if (ppq > 2000 || totalNotes > 100000 || tracksWithNotes > 20) {
        // Medium complexity
        adaptiveViewWindow = static_cast<int>(ppq * 2 * qualityScale * scrollSpeedMultiplier);
        maxNotesToDraw = static_cast<int>(32768 * qualityScale);
    } else {
        // Low complexity: full quality
        adaptiveViewWindow = static_cast<int>(ppq * 4 * qualityScale * scrollSpeedMultiplier);
        maxNotesToDraw = static_cast<int>(65536 * qualityScale);
    }

    const int viewWindow = adaptiveViewWindow;

    int notesDrawn = 0;
    // tracksWithNotes already calculated above, don't redeclare

    // Calculate dynamic track height based on ALL tracks with notes (not just visible ones)
    int screenHeight = GetScreenHeight();

    // Adaptive track rendering based on track count
    int availableHeight = screenHeight - 120; // More space for UI with complex files
    int maxVisibleTracks;
    int trackHeight;

    if (tracksWithNotes > 40) {
        // Many tracks: show subset with scrolling capability
        maxVisibleTracks = 20;
        trackHeight = availableHeight / maxVisibleTracks;
        if (trackHeight < 20) trackHeight = 20; // Minimum readable height
    } else if (tracksWithNotes > 20) {
        // Medium track count: smaller tracks
        maxVisibleTracks = tracksWithNotes;
        trackHeight = availableHeight / tracksWithNotes;
        if (trackHeight < 12) trackHeight = 12;
    } else {
        // Few tracks: full height
        maxVisibleTracks = tracksWithNotes;
        trackHeight = availableHeight / std::max(tracksWithNotes, 1);
        if (trackHeight > 60) trackHeight = 60; // Maximum height for readability
    }

    // Track scrolling for many tracks (simple implementation)
    static int trackScrollOffset = 0;
    if (tracksWithNotes > maxVisibleTracks) {
        // Simple auto-scroll based on playback time (could be enhanced with user controls)
        trackScrollOffset = 0; // For now, show first tracks
    }

    // Clean display - remove debug clutter

    // Process tracks with smart visibility management
    int currentTrackIndex = 0; // Counter for track positioning
    int visibleTrackCount = 0; // Counter for actually visible tracks

    for (size_t t = 0; t < tracks.size(); ++t) {
        const TrackData& track = tracks[t];

        // Skip tracks with no notes
        if (track.notes.empty()) continue;

        // Skip tracks outside visible range (for performance with many tracks)
        if (currentTrackIndex < trackScrollOffset) {
            currentTrackIndex++;
            continue;
        }

        if (visibleTrackCount >= maxVisibleTracks) {
            break; // Stop rendering when we've filled the screen
        }

        Color trackColor = GetTrackColorPFA((int)t);
        int trackY = 50 + (visibleTrackCount * trackHeight); // Use visible track count for positioning

        // Track for active notes to avoid overlaps
        std::map<int, uint32_t> activeNotes; // note -> endTick

        // Draw track background
        DrawRectangle(0, trackY, GetScreenWidth(), trackHeight - 2,
                     Color{(unsigned char)(trackColor.r/4), (unsigned char)(trackColor.g/4), (unsigned char)(trackColor.b/4), 50});

        // Draw track label with note count (show as channel)
        DrawText(TextFormat("Channel %d (%d notes)", (int)t, (int)track.notes.size()), 5, trackY + 2, 12, WHITE);

        // Process notes for this track (limit per track to ensure all tracks are visible)
        int trackNotesDrawn = 0;
        for (const NoteEvent& note : track.notes) {
            if (trackNotesDrawn >= 50) break; // Limit per track, not global

            // Skip notes that are completely in the past
            if (note.endTick < currentTick) continue;

            // Skip notes that are too far in the future
            if (note.startTick > currentTick + viewWindow) continue;

            // Calculate horizontal position (time-based)
            float x = ((float)(note.startTick - currentTick) / viewWindow) * GetScreenWidth();
            float width = ((float)(note.endTick - note.startTick) / viewWindow) * GetScreenWidth();

            // Ensure minimum width for visibility
            if (width < 2) width = 2;

            // More aggressive visibility check for tracks mode
            if (x + width >= -10 && x <= GetScreenWidth() + 10) {
                // Calculate drawing bounds with proper left edge handling
                float drawX = x;
                float drawWidth = width;

                // Handle left edge clipping more carefully - ensure notes aren't cut off
                if (x < 0) {
                    // Only clip if the note extends significantly off-screen
                    if (x + width > 5) { // Note extends at least 5 pixels into visible area
                        drawX = 0;
                        drawWidth = width + x; // Reduce width by the amount off-screen
                        if (drawWidth <= 1) continue; // Skip if too small to see
                    } else {
                        continue; // Skip notes that are mostly off-screen
                    }
                }

                // Handle right edge clipping
                if (drawX + drawWidth > GetScreenWidth()) {
                    drawWidth = GetScreenWidth() - drawX;
                    if (drawWidth <= 0) continue; // Skip if completely off-screen
                }

                // Draw if there's something visible (minimum 1 pixel)
                if (drawWidth >= 1) {

                // Calculate note position within track (pitch-based)
                int noteHeight = trackHeight / 12; // Divide track into 12 semitones
                int noteY = trackY + ((127 - note.note) % 12) * noteHeight;

                // Check for overlapping notes and offset if needed
                bool isActive = (note.startTick <= currentTick && note.endTick > currentTick);
                Color noteColor = isActive ?
                    Color{255, 255, 255, 255} : // Active notes are white
                    Color{trackColor.r, trackColor.g, trackColor.b, 180}; // Inactive notes are track color

                    DrawRectangle((int)drawX, noteY, (int)drawWidth, noteHeight - 1, noteColor);

                    // Draw note border for active notes
                    if (isActive) {
                        DrawRectangleLines((int)drawX, noteY, (int)drawWidth, noteHeight - 1, YELLOW);
                    }

                    notesDrawn++;
                    trackNotesDrawn++;
                }
            }
        }

        // Move to next track position
        currentTrackIndex++;
        visibleTrackCount++;
    }

    // Minimal debug info for tracks mode
    // Remove most debug text for clean display
}

// Main drawing function that chooses the appropriate render mode
void DrawVisualizerNotes(const std::vector<TrackData>& tracks, int currentTick, int ppq, std::vector<size_t>& searchStartIndices) {
    if (selectedRenderMode == RENDER_DEFAULT) {
        // Pass the index along to the default renderer for performance
        DrawVisualizerNotesDefault(tracks, currentTick, ppq, searchStartIndices);
    } else {
        // The tracks renderer doesn't use the search index optimization yet
        DrawVisualizerNotesTracks(tracks, currentTick, ppq);
    }
}

bool loadVisualizerMidiData(const std::string& filename, std::vector<TrackData>& tracks, int& ppq, int& tempoMicroseconds) {
    // Validate file first
    if (!validateMidiFile(filename)) {
        std::cerr << "ERROR: MIDI file validation failed for " << filename << std::endl;
        return false;
    }

    std::ifstream file(filename, std::ios::binary);
    if (!file) {
        std::cerr << "ERROR: Cannot open file after validation: " << filename << std::endl;
        return false;
    }

    char header[14];
    file.read(header, 14);
    if (std::memcmp(header, "MThd", 4) != 0) return false;

    ppq = (static_cast<uint8_t>(header[12]) << 8) | static_cast<uint8_t>(header[13]);
    tempoMicroseconds = MidiTiming::DEFAULT_TEMPO_MICROSECONDS;

    std::cout << "Raw PPQ from file: " << ppq << std::endl;

    // Only handle invalid PPQ values (0 or out of MIDI spec range)
    if (ppq == 0) {
        std::cout << "ERROR: Invalid PPQ value (0), using default 480" << std::endl;
        ppq = 480;
    } else if (ppq > 32767) {
        // MIDI spec allows up to 32767 for positive values
        std::cout << "WARNING: PPQ (" << ppq << ") exceeds MIDI spec, clamping to 32767" << std::endl;
        ppq = 32767;
    } else {
        // PPQ is valid, use as-is
        std::cout << "Using PPQ: " << ppq << std::endl;
    }

    // Initialize 16 channel-based tracks (one for each MIDI channel)
    tracks.clear();
    tracks.resize(16); // Always exactly 16 tracks (channels 0-15)

    std::vector<TrackData> tempTracks; // Temporary storage for file tracks
    const size_t MAX_FILE_TRACKS = 65535; // Allow many file tracks to read

    while (file && tempTracks.size() < MAX_FILE_TRACKS) {
        char chunkHeader[8];
        file.read(chunkHeader, 8);
        if (file.gcount() < 8) break;

        if (std::memcmp(chunkHeader, "MTrk", 4) != 0) {
            file.seekg((static_cast<uint8_t>(chunkHeader[4]) << 24) |
                       (static_cast<uint8_t>(chunkHeader[5]) << 16) |
                       (static_cast<uint8_t>(chunkHeader[6]) << 8) |
                       static_cast<uint8_t>(chunkHeader[7]), std::ios::cur);
            continue;
        }

        // Read track length as 32-bit big-endian (MIDI standard)
        uint32_t trackLength32 = (static_cast<uint8_t>(chunkHeader[4]) << 24) |
                                 (static_cast<uint8_t>(chunkHeader[5]) << 16) |
                                 (static_cast<uint8_t>(chunkHeader[6]) << 8) |
                                 static_cast<uint8_t>(chunkHeader[7]);

        // Convert to 64-bit for better handling and overflow detection
        uint64_t trackLength = static_cast<uint64_t>(trackLength32);

        // Protection against corrupted files - CHECK BEFORE ALLOCATION
        const size_t MAX_NOTES_PER_TRACK = 100000000; // 100 million notes per track (increased for complex MIDIs)
        const uint64_t MAX_TRACK_LENGTH = 200000000ULL; // 200MB max track size (increased for complex MIDIs)
        const uint64_t MIN_TRACK_LENGTH = 4ULL; // Minimum valid track length

        // Debug output for problematic track lengths
        if (trackLength32 == UINT32_MAX || trackLength32 > 100000000) {
            std::cout << "WARNING: Suspicious track length detected: " << trackLength32
                      << " (0x" << std::hex << trackLength32 << std::dec << ")" << std::endl;

            // Special handling for the 4GB corruption pattern
            if (trackLength32 >= 4294967000UL) { // Close to UINT32_MAX
                std::cout << "ERROR: Track length appears to be corrupted (near 4GB limit)" << std::endl;
                std::cout << "This suggests a 32-bit overflow or corrupted MIDI file" << std::endl;

                // Try to recover by skipping this track entirely
                std::cout << "Attempting to skip corrupted track..." << std::endl;
                break; // Stop processing this file
            }
        }

        // Validate track length before attempting to allocate memory
        if (trackLength > MAX_TRACK_LENGTH) {
            std::cout << "WARNING: Track too large (" << trackLength << " bytes), skipping" << std::endl;
            // For very large values, try to skip safely
            if (trackLength32 == UINT32_MAX) {
                std::cout << "ERROR: Track length is UINT32_MAX, likely corrupted data. Stopping." << std::endl;
                break;
            }
            file.seekg(trackLength32, std::ios::cur);
            continue;
        }

        if (trackLength < MIN_TRACK_LENGTH) {
            std::cout << "WARNING: Track too small (" << trackLength << " bytes), skipping" << std::endl;
            file.seekg(trackLength32, std::ios::cur);
            continue;
        }

        // Check if we can actually read this much data from the file
        std::streampos currentPos = file.tellg();
        file.seekg(0, std::ios::end);
        std::streampos fileSize = file.tellg();
        file.seekg(currentPos);

        uint64_t remainingFileSize = static_cast<uint64_t>(fileSize - currentPos);

        if (trackLength > remainingFileSize) {
            std::cout << "WARNING: Track length (" << trackLength
                      << ") exceeds remaining file size (" << remainingFileSize << "), skipping" << std::endl;
            break; // End of valid data
        }

        std::vector<uint8_t> trackData;
        try {
            // Ensure we don't exceed size_t limits when allocating
            if (trackLength > static_cast<uint64_t>(SIZE_MAX)) {
                std::cout << "ERROR: Track length exceeds system memory limits" << std::endl;
                continue;
            }

            size_t allocSize = static_cast<size_t>(trackLength);
            trackData.resize(allocSize);

            // Read using the original 32-bit value to match MIDI standard
            file.read(reinterpret_cast<char*>(trackData.data()), trackLength32);

            if (file.gcount() != static_cast<std::streamsize>(trackLength32)) {
                std::cout << "WARNING: Could not read complete track data (expected "
                          << trackLength32 << ", got " << file.gcount() << "), skipping" << std::endl;
                continue;
            }
        } catch (const std::exception& e) {
            std::cout << "ERROR: Failed to allocate memory for track (" << trackLength << " bytes): " << e.what() << std::endl;
            continue;
        }

        TrackData tempTrack;
        uint32_t tick = 0;
        size_t i = 0;
        uint8_t runningStatus = 0;

        // Track active notes for proper note-off handling
        std::map<std::pair<uint8_t, uint8_t>, size_t> activeNotes; // (note, channel) -> index in tempTrack.notes

        while (i < trackData.size() && tempTrack.notes.size() < MAX_NOTES_PER_TRACK) {
            // Read variable-length delta time (fixed implementation)
            uint32_t delta = 0;
            uint8_t byte = 0;
            int varLenBytes = 0;
            const int MAX_VAR_LEN_BYTES = 4; // MIDI standard allows max 4 bytes for variable length

            // Proper variable-length quantity parsing
            do {
                if (i >= trackData.size()) break;
                byte = trackData[i++];
                delta = (delta << 7) | (byte & 0x7F);
                varLenBytes++;
                if (varLenBytes >= MAX_VAR_LEN_BYTES) break; // Prevent infinite loop
            } while (byte & 0x80);

            // Check for tick overflow
            if (tick > UINT32_MAX - delta) {
                std::cout << "WARNING: Tick overflow detected, stopping track parsing" << std::endl;
                break;
            }

            tick += delta;

            if (i >= trackData.size()) break;
            uint8_t status = trackData[i];
            if (status < 0x80) status = runningStatus;
            else runningStatus = trackData[i++];

            if ((status & 0xF0) == 0x90 && i + 1 < trackData.size()) {
                // Note On
                uint8_t note = trackData[i++];
                uint8_t velocity = trackData[i++];
                uint8_t channel = status & 0x0F;

                if (velocity > 0) {
                    // Check if we've hit the note limit
                    if (tempTrack.notes.size() >= MAX_NOTES_PER_TRACK) {
                        std::cout << "WARNING: Track " << tempTracks.size() << " has too many notes, stopping at "
                                  << MAX_NOTES_PER_TRACK << " (current tick: " << tick << ")" << std::endl;
                        break;
                    }

                    // Real Note On
                    NoteEvent ev;
                    ev.startTick = tick;
                    ev.endTick = tick + ppq; // Default duration of 1 quarter note
                    ev.note = note;
                    ev.velocity = velocity;
                    ev.channel = channel;
                    tempTrack.notes.push_back(ev);

                    // Track this note for proper note-off handling
                    activeNotes[{note, channel}] = tempTrack.notes.size() - 1;
                } else {
                    // Note On with velocity 0 = Note Off
                    auto key = std::make_pair(note, channel);
                    auto it = activeNotes.find(key);
                    if (it != activeNotes.end()) {
                        tempTrack.notes[it->second].endTick = tick;
                        activeNotes.erase(it);
                    }
                }
            } else if ((status & 0xF0) == 0x80 && i + 1 < trackData.size()) {
                // Note Off
                uint8_t note = trackData[i++];
                uint8_t velocity = trackData[i++]; // Note off velocity (usually ignored)
                uint8_t channel = status & 0x0F;

                auto key = std::make_pair(note, channel);
                auto it = activeNotes.find(key);
                if (it != activeNotes.end()) {
                    tempTrack.notes[it->second].endTick = tick;
                    activeNotes.erase(it);
                }
            } else if ((status & 0xF0) >= 0xA0 && (status & 0xF0) <= 0xE0 && i + 1 < trackData.size()) {
                // Other 2-byte messages (Aftertouch, Control Change, Pitch Bend)
                i += 2;
            } else if (status == 0xFF && i + 1 < trackData.size()) {
                // Meta events
                uint8_t metaType = trackData[i++];
                uint32_t length = 0;
                while (i < trackData.size() && (trackData[i] & 0x80)) {
                    length = (length << 7) | (trackData[i++] & 0x7F);
                }
                if (i < trackData.size()) length = (length << 7) | (trackData[i++] & 0x7F);

                if (metaType == 0x51 && length == 3 && i + 3 <= trackData.size()) {
                    // Tempo change (silent processing)
                    uint32_t newTempo = (trackData[i] << 16) | (trackData[i + 1] << 8) | trackData[i + 2];
                    // Validate tempo (should be between 60 BPM and 300 BPM roughly)
                    if (newTempo >= 200000 && newTempo <= 1000000) {
                        tempoMicroseconds = newTempo;
                        // Removed tempo logging for performance
                    }
                    // Removed invalid tempo logging for performance
                }
                i += length;
            } else if (status == 0xF0 || status == 0xF7) {
                // SysEx events
                uint32_t length = 0;
                while (i < trackData.size() && (trackData[i] & 0x80)) {
                    length = (length << 7) | (trackData[i++] & 0x7F);
                }
                if (i < trackData.size()) length = (length << 7) | (trackData[i++] & 0x7F);
                i += length;
            } else {
                // Unknown event, skip
                i++;
            }
        }

        // Close any remaining active notes at the end of the track
        for (auto& [key, noteIndex] : activeNotes) {
            tempTrack.notes[noteIndex].endTick = tick;
        }

        std::cout << "File Track " << tempTracks.size() << " loaded: " << tempTrack.notes.size()
                  << " notes, last tick: " << tick << " (parsed " << i << "/" << trackData.size() << " bytes)" << std::endl;

        tempTracks.push_back(std::move(tempTrack));
    }

    // Now group all notes from tempTracks into 16 channel-based tracks
    std::cout << "Grouping " << tempTracks.size() << " file tracks into 16 channel tracks..." << std::endl;

    for (const auto& fileTrack : tempTracks) {
        for (const auto& note : fileTrack.notes) {
            uint8_t channel = note.channel;
            if (channel < 16) { // Safety check
                tracks[channel].notes.push_back(note);
            }
        }
    }

    // Print summary
    int totalNotes = 0;
    uint32_t earliestNote = UINT32_MAX;
    uint32_t latestNote = 0;

    for (const auto& track : tracks) {
        totalNotes += track.notes.size();
        if (!track.notes.empty()) {
            for (const auto& note : track.notes) {
                if (note.startTick < earliestNote) earliestNote = note.startTick;
                if (note.startTick > latestNote) latestNote = note.startTick;
            }
        }
    }

    std::cout << "\n=== MIDI Load Summary ===" << std::endl;
    std::cout << "File tracks read: " << tempTracks.size() << std::endl;
    std::cout << "Channel tracks created: " << tracks.size() << std::endl;
    std::cout << "Total notes: " << totalNotes << std::endl;

    // Print notes per channel
    for (int i = 0; i < 16; i++) {
        if (!tracks[i].notes.empty()) {
            std::cout << "Channel " << i << ": " << tracks[i].notes.size() << " notes" << std::endl;
        }
    }
    std::cout << "PPQ: " << ppq << std::endl;
    if (totalNotes > 0) {
        std::cout << "First note at tick: " << earliestNote << std::endl;
        std::cout << "Last note at tick: " << latestNote << std::endl;
        std::cout << "First note time: " << (earliestNote * MidiTiming::CalculateMicrosecondsPerTick(tempoMicroseconds, ppq) / 1000.0) << " ms" << std::endl;

        // Show first few notes from each track for debugging
        for (size_t t = 0; t < tracks.size() && t < 3; ++t) {
            const auto& track = tracks[t];
            std::cout << "Track " << t << " first 3 notes:" << std::endl;
            for (size_t n = 0; n < track.notes.size() && n < 3; ++n) {
                const auto& note = track.notes[n];
                std::cout << "  Note " << (int)note.note << " at tick " << note.startTick
                          << "-" << note.endTick << " (ch " << (int)note.channel << ")" << std::endl;
            }
        }
    }
    std::cout << "=========================" << std::endl;

    return true;
}

int main(int argc, char* argv[]) {
    // Handle command line arguments
    if (argc > 1) {
        selectedMidiFile = argv[1];
    }

    // === Initialization ===
    InitWindow(1280, 720, "JIDI Player - Mode Selection");
    //SetTargetFPS(144);

    // Variables for MIDI data (will be loaded when user starts)
    std::vector<TrackData> tracks;
    int ppq = 480;
    int initialTempo = 500000;
    bool midiLoaded = false;
    bool kdmapiInitialized = false;

    // Variables for playback state
    std::vector<PlaybackEvent> eventStream;
    auto playbackStartTime = std::chrono::steady_clock::now();
    size_t nextEventIndex = 0;
    uint32_t lastTick = 0;
    double microsecondsPerTick = 0.0;
    uint64_t accumulatedMicroseconds = 0;
    uint32_t currentTempo = initialTempo;

    // Additional variables needed for compilation
    std::vector<TempoEvent> globalTempoEvents;
    std::vector<TempoMapEntry> tempoMap;
    uint32_t firstNoteTick = UINT32_MAX;

    // Function to initialize MIDI playback with progress updates
    auto initializeMidiPlayback = [&]() -> bool {
        loadingStatus = "Loading MIDI file...";
        loadingProgress = 10;
        if (!loadVisualizerMidiData(selectedMidiFile, tracks, ppq, initialTempo)) {
            return false;
        }

        // Load MIDI data with detailed error reporting
        tracks.clear(); // Keep this line to ensure a fresh start
        std::cout << "Loading MIDI file: " << selectedMidiFile << std::endl;

        if (!loadVisualizerMidiData(selectedMidiFile, tracks, ppq, initialTempo)) {
            loadingStatus = "Failed to load MIDI file! Check console for details.";
            std::cerr << "CRITICAL ERROR: Failed to load " << selectedMidiFile << std::endl;
            return false;
        }

        std::cout << "Successfully loaded " << tracks.size() << " channel tracks" << std::endl;
        searchStartIndices.resize(tracks.size(), 0);

        loadingProgress = 30;
        if (!InitializeKDMAPIStream()) {
             return false;
        }

        loadingProgress = 50;
        loadingStatus = "Processing tempo events...";

        // Collect global tempo events
        globalTempoEvents = collectGlobalTempoEvents(selectedMidiFile);

        loadingProgress = 60;
        totalTracks = (int)tracks.size();
        processedTracks = totalTracks;

        loadingProgress = 70;
        loadingStatus = "Building event stream...";

        // Build the unified event stream
        eventStream.clear();
        firstNoteTick = UINT32_MAX;

        // Add initial tempo event if no tempo events found
        if (globalTempoEvents.empty()) {
            eventStream.push_back({
                PlaybackEvent::TEMPO, 0, 0, 0, 0, (uint32_t)initialTempo
            });
        } else {
            // Add all global tempo events
            for (const auto& tempoEvent : globalTempoEvents) {
                eventStream.push_back({
                    PlaybackEvent::TEMPO, tempoEvent.tick, 0, 0, 0, tempoEvent.tempoMicroseconds
                });
            }
        }

        loadingProgress = 80;
        loadingStatus = "Processing notes...";

        // Add all note events from all tracks
        int noteCount = 0;
        for (const auto& track : tracks) {
            for (const auto& note : track.notes) {
                if (note.startTick < firstNoteTick) {
                    firstNoteTick = note.startTick;
                }

                eventStream.push_back({
                    PlaybackEvent::NOTE, note.startTick,
                    (uint8_t)(0x90 | (note.channel & 0x0F)),
                    note.note, note.velocity
                });
                eventStream.push_back({
                    PlaybackEvent::NOTE, note.endTick,
                    (uint8_t)(0x80 | (note.channel & 0x0F)),
                    note.note, 0
                });
                noteCount++;
            }
        }

        totalEvents = (int)eventStream.size();
        processedEvents = totalEvents;

        loadingProgress = 90;
        loadingStatus = "Sorting events...";

        std::sort(eventStream.begin(), eventStream.end());

        loadingProgress = 95;
        loadingStatus = "Finalizing...";

        // Reset playback state
        playbackStartTime = std::chrono::steady_clock::now();
        nextEventIndex = 0;
        lastTick = 0;
        microsecondsPerTick = MidiTiming::CalculateMicrosecondsPerTick(initialTempo, ppq);
        accumulatedMicroseconds = 0;

        // Send All Notes Off to all channels to ensure clean start
        for (int channel = 0; channel < 16; channel++) {
            unsigned long allNotesOff = 0xB0 | channel | (0x7B << 8) | (0 << 16); // CC 123 = All Notes Off
            SendDirectData(allNotesOff);
        }
        currentTempo = initialTempo;

        // No auto-skip - let user manually skip with F key if needed
        // This ensures normal MIDI files play with proper sync

        // Build tempo map for accurate tick calculation
        tempoMap.clear();
        tempoMap.push_back({0, static_cast<uint32_t>(initialTempo)});

        uint32_t lastTempoAdded = initialTempo;
        for (const auto& event : eventStream) {
            if (event.type == PlaybackEvent::TEMPO) {
                // For extreme tempo changes (like 183→60 BPM), use smaller threshold
                // This ensures we capture all significant tempo changes accurately
                uint32_t tempoDiff = (event.tempoValue > lastTempoAdded) ?
                    event.tempoValue - lastTempoAdded : lastTempoAdded - event.tempoValue;

                // Use 0.5% threshold for better accuracy with extreme tempo changes
                if (tempoDiff > lastTempoAdded / 200) {  // >0.5% change
                    tempoMap.push_back({event.tick, event.tempoValue});
                    lastTempoAdded = event.tempoValue;
                }
            }
        }

        std::cout << "Tempo map optimized: " << tempoMap.size() << " entries (from 213 original)" << std::endl;

        loadingProgress = 100;
        loadingStatus = "Ready!";

        // Initialize search indices for all tracks
        searchStartIndices.assign(tracks.size(), 0);
        midiLoaded = true;
        return true;
    };

    // Function to calculate tick from elapsed time using tempo map
    auto calculateTickFromTime = [&](uint64_t elapsedMicroseconds) -> uint64_t {
        // Safety check for empty tempo map
        if (tempoMap.empty()) {
            double microsecondsPerTick = MidiTiming::CalculateMicrosecondsPerTick(initialTempo, ppq);
            if (microsecondsPerTick <= 0) return 0;
            return static_cast<uint64_t>(elapsedMicroseconds / microsecondsPerTick);
        }

        uint64_t timeRemaining = elapsedMicroseconds;
        uint64_t currentTick = 0;  // Use 64-bit for large tick values
        uint32_t currentTempo = initialTempo;

        for (size_t i = 1; i < tempoMap.size(); i++) {
            uint32_t nextTempoTick = tempoMap[i].tick;
            uint32_t nextTempo = tempoMap[i].tempoMicroseconds;

            // Safety check for invalid tempo values
            if (nextTempo == 0 || currentTempo == 0) continue;

            // Safety check for tick overflow
            if (nextTempoTick < currentTick) continue;

            // Calculate time needed to reach next tempo change
            uint64_t ticksToNextTempo = nextTempoTick - currentTick;  // Use 64-bit
            double microsecondsPerTickCurrent = MidiTiming::CalculateMicrosecondsPerTick(currentTempo, ppq);

            // Safety check for division by zero
            if (microsecondsPerTickCurrent <= 0) continue;

            uint64_t timeToNextTempo = static_cast<uint64_t>(ticksToNextTempo * microsecondsPerTickCurrent);

            if (timeRemaining <= timeToNextTempo) {
                // Target time is before next tempo change
                uint64_t additionalTicks = static_cast<uint64_t>(timeRemaining / microsecondsPerTickCurrent);

                return currentTick + additionalTicks;
            } else {
                // Move past this tempo segment
                timeRemaining -= timeToNextTempo;
                currentTick = nextTempoTick;
                currentTempo = nextTempo;
            }
        }

        // Handle remaining time after last tempo change
        double microsecondsPerTickCurrent = MidiTiming::CalculateMicrosecondsPerTick(currentTempo, ppq);
        if (microsecondsPerTickCurrent <= 0) return currentTick;

        uint64_t additionalTicks = static_cast<uint64_t>(timeRemaining / microsecondsPerTickCurrent);

        return currentTick + additionalTicks;
    };

    // === Main Loop ===
    bool shouldExit = false;
    while (!shouldExit) {
        if (WindowShouldClose()) {
            shouldExit = true;
            break;
        }
        // Handle state transitions
        switch (currentState) {
            case STATE_MENU: {
                if (IsKeyPressed(KEY_Q) && (IsKeyDown(KEY_LEFT_CONTROL) || IsKeyDown(KEY_RIGHT_CONTROL))) {
                    shouldExit = true;
                }
                BeginDrawing();
                DrawModeSelectionMenu();
                EndDrawing();
                break;
            }
            case STATE_LOADING: {
                BeginDrawing();
                DrawLoadingScreen();
                EndDrawing();


            // Perform loading with staged progress (minimum 2 seconds for visibility)
            static bool loadingStarted = false;
            static auto loadingStartTime = std::chrono::steady_clock::now();
            static bool loadingComplete = false;

            if (!loadingStarted) {
                loadingStarted = true;
                loadingStartTime = std::chrono::steady_clock::now();
                loadingComplete = false;
                loadingProgress = 0;
            }

            auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::steady_clock::now() - loadingStartTime).count();

            // Stage the loading process over time for visibility
            if (elapsed < 300) {
                loadingProgress = (int)(elapsed / 3); // 0-100 over 300ms
                loadingStatus = "Loading MIDI file...";
            } else if (elapsed < 600) {
                loadingProgress = 30 + (int)((elapsed - 300) / 10); // 30-60 over 300ms
                loadingStatus = "Processing tracks...";
            } else if (elapsed < 900) {
                loadingProgress = 60 + (int)((elapsed - 600) / 10); // 60-90 over 300ms
                loadingStatus = "Building event stream...";
            } else if (elapsed < 1200) {
                loadingProgress = 90 + (int)((elapsed - 900) / 30); // 90-100 over 300ms
                loadingStatus = "Finalizing...";
            } else if (!loadingComplete) {
                // Actually perform the loading after visual progress
                if (initializeMidiPlayback()) {
                    loadingComplete = true;
                    loadingProgress = 100;
                    loadingStatus = "Ready!";
                } else {
                    currentState = STATE_MENU;
                    break;
                }
            } else if (elapsed > 1500) {
                // Show "Ready!" for a moment, then start playing
                currentState = STATE_PLAYING;
                SetWindowTitle(TextFormat("JIDI Player - %s (%s Mode)",
                    selectedMidiFile.c_str(),
                    selectedRenderMode == RENDER_DEFAULT ? "Default" : "Tracks"));
                loadingStarted = false;
            }
            continue;
            }
            case STATE_PLAYING: {
        // Playing state - MIDI playback and visualization
        if (midiLoaded) {
            if (!midiLoaded) {
                    currentState = STATE_MENU;
                    break;
                }
            // Add pause functionality
            static bool isPaused = false;
            static auto pauseStartTime = std::chrono::steady_clock::now();
            static uint64_t totalPausedTime = 0;

            // Handle keyboard input
            if (IsKeyPressed(KEY_BACKSPACE)) {
                // Return to menu - cleanup KDMAPI and MIDI
                if (kdmapiInitialized) {
                    // Stop all currently playing notes before cleanup
                    for (int channel = 0; channel < 16; channel++) {
                        unsigned long allNotesOff = 0xB0 | channel | (0x7B << 8) | (0 << 16); // CC 123 = All Notes Off
                        SendDirectData(allNotesOff);
                    }
                    TerminateKDMAPIStream();
                    kdmapiInitialized = false;
                }
                midiLoaded = false;
                tracks.clear();
                eventStream.clear();
                currentState = STATE_MENU;
                SetWindowTitle("JIDI Player - Mode Selection");
                isPaused = false; // Reset pause state
                continue;
            }

            if (IsKeyPressed(KEY_SPACE)) {
                // Toggle pause/play
                if (isPaused) {
                    // Resume playback - adjust start time to account for pause duration
                    auto pauseDuration = std::chrono::duration_cast<std::chrono::microseconds>(
                        std::chrono::steady_clock::now() - pauseStartTime).count();

                    // Move the start time forward by the pause duration
                    playbackStartTime = playbackStartTime + std::chrono::microseconds(pauseDuration);
                    isPaused = false;
                } else {
                    // Pause playback - record when we paused
                    pauseStartTime = std::chrono::steady_clock::now();
                    isPaused = true;
                }
            }

            if (IsKeyPressed(KEY_R)) {
                // Reset playback to beginning
                // First, stop all currently playing notes
                for (int channel = 0; channel < 16; channel++) {
                    unsigned long allNotesOff = 0xB0 | channel | (0x7B << 8) | (0 << 16); // CC 123 = All Notes Off
                    SendDirectData(allNotesOff);
                }

                playbackStartTime = std::chrono::steady_clock::now();
                nextEventIndex = 0;
                lastTick = 0;
                accumulatedMicroseconds = 0;
                isPaused = false;
            }

            if (IsKeyPressed(KEY_F) && firstNoteTick != UINT32_MAX) {
                // Skip to first note
                double skipTime = firstNoteTick * microsecondsPerTick;
                playbackStartTime = std::chrono::steady_clock::now() - std::chrono::microseconds((uint64_t)skipTime);
                // Removed skip logging for performance
            }

            // Quality and scroll speed controls
            if (IsKeyPressed(KEY_Q)) {
                // Cycle through quality modes: Auto -> 100% -> 75% -> 50% -> 25% -> Auto
                static int qualityMode = 0; // 0=auto, 1=100%, 2=75%, 3=50%, 4=25%
                qualityMode = (qualityMode + 1) % 5;

                switch (qualityMode) {
                    case 0: // Auto
                        adaptiveQualityEnabled = true;
                        break;
                    case 1: // 100%
                        adaptiveQualityEnabled = false;
                        qualityScale = 1.0f;
                        break;
                    case 2: // 75%
                        adaptiveQualityEnabled = false;
                        qualityScale = 0.75f;
                        break;
                    case 3: // 50%
                        adaptiveQualityEnabled = false;
                        qualityScale = 0.5f;
                        break;
                    case 4: // 25%
                        adaptiveQualityEnabled = false;
                        qualityScale = 0.25f;
                        break;
                }
            }

            if (IsKeyPressed(KEY_UP)) {
                // Increase scroll speed
                scrollSpeedMultiplier = std::min(4.0f, scrollSpeedMultiplier + 0.25f);
            }

            if (IsKeyPressed(KEY_DOWN)) {
                // Decrease scroll speed
                scrollSpeedMultiplier = std::max(0.25f, scrollSpeedMultiplier - 0.25f);
            }

            // Calculate elapsed time (pause-aware)
            auto now = std::chrono::steady_clock::now();
            auto elapsedMicroseconds = std::chrono::duration_cast<std::chrono::microseconds>(now - playbackStartTime).count();

            // --- Process All Due Events (only if not paused) ---
            if (!isPaused) {
                while (nextEventIndex < eventStream.size()) {
                    const auto& event = eventStream[nextEventIndex];
                    uint32_t deltaTick = event.tick - lastTick;
                    uint64_t scheduledTime = accumulatedMicroseconds + (uint64_t)(deltaTick * microsecondsPerTick);

                    if (elapsedMicroseconds >= scheduledTime) {
                        if (event.type == PlaybackEvent::TEMPO) {
                            microsecondsPerTick = (double)event.tempoValue / (double)ppq;
                        } else if (event.type == PlaybackEvent::NOTE) {
                            SendDirectData(event.status | (event.note << 8) | (event.velocity << 16));
                        }
                        accumulatedMicroseconds = scheduledTime;
                        lastTick = event.tick;
                        nextEventIndex++;
                    } else {
                        break;
                    }
                }
            } // End of pause check

            // Check if playback is finished
            bool playbackFinished = (nextEventIndex >= eventStream.size());

            // --- Calculate Current Visualizer Tick (Corrected for Perfect Sync) ---
            uint64_t visualizerTick = 0;
            if (microsecondsPerTick > 0) {
                // if (isPaused) { visualizerTick = lastTick; } else { ... } // Your pause logic here
                uint64_t microsSinceLastEvent = elapsedMicroseconds - accumulatedMicroseconds;
                visualizerTick = lastTick + (uint64_t)(microsSinceLastEvent / microsecondsPerTick);
            }

            // Removed debug tick logging for performance

            // --- Drawing ---
            BeginDrawing();
            ClearBackground(BLACK);

            // Update performance metrics for adaptive quality
            UpdatePerformanceMetrics();

            // Safe drawing with error handling
            try {
                DrawVisualizerNotes(tracks, (int)visualizerTick, ppq, searchStartIndices);
            } catch (const std::exception& e) {
                // If drawing fails, show error message instead of crashing
                DrawText("VISUALIZER ERROR - Check console", 10, 200, 20, RED);
                std::cerr << "Visualizer drawing error: " << e.what() << std::endl;
            }

            // Clean, minimal status display (top-left corner)
            DrawText(TextFormat("%.1fs", elapsedMicroseconds / 1000000.0), 10, 10, 20, WHITE);
            DrawText(TextFormat("%.0f BPM", MidiTiming::MicrosecondsToBPM(currentTempo)), 10, 35, 16, LIGHTGRAY);

            // Performance indicator (top-right corner)
            float currentFPS = GetFPS();
            Color fpsColor = (currentFPS > 50) ? GREEN : (currentFPS > 30) ? YELLOW : RED;
            int fpsX = GetScreenWidth() - 80;
            DrawText(TextFormat("%.0f FPS", currentFPS), fpsX, 10, 16, fpsColor);

            // Simple status indicator (top-right, below FPS)
            if (isPaused) {
                DrawText("PAUSED", fpsX - 20, 30, 16, YELLOW);
            } else if (playbackFinished) {
                DrawText("FINISHED", fpsX - 30, 30, 16, ORANGE);
            }

            // Single, clean controls display (bottom of screen) - no Unicode
            int helpY = GetScreenHeight() - 40;
            DrawText("SPACE=Pause  R=Reset  F=Skip  Q=Quality  UP/DOWN=Speed", 10, helpY, 14, LIGHTGRAY);

            // Advanced debug info (only when Ctrl is held) - positioned on right side
            if (IsKeyDown(KEY_LEFT_CONTROL) || IsKeyDown(KEY_RIGHT_CONTROL)) {
                // Position debug info on the right side to avoid overlap
                int debugX = GetScreenWidth() - 400;
                int debugY = 60;

                // Semi-transparent background for better readability
                DrawRectangle(debugX - 10, debugY - 10, 390, 120, Color{0, 0, 0, 180});

                // Show detailed debug information
                DrawText(TextFormat("DEBUG MODE - Hold CTRL"), debugX, debugY, 16, ORANGE);
                DrawText(TextFormat("Tick: %d | Audio: %d", visualizerTick, lastTick), debugX, debugY + 20, 14, YELLOW);
                DrawText(TextFormat("Events: %zu/%zu (%.1f%%)", nextEventIndex, eventStream.size(),
                    (nextEventIndex * 100.0) / eventStream.size()), debugX, debugY + 40, 14, YELLOW);
                DrawText(TextFormat("PPQ: %d | Tempo: %d us", ppq, currentTempo), debugX, debugY + 60, 14, YELLOW);
                DrawText(TextFormat("μs/tick: %.2f | Elapsed: %.1fs", microsecondsPerTick, elapsedMicroseconds / 1000000.0), debugX, debugY + 80, 14, YELLOW);
                const char* qualityModeText = adaptiveQualityEnabled ? "Auto" : "Manual";
                DrawText(TextFormat("Quality: %.0f%% (%s) | Speed: %.2fx", qualityScale * 100, qualityModeText, scrollSpeedMultiplier), debugX, debugY + 100, 14, YELLOW);

                // Sync status with color (handle large tick values safely)
                int64_t syncDiff = static_cast<int64_t>(visualizerTick) - static_cast<int64_t>(lastTick);
                Color syncColor = (abs(syncDiff) < 10) ? GREEN : (abs(syncDiff) < 100) ? YELLOW : RED;
                DrawText(TextFormat("Sync: %lld ticks", syncDiff), debugX, debugY + 120, 14, syncColor);
            }

            // Show first note info if available
            //if (firstNoteTick != UINT32_MAX) {
            //    double firstNoteTime = firstNoteTick * microsecondsPerTick / 1000.0;
            //    DrawText(TextFormat("First note: tick %d (%.1f ms)", firstNoteTick, firstNoteTime), 10, 300, 16, SKYBLUE);
            //}  

            EndDrawing();
        } // End of if (midiLoaded)
        break;
        } // End of case STATE_PLAYING
        } // End of switch
    } // End of main loop

    // === Cleanup ===
    CloseWindow();
    if (kdmapiInitialized) {
        TerminateKDMAPIStream();
    }
    return 0;
}