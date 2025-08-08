// visualizer.cpp (Fixed Event Processing & 64-bit Safe)

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

// Black MIDI optimization constants - 64-bit safe
const size_t MAX_EVENTS_PER_FRAME = 1000; // Reduced for smoother processing
const uint32_t VISUALIZER_LOOKAHEAD_TICKS = 4000; // View window

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
    DrawText("JIDI Player (64-bit Black MIDI)", GetScreenWidth() / 2 - MeasureText("JIDI Player (64-bit Black MIDI)", 40) / 2, 50, 40, WHITE);
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
    DrawText("Memory usage optimized for 64-bit", GetScreenWidth() / 2 - MeasureText("Memory usage optimized for 64-bit", 20) / 2, 250, 20, LIGHTGRAY);
    DrawText("Processing large file...", GetScreenWidth() / 2 - MeasureText("Processing large file...", 20) / 2, 280, 20, LIGHTGRAY);
}

// ===================================================================
// STREAMING MIDI LOADER FOR BLACK MIDI (Memory Safe)
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
    if (!file || strncmp(header, expectedType, 4) != 0) {
        return {};
    }

    uint32_t length = ntohl(*reinterpret_cast<uint32_t*>(header + 4));

    // --- THE FINAL FIX: Sanity Check ---
    // Protect against malformed files that claim an impossible track size.
    const uint32_t MAX_TRACK_SIZE = 100 * 1024 * 1024; // 100 MB limit
    if (length > MAX_TRACK_SIZE) {
        std::cerr << "WARNING: Track '" << expectedType << "' claims to be " << length 
                  << " bytes, which is too large. Skipping this track." << std::endl;
        file.seekg(length, std::ios_base::cur); // Skip over the chunk
        if (!file) { return {}; } // Check if skipping failed
        return {};
    }
    // --- END FIX ---

    std::vector<uint8_t> data(length);
    file.read(reinterpret_cast<char*>(data.data()), length);
    if (!file) {
        std::cerr << "WARNING: Could not read the full track data." << std::endl;
        return {};
    }
    return data;
}

// Memory-efficient structure for massive black MIDI
struct OptimizedTrackData {
    std::vector<NoteEvent> notes;
    uint8_t channel;
    uint64_t totalNotes = 0; // 64-bit safe
};

