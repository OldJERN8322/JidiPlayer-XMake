// visualizer.cpp (Black MIDI Optimized Version)

#include "visualizer.hpp"
#include "midi_timing.hpp"
#include <fstream>
#include <iostream>
#include <algorithm>
#include <chrono>
#include <map>
#include <vector>
#include <cstdint>
#include <cstring>
#include <queue>
#include <tuple>
#include <unordered_map>
#include "raylib.h"

// Define byte order conversion functions
#ifdef _WIN32
inline uint32_t ntohl(uint32_t netlong) { return ((netlong & 0xFF000000) >> 24) | ((netlong & 0x00FF0000) >> 8) | ((netlong & 0x0000FF00) << 8) | ((netlong & 0x000000FF) << 24); }
inline uint16_t ntohs(uint16_t netshort) { return ((netshort & 0xFF00) >> 8) | ((netshort & 0x00FF) << 8); }
#else
#include <arpa/inet.h>
#endif

// External KDMAPI functions
extern "C" {
    bool InitializeKDMAPIStream();
    void TerminateKDMAPIStream();
    void SendDirectData(unsigned long data);
}

// Global state variables
static AppState currentState = STATE_MENU;
static std::string selectedMidiFile = "test.mid";

// Black MIDI optimization constants
const size_t MAX_VISIBLE_NOTES = 50000;  // Limit visible notes for performance
const size_t MAX_EVENTS_PER_FRAME = 5000; // Limit events processed per frame
const uint32_t VISUALIZER_LOOKAHEAD_TICKS = 2000; // How far ahead to look for notes

// ===================================================================
// UTILITY AND GUI FUNCTIONS
// ===================================================================

inline Color GetTrackColorPFA(int index) {
    static Color pfaColors[] = { MCOLOR1, MCOLOR2, MCOLOR3, MCOLOR4, MCOLOR5, MCOLOR6, MCOLOR7, MCOLOR8, MCOLOR9, MCOLOR10, MCOLOR11, MCOLOR12, MCOLOR13, MCOLOR14, MCOLOR15, MCOLOR16 };
    return pfaColors[index % 16];
}

bool DrawButton(Rectangle bounds, const char* text) {
    bool isHovered = CheckCollisionPointRec(GetMousePosition(), bounds);
    DrawRectangleRec(bounds, isHovered ? GRAY : DARKGRAY);
    DrawRectangleLinesEx(bounds, 2, LIGHTGRAY);
    int textWidth = MeasureText(text, 20);
    DrawText(text, (int)(bounds.x + (bounds.width - textWidth) / 2), (int)(bounds.y + (bounds.height - 20) / 2), 20, WHITE);
    return isHovered && IsMouseButtonPressed(MOUSE_BUTTON_LEFT);
}

void DrawModeSelectionMenu() {
    ClearBackground(DARKGRAY);
    DrawText("JIDI Player (Black MIDI Optimized)", GetScreenWidth() / 2 - MeasureText("JIDI Player (Black MIDI Optimized)", 40) / 2, 50, 40, WHITE);
    if (DrawButton({(float)GetScreenWidth() / 2 - 150, 200, 300, 50}, "Select File (Cycle)")) {
        if (selectedMidiFile == "test.mid") selectedMidiFile = "tau2.5.9 - Second crashpoint.mid";
        else selectedMidiFile = "test.mid";
    }
    DrawText(TextFormat("File: %s", GetFileName(selectedMidiFile.c_str())), GetScreenWidth()/2 - MeasureText(TextFormat("File: %s", GetFileName(selectedMidiFile.c_str())), 20)/2, 260, 20, LIGHTGRAY);

    if (DrawButton({(float)GetScreenWidth() / 2 - 150, 300, 300, 50}, "Start Playback")) {
        currentState = STATE_LOADING;
    }
}

void DrawLoadingScreen() {
    ClearBackground(DARKGRAY);
    DrawText("Loading Black MIDI File...", GetScreenWidth() / 2 - MeasureText("Loading Black MIDI File...", 40) / 2, 200, 40, WHITE);
    DrawText("This may take a while for large files", GetScreenWidth() / 2 - MeasureText("This may take a while for large files", 20) / 2, 250, 20, LIGHTGRAY);
    DrawText("Optimizing for performance...", GetScreenWidth() / 2 - MeasureText("Optimizing for performance...", 20) / 2, 280, 20, LIGHTGRAY);
}

// ===================================================================
// OPTIMIZED MIDI LOADING FUNCTIONS FOR BLACK MIDI
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

