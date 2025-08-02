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

// Loading progress variables
static std::string loadingStatus = "";
static int loadingProgress = 0;
static int totalTracks = 0;
static int processedTracks = 0;
static int totalEvents = 0;
static int processedEvents = 0;



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

struct PlaybackEvent {
    enum EventType { NOTE, TEMPO };

    EventType type;
    uint32_t tick;

    // Data for Note events
    uint8_t status;   // 0x90 for Note On, 0x80 for Note Off
    uint8_t note;
    uint8_t velocity;

    // Data for Tempo events
    uint32_t tempoValue; // in microseconds per quarter note

    // Comparator to allow sorting events chronologically
    bool operator<(const PlaybackEvent& other) const {
        if (tick == other.tick) {
            // Tempo events should come before note events at the same tick
            return type == TEMPO && other.type == NOTE;
        }
        return tick < other.tick;
    }
};

struct GlobalTempoEvent {
    uint32_t tick;
    uint32_t tempoMicroseconds;
};

// Function to collect all tempo events from the MIDI file
std::vector<GlobalTempoEvent> collectGlobalTempoEvents(const std::string& filename) {
    std::vector<GlobalTempoEvent> tempoEvents;

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
              [](const GlobalTempoEvent& a, const GlobalTempoEvent& b) {
                  return a.tick < b.tick;
              });

    return tempoEvents;
}

float GetNoteY(uint8_t note) {
    // Full MIDI range positioning (0-127)
    return GetScreenHeight() - 50 - ((float)note / 127.0f) * (GetScreenHeight() - 100);
}

void DrawVisualizerNotesDefault(const std::vector<TrackData>& tracks, int currentTick, int ppq) {
    // Default mode: All tracks combined into piano roll view
    const int viewWindow = ppq * 4; // Show 4 quarter notes ahead
    const int maxNotesToDraw = 65536;

    int notesDrawn = 0;
    int screenWidth = GetScreenWidth();
    int screenHeight = GetScreenHeight();

    // Piano roll style - notes positioned by pitch
    for (size_t t = 0; t < tracks.size() && notesDrawn < maxNotesToDraw; ++t) {
        const TrackData& track = tracks[t];
        if (track.notes.empty()) continue;

        Color trackColor = GetTrackColorPFA((int)t);

        for (const NoteEvent& note : track.notes) {
            if (notesDrawn >= maxNotesToDraw) break;

            // Skip notes that are completely in the past
            if (note.endTick < currentTick) continue;

            // Skip notes that are too far in the future
            if (note.startTick > currentTick + viewWindow) continue;

            // Calculate horizontal position (time-based)
            float x = ((float)(note.startTick - currentTick) / viewWindow) * screenWidth;
            float width = ((float)(note.endTick - note.startTick) / viewWindow) * screenWidth;

            // Calculate vertical position (pitch-based) - full MIDI range
            float y = screenHeight - 50 - ((float)note.note / 127.0f) * (screenHeight - 100); // Full 128 MIDI keys (0-127)
            float height = 3; // Thin horizontal bars

            // Ensure minimum width for visibility
            if (width < 2) width = 2;

            // More aggressive visibility check - include notes that extend into screen
            if (x + width >= -10 && x <= screenWidth + 10 && y >= 50 && y < screenHeight - 50) {
                // Calculate drawing bounds with proper left edge handling
                float drawX = x;
                float drawWidth = width;

                // Handle left edge clipping more carefully
                if (x < 0) {
                    drawX = 0;
                    drawWidth = width + x; // Reduce width by the amount off-screen
                    if (drawWidth <= 0) continue; // Skip if completely off-screen
                }

                // Handle right edge clipping
                if (drawX + drawWidth > screenWidth) {
                    drawWidth = screenWidth - drawX;
                    if (drawWidth <= 0) continue; // Skip if completely off-screen
                }

                // Draw if there's something visible (minimum 1 pixel)
                if (drawWidth >= 1) {

                // Check if note is currently active
                bool isActive = (note.startTick <= currentTick && note.endTick > currentTick);
                Color noteColor = isActive ?
                    Color{255, 255, 255, 255} : // Active notes are white
                    Color{trackColor.r, trackColor.g, trackColor.b, 180}; // Inactive notes are track color

                    DrawRectangle((int)drawX, (int)y, (int)drawWidth, (int)height, noteColor);

                    // Draw note border for active notes
                    if (isActive) {
                        DrawRectangleLines((int)drawX, (int)y, (int)drawWidth, (int)height, YELLOW);
                    }

                    notesDrawn++;
                }
            }
        }
    }

    // Draw MIDI octave guide lines (full 128 key range)
    for (int octave = 0; octave < 11; octave++) { // MIDI octaves 0-10 (covers 0-127)
        int noteC = octave * 12; // C notes: 0, 12, 24, 36, 48, 60, 72, 84, 96, 108, 120
        if (noteC <= 127) { // Only draw if within MIDI range
            float y = screenHeight - 50 - ((float)noteC / 127.0f) * (screenHeight - 100);
            if (y >= 50 && y < screenHeight - 50) {
                DrawLine(0, (int)y, screenWidth, (int)y, Color{100, 100, 100, 100});
                DrawText(TextFormat("C%d", octave), 5, (int)y - 10, 12, LIGHTGRAY);
            }
        }
    }

    // Debug info
    DrawText(TextFormat("Renders: %d/%d (Default Mode)", notesDrawn, maxNotesToDraw), 10, 160, 20, WHITE);
}