bool loadStreamingMidiData(const std::string& filename, std::vector<OptimizedTrackData>& tracks, int& ppq, int& initialTempo, uint64_t& totalNoteCount) {
    std::cout << "Loading streaming Black MIDI file: " << filename << std::endl;
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

    // Reserve memory to prevent constant reallocations
    for (auto& track : tracks) {
        track.notes.reserve(100000); // Reserve space for efficiency
    }

    // Use map instead of unordered_map to prevent crashes on large files
    std::map<uint8_t, NoteEvent> activeNotes[16];

    for (uint16_t t = 0; t < nTracks; ++t) {
        if (t % 5 == 0) {
            std::cout << "Processing track " << t << "/" << nTracks << " (Notes so far: " << totalNoteCount << ")" << std::endl;
        }

        std::vector<uint8_t> trackData = readChunk(file, "MTrk");
        if (trackData.empty()) continue;

        size_t pos = 0;
        uint32_t tick = 0;
        uint8_t runningStatus = 0;
        uint64_t trackNoteCount = 0;

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
                    
                    // Periodically print progress for very large files
                    if (totalNoteCount % 1000000 == 0) {
                        std::cout << "Processed " << totalNoteCount << " notes..." << std::endl;
                    }
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
                        std::cout << "Found tempo: " << tempo << " μs" << std::endl;
                    }
                }
                pos += len;
            } else {
                // Skip other events efficiently
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

        //tracks[t].totalNotes = trackNoteCount;
        //tracks[t].channel = t;
        if (trackNoteCount > 0) {
            std::cout << "Track " << t << ": " << trackNoteCount << " notes" << std::endl;
        }
    }

    // Sort notes by start time for efficient streaming playback
    std::cout << "Sorting notes for optimized streaming playback..." << std::endl;
    for (auto& track : tracks) {
        if (!track.notes.empty()) {
            std::sort(track.notes.begin(), track.notes.end(), 
                     [](const NoteEvent& a, const NoteEvent& b) {
                         return a.endTick < b.endTick; // <-- CORRECTED: Sort by end time
                     });
        }
    }

    auto endTime = std::chrono::steady_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(endTime - startTime);
    
    std::cout << "Loaded " << totalNoteCount << " total notes in " << duration.count() << "ms" << std::endl;
    std::cout << "Memory usage optimized for streaming playback" << std::endl;
    
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

    // Only check first few tracks for tempo events to prevent crashes
    uint16_t maxTracksToCheck = std::min(nTracks, (uint16_t)3);
    
    for (uint16_t t = 0; t < maxTracksToCheck; ++t) {
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
// STREAMING VISUALIZER FOR BLACK MIDI (No Note Limits)
// ===================================================================

void DrawStreamingVisualizerNotes(const std::vector<OptimizedTrackData>& tracks, uint32_t currentTick, int ppq, uint32_t currentTempo) {
    int screenWidth = GetScreenWidth();
    int screenHeight = GetScreenHeight();

    uint16_t validatedPPQ = MidiTiming::ValidatePPQ(ppq);
    uint32_t validatedTempo = (currentTempo >= 200000 && currentTempo <= 2000000) ? currentTempo : MidiTiming::DEFAULT_TEMPO_MICROSECONDS;

    double microsecondsPerTick = MidiTiming::CalculateMicrosecondsPerTick(validatedTempo, validatedPPQ);
    double calculatedViewWindow = (microsecondsPerTick > 0) ? (4.0 * 500000.0 / microsecondsPerTick) : (double)(validatedPPQ * 4);
    const uint32_t viewWindow = std::max(1U, static_cast<uint32_t>(calculatedViewWindow));

    uint64_t totalVisibleNotes = 0; // 64-bit counter

    for (size_t t = 0; t < tracks.size(); ++t) {
        const OptimizedTrackData& track = tracks[t];
        if (track.notes.empty()) continue;

        // Use binary search to find the start range efficiently
        auto startIt = std::lower_bound(track.notes.begin(), track.notes.end(), currentTick,
                                       [](const NoteEvent& note, uint32_t tick) {
                                           return note.endTick < tick;
                                       });

        for (auto it = startIt; it != track.notes.end(); ++it) {
            const NoteEvent& note = *it;

            if (note.startTick > currentTick + viewWindow) break;
            if (note.endTick < currentTick) continue;
            
            float x = ((float)((int64_t)note.startTick - (int64_t)currentTick) / (float)viewWindow) * screenWidth;
            float width = ((float)(note.endTick - note.startTick) / (float)viewWindow) * screenWidth;
            if (width < 0.3f) width = 0.3f;
            
            if (x + width < 0 || x > screenWidth) continue;

            float y = screenHeight - 50.0f - (note.note / 127.0f) * (screenHeight - 100.0f);
            float height = 3.0f;

            bool isActive = (note.startTick <= currentTick && note.endTick > currentTick);
            Color noteColor = isActive ? WHITE : GetTrackColorPFA(note.channel);
            
            // Optimized drawing for black MIDI
            if (width < 0.8f) {
                DrawPixelV(Vector2{(float)(int)x, (float)(int)y}, noteColor);
                DrawPixelV(Vector2{(float)(int)x, (float)(int)y + 1.0f}, noteColor);
            } else {
                DrawRectangleRec({x, y, width, height}, noteColor);
            }
            
            totalVisibleNotes++;
        }
    }
}

// ===================================================================
// MAIN FUNCTION WITH FIXED EVENT PROCESSING
// ===================================================================

int main(int argc, char* argv[]) {
    if (argc > 1) selectedMidiFile = argv[1];

    SetConfigFlags(FLAG_WINDOW_RESIZABLE);
    InitWindow(1280, 720, "JIDI Player - 64-bit Black MIDI");
    //SetTargetFPS(60); // Unlimited FPS for smooth black MIDI

    std::vector<OptimizedTrackData> tracks;
    std::vector<TempoEvent> globalTempoEvents;
    int ppq = 480;
    uint64_t totalNoteCount = 0; // 64-bit safe

    // Fixed event processing structures
    enum class EventType { TEMPO, NOTE_ON, NOTE_OFF };
    using PlaybackItem = std::tuple<uint32_t, EventType, uint8_t, uint8_t, uint8_t, uint32_t>;
    auto cmp = [](const PlaybackItem& a, const PlaybackItem& b) {
        if (std::get<0>(a) != std::get<0>(b)) {
            return std::get<0>(a) > std::get<0>(b);
        }
        return std::get<1>(a) > std::get<1>(b);
    };
    std::priority_queue<PlaybackItem, std::vector<PlaybackItem>, decltype(cmp)> eventQueue;
    
    // Fixed timing variables
    auto playbackStartTime = std::chrono::steady_clock::now();
    auto pauseTime = std::chrono::steady_clock::now();
    uint64_t totalPausedTime = 0;
    uint32_t currentTempo = MidiTiming::DEFAULT_TEMPO_MICROSECONDS;
    double microsecondsPerTick = MidiTiming::CalculateMicrosecondsPerTick(currentTempo, ppq);
    bool isPaused = false;
    uint32_t currentVisualizerTick = 0;
    uint32_t lastProcessedTick = 0; // Track last processed tick to prevent jumping

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
                if (loadStreamingMidiData(selectedMidiFile, tracks, ppq, initialTempo, totalNoteCount) && InitializeKDMAPIStream()) {

                    std::cout << "Collecting tempo events..." << std::endl;
                    globalTempoEvents = collectGlobalTempoEvents(selectedMidiFile);

                    // Initialize tempo properly
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
                    lastProcessedTick = 0;
                    
                    // Build event queue efficiently (streaming approach)
                    while(!eventQueue.empty()) eventQueue.pop();

                    std::cout << "Building streaming event queue..." << std::endl;
                    uint64_t totalEvents = 0;
                    for (const auto& track : tracks) {
                        for (const auto& note : track.notes) {
                            eventQueue.emplace(note.startTick, EventType::NOTE_ON, note.channel, note.note, note.velocity, 0);
                            eventQueue.emplace(note.endTick, EventType::NOTE_OFF, note.channel, note.note, 0, 0);
                            totalEvents += 2;
                            
                            // Progress indicator for very large files
                            if (totalEvents % 2000000 == 0) {
                                std::cout << "Built " << totalEvents << " events..." << std::endl;
                            }
                        }
                    }
                    for (const auto& tempo : globalTempoEvents) {
                        eventQueue.emplace(tempo.tick, EventType::TEMPO, 0, 0, 0, tempo.tempoMicroseconds);
                        totalEvents++;
                    }
                    
                    std::cout << "Created " << totalEvents << " total playback events" << std::endl;
                    
                    playbackStartTime = std::chrono::steady_clock::now();
                    currentState = STATE_PLAYING;
                    SetWindowTitle(TextFormat("JIDI Player - %s (%llu notes)", GetFileName(selectedMidiFile.c_str()), totalNoteCount));
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
                    SetWindowTitle("JIDI Player - 64-bit Black MIDI");
                    continue;
                }
                
                // FIXED timing calculation - no jumping
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

                // Calculate current tick smoothly
                if (microsecondsPerTick > 0) {
                    uint32_t newTick = (uint32_t)(elapsedMicroseconds / microsecondsPerTick);
                    // Prevent jumping by ensuring smooth progression
                    if (newTick > currentVisualizerTick) {
                        currentVisualizerTick = newTick;
                    }
                }

                // FIXED event processing - prevent BPM jumps
                if (!isPaused) {
                    size_t eventsProcessedThisFrame = 0;
                    
                    while (!eventQueue.empty() && eventsProcessedThisFrame < MAX_EVENTS_PER_FRAME) {
                        auto const& [eventTick, type, channel, note, velocity, tempoValue] = eventQueue.top();
                        
                        // Only process events that should happen now or in the past
                        if (eventTick <= currentVisualizerTick) {
                            eventQueue.pop();

                            if (type == EventType::TEMPO) {
                                // Apply tempo changes carefully to prevent jumping
                                if (tempoValue >= 200000 && tempoValue <= 2000000) {
                                    // --- THE FINAL TIMING FIX ---
                                    // 1. Calculate the real time that has passed under the OLD tempo
                                    uint64_t timeElapsedThisStep = (uint64_t)((eventTick - lastProcessedTick) * microsecondsPerTick);

                                    // 2. Adjust the start time and paused time to anchor the clock to this event
                                    auto adjustment = std::chrono::microseconds(timeElapsedThisStep);
                                    playbackStartTime += adjustment;
                                    if(isPaused) pauseTime += adjustment;
                                    
                                    // 3. Now, safely update the tempo for all FUTURE calculations
                                    currentTempo = tempoValue;
                                    microsecondsPerTick = MidiTiming::CalculateMicrosecondsPerTick(currentTempo, ppq);
                                    // --- END FIX ---
                                }
                            } else {
                                uint8_t status = (type == EventType::NOTE_ON) ? (0x90 | channel) : (0x80 | channel);
                                SendDirectData(status | (note << 8) | (velocity << 16));
                            }
                            
                            eventsProcessedThisFrame++;
                            lastProcessedTick = eventTick;
                        } else {
                            // Event is in the future, stop processing
                            break;
                        }
                    }
                }

                // Drawing with performance info
                BeginDrawing();
                ClearBackground(BLACK);
                
                DrawStreamingVisualizerNotes(tracks, currentVisualizerTick, ppq, currentTempo);
                
                // Performance info
                DrawText(TextFormat("%.1f BPM", MidiTiming::MicrosecondsToBPM(currentTempo)), 10, 10, 20, WHITE);
                DrawText(TextFormat("Notes: %llu", totalNoteCount), 10, 40, 20, WHITE);
                DrawText(TextFormat("Tick: %u", currentVisualizerTick), 10, 70, 20, WHITE);
                DrawText(TextFormat("Queue: %zu", eventQueue.size()), 10, 100, 20, WHITE);
                
                if(isPaused) DrawText("PAUSED", GetScreenWidth()/2 - MeasureText("PAUSED", 20)/2, 20, 20, YELLOW);
                
                DrawFPS(10, GetScreenHeight() - 25);
                EndDrawing();
                break;
            }
        }
    }

    TerminateKDMAPIStream();
    CloseWindow();
    return 0;
}