std::vector<uint8_t> readChunk(std::ifstream& file, const char* expectedType) {
    char header[8];
    file.read(header, 8);
    if (!file || strncmp(header, expectedType, 4) != 0) return {};
    uint32_t length = ntohl(*reinterpret_cast<uint32_t*>(header + 4));
    std::vector<uint8_t> data(length);
    file.read(reinterpret_cast<char*>(data.data()), length);
    return data;
}

// Optimized structure for black MIDI - stores notes in time-sorted chunks
struct OptimizedTrackData {
    std::vector<NoteEvent> notes;
    size_t currentIndex = 0;  // Current position in sorted notes
    uint8_t channel;
    size_t totalNotes = 0;
};

bool loadOptimizedMidiData(const std::string& filename, std::vector<OptimizedTrackData>& tracks, int& ppq, int& initialTempo, size_t& totalNoteCount) {
    std::cout << "Loading Black MIDI file: " << filename << std::endl;
    tracks.clear();
    tracks.resize(16);
    totalNoteCount = 0;

    auto startTime = std::chrono::steady_clock::now();

    std::ifstream file(filename, std::ios::binary);
    if (!file.is_open()) {
        std::cerr << "Error: Could not open file " << filename << std::endl;
        return false;
    }

    char header[14];
    file.read(header, 14);
    if (!file || strncmp(header, "MThd", 4) != 0) {
        std::cerr << "Error: Invalid MIDI header." << std::endl;
        return false;
    }
    
    uint16_t format = ntohs(*reinterpret_cast<uint16_t*>(header + 8));
    uint16_t nTracks = ntohs(*reinterpret_cast<uint16_t*>(header + 10));
    uint16_t division = ntohs(*reinterpret_cast<uint16_t*>(header + 12));
    ppq = division;

    std::cout << "MIDI Format: " << format << ", Tracks: " << nTracks << ", PPQ: " << ppq << std::endl;

    // Use unordered_map for faster lookups in black MIDI
    std::unordered_map<uint8_t, NoteEvent> activeNotes[16];

    for (uint16_t t = 0; t < nTracks; ++t) {
        if (t % 10 == 0) {
            std::cout << "Processing track " << t << "/" << nTracks << std::endl;
        }

        std::vector<uint8_t> trackData = readChunk(file, "MTrk");
        if (trackData.empty()) continue;

        size_t pos = 0;
        uint32_t tick = 0;
        uint8_t runningStatus = 0;
        size_t trackNoteCount = 0;

        while (pos < trackData.size()) {
            tick += readVarLen(trackData, pos);
            if (pos >= trackData.size()) break;

            uint8_t status = trackData[pos];
            if (status < 0x80) { 
                status = runningStatus; 
            } else { 
                pos++; 
                runningStatus = status;
            }

            uint8_t eventType = status & 0xF0;
            uint8_t channel = status & 0x0F;

            if (eventType == 0x90 && pos + 1 < trackData.size() && trackData[pos+1] > 0) {
                // Note On
                uint8_t note = trackData[pos];
                uint8_t vel = trackData[pos+1];
                activeNotes[channel][note] = { tick, 0, note, vel, channel };
                pos += 2;
            } else if ((eventType == 0x80 || (eventType == 0x90 && pos + 1 < trackData.size() && trackData[pos+1] == 0)) && pos + 1 < trackData.size()) {
                // Note Off
                uint8_t note = trackData[pos];
                auto it = activeNotes[channel].find(note);
                if (it != activeNotes[channel].end()) {
                    it->second.endTick = tick;
                    tracks[channel].notes.push_back(it->second);
                    activeNotes[channel].erase(it);
                    trackNoteCount++;
                    totalNoteCount++;
                }
                pos += 2;
            } else if (status == 0xFF && pos < trackData.size()) {
                // Meta Event
                uint8_t metaType = trackData[pos++];
                uint32_t len = readVarLen(trackData, pos);
                if (metaType == 0x51 && len == 3 && pos + 3 <= trackData.size()) {
                    uint32_t tempo = (trackData[pos] << 16) | (trackData[pos + 1] << 8) | trackData[pos + 2];
                    if (tempo >= 200000 && tempo <= 2000000) {
                        initialTempo = tempo;
                    }
                }
                pos += len;
            } else {
                // Skip other events
                if (eventType == 0xC0 || eventType == 0xD0) {
                    pos += 1;
                } else if (eventType == 0xA0 || eventType == 0xB0 || eventType == 0xE0) {
                    pos += 2;
                } else if (status == 0xF0 || status == 0xF7) {
                    uint32_t len = readVarLen(trackData, pos);
                    pos += len;
                } else {
                    if (pos < trackData.size()) pos++;
                }
            }
        }

        tracks[t].totalNotes = trackNoteCount;
        std::cout << "Track " << t << ": " << trackNoteCount << " notes" << std::endl;
    }

    // Sort notes by start time for efficient playback
    std::cout << "Sorting notes for optimized playback..." << std::endl;
    for (auto& track : tracks) {
        std::sort(track.notes.begin(), track.notes.end(), 
                 [](const NoteEvent& a, const NoteEvent& b) {
                     return a.startTick < b.startTick;
                 });
        track.channel = &track - &tracks[0]; // Set channel index
    }

    auto endTime = std::chrono::steady_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(endTime - startTime);
    
    std::cout << "Loaded " << totalNoteCount << " total notes in " << duration.count() << "ms" << std::endl;
    return true;
}