void DrawVisualizerNotesTracks(const std::vector<TrackData>& tracks, int currentTick, int ppq) {
    // ADVANCED: Full tracks support - no limits
    const int viewWindow = ppq * 4; // Show 4 quarter notes ahead
    const int maxNotesToDraw = 65536; // Higher limit for complex MIDI files

    int notesDrawn = 0;
    int tracksWithNotes = 0;

    // Calculate dynamic track height based on ALL tracks with notes (not just visible ones)
    int screenHeight = GetScreenHeight();

    // Count all tracks that have notes (regardless of current view window)
    for (size_t t = 0; t < tracks.size(); ++t) {
        if (!tracks[t].notes.empty()) {
            tracksWithNotes++;
        }
    }

    // Calculate track height for 16 channels - use more screen space
    int availableHeight = screenHeight - 80; // Leave less space for UI
    int trackHeight = tracksWithNotes > 0 ? availableHeight / tracksWithNotes : 50;
    if (trackHeight < 8) trackHeight = 8; // Better minimum height for 16 channels
    if (trackHeight > 60) trackHeight = 60; // Good maximum height for readability

    // Debug info for unlimited tracks
    DrawText(TextFormat("Total Tracks: %d, With Notes: %d, Height: %d", (int)tracks.size(), tracksWithNotes, trackHeight), 10, 10, 16, WHITE);

    // Process ALL tracks that have notes (TRUE UNLIMITED TRACKS)
    int currentTrackIndex = 0; // Separate counter for track positioning
    for (size_t t = 0; t < tracks.size(); ++t) { // Removed note limit to show ALL tracks
        const TrackData& track = tracks[t];

        // Skip tracks with no notes
        if (track.notes.empty()) continue;

        // Show ALL tracks with notes - no filtering whatsoever
        // This enables true unlimited tracks support for any MIDI file

        Color trackColor = GetTrackColorPFA((int)t);
        int trackY = 30 + (currentTrackIndex * trackHeight); // Start higher up on screen

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

                // Handle left edge clipping more carefully
                if (x < 0) {
                    drawX = 0;
                    drawWidth = width + x; // Reduce width by the amount off-screen
                    if (drawWidth <= 0) continue; // Skip if completely off-screen
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
    }

    // Debug: Show performance info
    DrawText(TextFormat("Visible Tracks: %d/%d (Total: %d)", currentTrackIndex, tracksWithNotes, (int)tracks.size()), 10, 160, 20, WHITE);
    DrawText(TextFormat("Renders: %d/%d (Full Tracks Mode)", notesDrawn, maxNotesToDraw), 10, 180, 20, WHITE);
}

// Main drawing function that chooses the appropriate render mode
void DrawVisualizerNotes(const std::vector<TrackData>& tracks, int currentTick, int ppq) {
    if (selectedRenderMode == RENDER_DEFAULT) {
        DrawVisualizerNotesDefault(tracks, currentTick, ppq);
    } else {
        DrawVisualizerNotesTracks(tracks, currentTick, ppq);
    }
}

bool loadVisualizerMidiData(const std::string& filename, std::vector<TrackData>& tracks, int& ppq, int& tempoMicroseconds) {
    std::ifstream file(filename, std::ios::binary);
    if (!file) return false;

    char header[14];
    file.read(header, 14);
    if (std::memcmp(header, "MThd", 4) != 0) return false;

    ppq = (static_cast<uint8_t>(header[12]) << 8) | static_cast<uint8_t>(header[13]);
    tempoMicroseconds = MidiTiming::DEFAULT_TEMPO_MICROSECONDS;

    std::cout << "Raw PPQ from file: " << ppq << std::endl;

    // Handle extreme PPQ values that can cause crashes
    if (ppq > 4800) {
        std::cout << "WARNING: Extremely high PPQ (" << ppq << "), normalizing to 480" << std::endl;
        ppq = 480;
    } else {
        // Validate and correct PPQ if necessary
        ppq = MidiTiming::ValidatePPQ(ppq);
    }

    std::cout << "Using PPQ: " << ppq << std::endl;

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

        uint32_t trackLength = (static_cast<uint8_t>(chunkHeader[4]) << 24) |
                               (static_cast<uint8_t>(chunkHeader[5]) << 16) |
                               (static_cast<uint8_t>(chunkHeader[6]) << 8) |
                               static_cast<uint8_t>(chunkHeader[7]);

        std::vector<uint8_t> trackData(trackLength);
        file.read(reinterpret_cast<char*>(trackData.data()), trackLength);

        TrackData tempTrack;
        uint32_t tick = 0;
        size_t i = 0;
        uint8_t runningStatus = 0;

        // Protection against corrupted files
        const size_t MAX_NOTES_PER_TRACK = 16777216; // Increased limit for complex tracks
        const uint32_t MAX_TRACK_LENGTH = 192000000; // 10MB max track size

        if (trackLength > MAX_TRACK_LENGTH) {
            std::cout << "WARNING: Track too large (" << trackLength << " bytes), skipping" << std::endl;
            file.seekg(trackLength, std::ios::cur);
            continue;
        }

        // Track active notes for proper note-off handling
        std::map<std::pair<uint8_t, uint8_t>, size_t> activeNotes; // (note, channel) -> index in tempTrack.notes

        while (i < trackData.size() && tempTrack.notes.size() < MAX_NOTES_PER_TRACK) {
            // Read variable-length delta time
            uint32_t delta = 0;
            while (i < trackData.size() && (trackData[i] & 0x80)) {
                delta = (delta << 7) | (trackData[i++] & 0x7F);
            }
            if (i < trackData.size()) delta = (delta << 7) | (trackData[i++] & 0x7F);
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
                        std::cout << "WARNING: Track has too many notes, stopping at " << MAX_NOTES_PER_TRACK << std::endl;
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
                  << " notes, last tick: " << tick << std::endl;

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
        for (const auto& note : track.notes) {
            if (note.startTick < earliestNote) earliestNote = note.startTick;
            if (note.startTick > latestNote) latestNote = note.startTick;
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

int main() {
    // === Initialization ===
    InitWindow(1280, 720, "JIDI Player - Mode Selection");
    //SetTargetFPS(60); // Enable frame rate limiting to prevent crashes

    // Initialize variables for MIDI playback (will be loaded when user starts)
    std::vector<TrackData> tracks;
    int ppq = 480;
    int initialTempo = 500000;
    bool midiLoaded = false;
    bool kdmapiInitialized = false;

    // Variables for playback state (initialized when entering STATE_PLAYING)
    std::vector<PlaybackEvent> eventStream;
    std::vector<GlobalTempoEvent> globalTempoEvents;
    uint32_t firstNoteTick = UINT32_MAX;
    auto playbackStartTime = std::chrono::steady_clock::now();
    size_t nextEventIndex = 0;
    uint32_t lastTick = 0;
    double microsecondsPerTick = MidiTiming::CalculateMicrosecondsPerTick(initialTempo, ppq);
    uint64_t accumulatedMicroseconds = 0;
    uint32_t currentTempo = initialTempo;
    std::vector<std::pair<uint32_t, uint32_t>> tempoMap;

    // Function to initialize MIDI playback with progress updates
    auto initializeMidiPlayback = [&]() -> bool {
        loadingStatus = "Loading MIDI file...";
        loadingProgress = 10;

        // Load MIDI data
        tracks.clear();
        if (!loadVisualizerMidiData(selectedMidiFile.c_str(), tracks, ppq, initialTempo)) {
            loadingStatus = "Failed to load MIDI file!";
            return false;
        }

        loadingProgress = 30;
        loadingStatus = "Initializing audio...";

        // Initialize KDMAPI
        if (!kdmapiInitialized) {
            if (!InitializeKDMAPIStream()) {
                loadingStatus = "Failed to initialize audio!";
                return false;
            }
            kdmapiInitialized = true;
        }

        loadingProgress = 50;
        loadingStatus = "Processing tempo events...";

        // Collect global tempo events
        globalTempoEvents = collectGlobalTempoEvents(selectedMidiFile.c_str());

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

        // Build tempo map for accurate tick calculation
        tempoMap.clear();
        tempoMap.push_back({0, initialTempo});

        for (const auto& event : eventStream) {
            if (event.type == PlaybackEvent::TEMPO) {
                tempoMap.push_back({event.tick, event.tempoValue});
            }
        }

        loadingProgress = 100;
        loadingStatus = "Ready!";

        midiLoaded = true;
        return true;
    };

    // Function to calculate tick from elapsed time using tempo map
    auto calculateTickFromTime = [&](uint64_t elapsedMicroseconds) -> uint32_t {
        uint64_t timeRemaining = elapsedMicroseconds;
        uint32_t currentTick = 0;
        uint32_t currentTempo = initialTempo;

        for (size_t i = 1; i < tempoMap.size(); i++) {
            uint32_t nextTempoTick = tempoMap[i].first;
            uint32_t nextTempo = tempoMap[i].second;

            // Calculate time needed to reach next tempo change
            uint32_t ticksToNextTempo = nextTempoTick - currentTick;
            double microsecondsPerTickCurrent = MidiTiming::CalculateMicrosecondsPerTick(currentTempo, ppq);
            uint64_t timeToNextTempo = (uint64_t)(ticksToNextTempo * microsecondsPerTickCurrent);

            if (timeRemaining <= timeToNextTempo) {
                // Target time is before next tempo change
                uint32_t additionalTicks = (uint32_t)(timeRemaining / microsecondsPerTickCurrent);
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
        uint32_t additionalTicks = (uint32_t)(timeRemaining / microsecondsPerTickCurrent);
        return currentTick + additionalTicks;
    };

    // === Main Loop ===
    bool shouldExit = false;
    while (!shouldExit) {
        // Completely override window close behavior - ignore ALL automatic closes
        // Only allow manual shouldExit = true
        if (WindowShouldClose()) {
            // Force window to stay open by clearing the close flag
            // This prevents ESC, BACKSPACE, or any other key from closing
            continue;
        }
        // Handle state transitions
        if (currentState == STATE_MENU) {
            // Menu state - show mode selection GUI

            // Handle CTRL+Q to close application (safer combination)
            if (IsKeyPressed(KEY_Q) && (IsKeyDown(KEY_LEFT_CONTROL) || IsKeyDown(KEY_RIGHT_CONTROL))) {
                shouldExit = true;
                break;
            }

            BeginDrawing();
            DrawModeSelectionMenu();
            EndDrawing();
            continue;
        }

        if (currentState == STATE_LOADING) {
            // Loading state - show progress and load MIDI

            // Handle BACKSPACE to cancel loading
            if (IsKeyPressed(KEY_BACKSPACE)) {
                currentState = STATE_MENU;
                // Reset loading state
                static bool loadingStarted = false;
                loadingStarted = false;
                continue;
            }

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
                    loadingProgress = 100;
                    loadingStatus = "Ready!";
                    loadingComplete = true;
                } else {
                    // Loading failed, go back to menu
                    currentState = STATE_MENU;
                    loadingStarted = false;
                    continue;
                }
            } else if (elapsed > 1500) {
                // Show "Ready!" for a moment, then start playing with 3-second preview
                currentState = STATE_PLAYING;
                SetWindowTitle(TextFormat("JIDI Player - %s (%s Mode)",
                    selectedMidiFile.c_str(),
                    selectedRenderMode == RENDER_DEFAULT ? "Default" : "Tracks"));
                loadingStarted = false;
            }
            continue;
        }

        // Playing state - MIDI playback and visualization with 3-second play-ahead
        if (currentState == STATE_PLAYING && midiLoaded) {
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

            // Calculate elapsed time (pause-aware)
            auto now = std::chrono::steady_clock::now();
            uint64_t elapsedMicroseconds = std::chrono::duration_cast<std::chrono::microseconds>(now - playbackStartTime).count();

            // --- Process All Due Events (only if not paused) ---
            if (!isPaused) {
                while (nextEventIndex < eventStream.size()) {
                const auto& event = eventStream[nextEventIndex];
                uint32_t deltaTick = event.tick - lastTick;

                // Calculate the time for this event using CURRENT tempo for the delta
                uint64_t deltaTime = (uint64_t)(deltaTick * microsecondsPerTick);
                uint64_t scheduledTime = accumulatedMicroseconds + deltaTime;

                if (elapsedMicroseconds >= scheduledTime) {
                    // Time to process this event
                    if (event.type == PlaybackEvent::TEMPO) {
                        // Process tempo changes FIRST, before notes at the same tick
                        if (event.tempoValue >= 200000 && event.tempoValue <= 1000000) {
                            currentTempo = event.tempoValue;
                            microsecondsPerTick = MidiTiming::CalculateMicrosecondsPerTick(event.tempoValue, ppq);
                            // Removed verbose tempo change logging for performance
                        }
                        // Removed invalid tempo logging for performance
                    } else if (event.type == PlaybackEvent::NOTE) {
                        // Send audio immediately (no delay - visual is the one that's delayed)
                        unsigned long msg = event.status | (event.note << 8) | (event.velocity << 16);
                        SendDirectData(msg);
                    }

                    // Update state for the next event
                    accumulatedMicroseconds = scheduledTime;
                    lastTick = event.tick;
                    nextEventIndex++;
                } else {
                    // Not yet time for the next event, break and wait for the next frame
                    break;
                }
                }
            } // End of pause check

            // Check if playback is finished
            bool playbackFinished = (nextEventIndex >= eventStream.size());

            // --- Calculate Current Visualizer Tick ---
            // Simple sync: visual and audio play together normally
            uint32_t visualizerTick;
            if (isPaused) {
                // Use the time when pause started for visualization
                auto pauseElapsed = std::chrono::duration_cast<std::chrono::microseconds>(pauseStartTime - playbackStartTime).count();
                visualizerTick = calculateTickFromTime(pauseElapsed);
            } else {
                visualizerTick = calculateTickFromTime(elapsedMicroseconds);
            }

            // Removed debug tick logging for performance

            // --- Drawing ---
            BeginDrawing();
            ClearBackground(BLACK);
            DrawVisualizerNotes(tracks, visualizerTick, ppq);

            // Simple debug information
            DrawText(TextFormat("Tick: %d", visualizerTick), 10, 10, 20, WHITE);
            DrawText(TextFormat("Time: %.2fs", elapsedMicroseconds / 1000000.0), 10, 35, 20, WHITE);
            DrawText(TextFormat("PPQ: %d", ppq), 10, 85, 20, WHITE);
            DrawText(TextFormat("Tempo: %d μs/quarter (%.1f BPM)", currentTempo, MidiTiming::MicrosecondsToBPM(currentTempo)), 10, 110, 20, WHITE);
            DrawText(TextFormat("Events: %zu/%zu", nextEventIndex, eventStream.size()), 10, 135, 20, WHITE);

            // Simple playback status
            if (playbackFinished) {
                DrawText("PLAYBACK FINISHED", 10, 185, 20, ORANGE);
            } else if (!isPaused) {
                DrawText("PLAYING", 10, 185, 20, WHITE);
            } else {
                DrawText("PAUSED", 10, 185, 20, YELLOW);
            }

            // Controls information with pause status (moved down for preview indicator)
            DrawText("Controls:", 10, 230, 16, YELLOW);
            DrawText(TextFormat("SPACE - %s", isPaused ? "Resume" : "Pause"), 10, 250, 16, YELLOW);
            DrawText("R - Reset playback", 10, 270, 16, YELLOW);
            DrawText("F - Skip to first note", 10, 290, 16, YELLOW);
            DrawText("BACKSPACE - Return to menu", 10, 310, 16, YELLOW);
            DrawText("CTRL+Q - Close application", 10, 330, 16, GRAY);

            // Pause status indicator
            if (isPaused) {
                DrawText("PAUSED", GetScreenWidth() - 120, 10, 20, RED);
            }

            // Show first note info if available
            //if (firstNoteTick != UINT32_MAX) {
            //    double firstNoteTime = firstNoteTick * microsecondsPerTick / 1000.0;
            //    DrawText(TextFormat("First note: tick %d (%.1f ms)", firstNoteTick, firstNoteTime), 10, 300, 16, SKYBLUE);
            //}  

            DrawFPS(10, 690); 
            EndDrawing();
        } // End of playing state
    } // End of main loop

    // === Cleanup ===
    CloseWindow();
    if (kdmapiInitialized) {
        TerminateKDMAPIStream();
    }
    return 0;
}