std::vector<TempoEvent> collectGlobalTempoEvents(const std::string& filename) {
    std::vector<TempoEvent> events;
    std::ifstream file(filename, std::ios::binary);
    if (!file.is_open()) return events;

    char header[14];
    file.read(header, 14);
    if (!file || strncmp(header, "MThd", 4) != 0) return events;
    uint16_t nTracks = ntohs(*reinterpret_cast<uint16_t*>(header + 10));

    for (uint16_t t = 0; t < nTracks && t < 5; ++t) { // Limit to first 5 tracks for tempo events
        std::vector<uint8_t> trackData = readChunk(file, "MTrk");
        if (trackData.empty()) continue;

        size_t pos = 0;
        uint32_t tick = 0;
        uint8_t runningStatus = 0;

        while (pos < trackData.size()) {
            tick += readVarLen(trackData, pos);
            if (pos >= trackData.size()) break;

            uint8_t status = trackData[pos];
            if (status < 0x80) {
                status = runningStatus;
            } else {
                runningStatus = status;
                pos++;
            }

            if (status == 0xFF && pos < trackData.size()) {
                uint8_t metaType = trackData[pos++];
                uint32_t len = readVarLen(trackData, pos);

                if (metaType == 0x51 && len == 3 && pos + 3 <= trackData.size()) {
                    uint32_t tempo = (trackData[pos] << 16) | (trackData[pos + 1] << 8) | trackData[pos + 2];
                    if (tempo >= 200000 && tempo <= 2000000) {
                        events.push_back({ tick, tempo });
                    }
                }
                pos += len;
            } else if (status == 0xF0 || status == 0xF7) {
                uint32_t len = readVarLen(trackData, pos);
                pos += len;
            } else {
                uint8_t eventType = status & 0xF0;
                if (eventType == 0xC0 || eventType == 0xD0) {
                    pos += 1;
                } else if (eventType >= 0x80 && eventType <= 0xE0) {
                    pos += 2;
                } else {
                    if (pos < trackData.size()) pos++;
                }
            }
        }
    }

    std::sort(events.begin(), events.end(), [](const TempoEvent& a, const TempoEvent& b){
        return a.tick < b.tick;
    });

    return events;
}

// ===================================================================
// OPTIMIZED VISUALIZER FOR BLACK MIDI
// ===================================================================

void DrawOptimizedVisualizerNotes(const std::vector<OptimizedTrackData>& tracks, uint32_t currentTick, int ppq, uint32_t currentTempo) {
    int screenWidth = GetScreenWidth();
    int screenHeight = GetScreenHeight();

    uint16_t validatedPPQ = MidiTiming::ValidatePPQ(ppq);
    uint32_t validatedTempo = (currentTempo >= 200000 && currentTempo <= 2000000) ? currentTempo : MidiTiming::DEFAULT_TEMPO_MICROSECONDS;

    double microsecondsPerTick = MidiTiming::CalculateMicrosecondsPerTick(validatedTempo, validatedPPQ);
    double calculatedViewWindow = (microsecondsPerTick > 0) ? (4.0 * 500000.0 / microsecondsPerTick) : (double)(validatedPPQ * 4);
    const uint32_t viewWindow = std::max(1U, static_cast<uint32_t>(calculatedViewWindow));

    size_t totalVisibleNotes = 0;

    for (size_t t = 0; t < tracks.size() && totalVisibleNotes < MAX_VISIBLE_NOTES; ++t) {
        const OptimizedTrackData& track = tracks[t];
        if (track.notes.empty()) continue;

        // Use binary search to find the start range more efficiently
        auto startIt = std::lower_bound(track.notes.begin(), track.notes.end(), currentTick,
                                       [](const NoteEvent& note, uint32_t tick) {
                                           return note.endTick < tick;
                                       });

        for (auto it = startIt; it != track.notes.end() && totalVisibleNotes < MAX_VISIBLE_NOTES; ++it) {
            const NoteEvent& note = *it;

            if (note.startTick > currentTick + viewWindow) break;
            if (note.endTick < currentTick) continue;
            
            float x = ((float)((int)note.startTick - (int)currentTick) / (float)viewWindow) * screenWidth;
            float width = ((float)(note.endTick - note.startTick) / (float)viewWindow) * screenWidth;
            if (width < 0.5f) width = 0.5f;
            
            if (x + width < 0 || x > screenWidth) continue;

            float y = screenHeight - 50.0f - (note.note / 127.0f) * (screenHeight - 100.0f);
            float height = 4.0f; // Thinner notes for black MIDI

            bool isActive = (note.startTick <= currentTick && note.endTick > currentTick);
            Color noteColor = isActive ? WHITE : GetTrackColorPFA(note.channel);
            
            // For extreme black MIDI, draw as points when width is very small
            if (width < 1.0f) {
                DrawPixel((int)x, (int)y, noteColor);
            } else {
                DrawRectangleRec({x, y, width, height}, noteColor);
            }
            
            totalVisibleNotes++;
        }
    }
}

// ===================================================================
// MAIN FUNCTION OPTIMIZED FOR BLACK MIDI
// ===================================================================

int main(int argc, char* argv[]) {
    if (argc > 1) selectedMidiFile = argv[1];

    SetConfigFlags(FLAG_WINDOW_RESIZABLE);
    InitWindow(1280, 720, "JIDI Player - Black MIDI Edition");
    //SetTargetFPS(60);

    std::vector<OptimizedTrackData> tracks;
    std::vector<TempoEvent> globalTempoEvents;
    int ppq = 480;
    size_t totalNoteCount = 0;

    enum class EventType { TEMPO, NOTE_ON, NOTE_OFF };
    using PlaybackItem = std::tuple<uint32_t, EventType, uint8_t, uint8_t, uint8_t, uint32_t>;
    auto cmp = [](const PlaybackItem& a, const PlaybackItem& b) {
        if (std::get<0>(a) != std::get<0>(b)) {
            return std::get<0>(a) > std::get<0>(b);
        }
        return std::get<1>(a) > std::get<1>(b);
    };
    std::priority_queue<PlaybackItem, std::vector<PlaybackItem>, decltype(cmp)> eventQueue;
    
    // Timing variables
    auto playbackStartTime = std::chrono::steady_clock::now();
    auto pauseTime = std::chrono::steady_clock::now();
    uint64_t totalPausedTime = 0;
    uint32_t currentTempo = MidiTiming::DEFAULT_TEMPO_MICROSECONDS;
    double microsecondsPerTick = MidiTiming::CalculateMicrosecondsPerTick(currentTempo, ppq);
    bool isPaused = false;
    uint32_t currentVisualizerTick = 0;

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

                int initialTempo = 0;
                if (loadOptimizedMidiData(selectedMidiFile, tracks, ppq, initialTempo, totalNoteCount) && InitializeKDMAPIStream()) {

                    std::cout << "Collecting tempo events..." << std::endl;
                    globalTempoEvents = collectGlobalTempoEvents(selectedMidiFile);

                    // Initialize tempo
                    if (!globalTempoEvents.empty()) {
                        currentTempo = globalTempoEvents[0].tempoMicroseconds;
                        std::cout << "Using first tempo event: " << MidiTiming::MicrosecondsToBPM(currentTempo) << " BPM" << std::endl;
                    } else if (initialTempo > 0 && initialTempo >= 200000 && initialTempo <= 2000000) {
                        currentTempo = initialTempo;
                        std::cout << "Using tempo from loader: " << MidiTiming::MicrosecondsToBPM(currentTempo) << " BPM" << std::endl;
                    } else {
                        currentTempo = MidiTiming::DEFAULT_TEMPO_MICROSECONDS;
                        std::cout << "Using default tempo: " << MidiTiming::MicrosecondsToBPM(currentTempo) << " BPM" << std::endl;
                    }

                    microsecondsPerTick = MidiTiming::CalculateMicrosecondsPerTick(currentTempo, ppq);
                    isPaused = false;
                    totalPausedTime = 0;
                    currentVisualizerTick = 0;
                    
                    // Build optimized event queue (limit events for performance)
                    while(!eventQueue.empty()) eventQueue.pop();

                    std::cout << "Building event queue (this may take a while)..." << std::endl;
                    size_t totalEvents = 0;
                    for (const auto& track : tracks) {
                        for (const auto& note : track.notes) {
                            eventQueue.emplace(note.startTick, EventType::NOTE_ON, note.channel, note.note, note.velocity, 0);
                            eventQueue.emplace(note.endTick, EventType::NOTE_OFF, note.channel, note.note, 0, 0);
                            totalEvents += 2;
                        }
                    }
                    for (const auto& tempo : globalTempoEvents) {
                        eventQueue.emplace(tempo.tick, EventType::TEMPO, 0, 0, 0, tempo.tempoMicroseconds);
                        totalEvents++;
                    }
                    
                    std::cout << "Created " << totalEvents << " playback events" << std::endl;
                    
                    playbackStartTime = std::chrono::steady_clock::now();
                    currentState = STATE_PLAYING;
                    SetWindowTitle(TextFormat("JIDI Player - %s (%zu notes)", GetFileName(selectedMidiFile.c_str()), totalNoteCount));
                } else {
                    currentState = STATE_MENU;
                }
                break;
            }
            case STATE_PLAYING: {
                // Handle input
                if (IsKeyPressed(KEY_SPACE)) {
                    if (isPaused) {
                        totalPausedTime += std::chrono::duration_cast<std::chrono::microseconds>(
                            std::chrono::steady_clock::now() - pauseTime).count();
                        isPaused = false;
                    } else {
                        pauseTime = std::chrono::steady_clock::now();
                        isPaused = true;
                    }
                }
                
                if (IsKeyPressed(KEY_ESCAPE)) {
                    currentState = STATE_MENU;
                    SetWindowTitle("JIDI Player - Black MIDI Edition");
                    continue;
                }
                
                // Calculate timing
                uint64_t elapsedMicroseconds = 0;
                if (!isPaused) {
                    auto now = std::chrono::steady_clock::now();
                    uint64_t totalElapsed = std::chrono::duration_cast<std::chrono::microseconds>(
                        now - playbackStartTime).count();
                    elapsedMicroseconds = totalElapsed - totalPausedTime;
                } else {
                    uint64_t totalElapsed = std::chrono::duration_cast<std::chrono::microseconds>(
                        pauseTime - playbackStartTime).count();
                    elapsedMicroseconds = totalElapsed - totalPausedTime;
                }

                if (microsecondsPerTick > 0) {
                    currentVisualizerTick = (uint32_t)(elapsedMicroseconds / microsecondsPerTick);
                }

                // Process MIDI events with frame limiting for black MIDI
                if (!isPaused) {
                    size_t eventsProcessedThisFrame = 0;
                    
                    while (!eventQueue.empty() && eventsProcessedThisFrame < MAX_EVENTS_PER_FRAME) {
                        auto const& [eventTick, type, channel, note, velocity, tempoValue] = eventQueue.top();
                        
                        if (currentVisualizerTick >= eventTick) {
                            eventQueue.pop();

                            if (type == EventType::TEMPO) {
                                if (tempoValue >= 200000 && tempoValue <= 2000000) {
                                    currentTempo = tempoValue;
                                    microsecondsPerTick = MidiTiming::CalculateMicrosecondsPerTick(currentTempo, ppq);
                                    currentVisualizerTick = (uint32_t)(elapsedMicroseconds / microsecondsPerTick);
                                }
                            } else {
                                uint8_t status = (type == EventType::NOTE_ON) ? (0x90 | channel) : (0x80 | channel);
                                SendDirectData(status | (note << 8) | (velocity << 16));
                            }
                            
                            eventsProcessedThisFrame++;
                        } else {
                            break;
                        }
                    }
                }

                // Drawing with performance monitoring
                BeginDrawing();
                ClearBackground(BLACK);
                
                DrawOptimizedVisualizerNotes(tracks, currentVisualizerTick, ppq, currentTempo);
                
                // Performance info for black MIDI
                DrawText(TextFormat("%.0f BPM", MidiTiming::MicrosecondsToBPM(currentTempo)), 10, 10, 20, WHITE);
                DrawText(TextFormat("Notes: %zu", totalNoteCount), 10, 40, 20, WHITE);
                DrawText(TextFormat("Tick: %u", currentVisualizerTick), 10, 70, 20, WHITE);
                DrawText(TextFormat("Events: %zu", eventQueue.size()), 10, 100, 20, WHITE);
                
                if(isPaused) DrawText("PAUSED", GetScreenWidth()/2 - MeasureText("PAUSED", 20)/2, 20, 20, YELLOW);
                
                DrawFPS(10, GetScreenHeight() - 24);
                EndDrawing();
                break;
            }
        }
    }

    TerminateKDMAPIStream();
    CloseWindow();
    return 0;
}