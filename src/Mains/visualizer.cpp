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
#include <ctime>
#include <cmath>
#include <queue>
#include <tuple>
#include "raylib.h"
#include "reasings.h"

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
static std::string selectedMidiFile = "Empty"; 
float ScrollSpeed = 0.5f;

int cursorPos = 0;  // caret index in inputBuffer
uint64_t renderNotes = 0, maxRenderNotes = 0;

bool isHUD = true;
bool inputActive = false;
std::string inputBuffer;

float DWidth = 270.0f, DHeight = 125.0f;
uint64_t noteCounter = 0, noteTotal = 0;

std::string FormatWithCommas(uint64_t value) {
    std::string num = std::to_string(value);
    int insertPosition = num.length() - 3;
    while (insertPosition > 0) {
        num.insert(insertPosition, ",");
        insertPosition -= 3;
    }
    return num;
}

// ===================================================================
// EASING FUNCTIONS IMPLEMENTATION
// ===================================================================
float EaseInBack(float t) {
    const float c1 = 1.70158f;
    const float c3 = c1 + 1.0f;
    return c3 * t * t * t - c1 * t * t;
}

float EaseOutBack(float t) {
    const float c1 = 1.70158f;
    const float c3 = c1 + 1.0f;
    return 1.0f + c3 * std::pow(t - 1.0f, 3.0f) + c1 * std::pow(t - 1.0f, 2.0f);
}

// ===================================================================
// NOTIFICATION SYSTEM IMPLEMENTATION
// ===================================================================

NotificationManager g_NotificationManager;

Notification::Notification(const std::string& txt, Color bgColor, float w, float h, float dur)
    : text(txt), backgroundColor(bgColor), width(w), height(h), duration(dur),
      targetY(0), currentY(-h), isVisible(true), isDismissing(false) {
    startTime = std::chrono::steady_clock::now();
    dismissTime = startTime + std::chrono::milliseconds(static_cast<int>(dur * 1000));
}

void NotificationManager::SendNotification(float width, float height, Color backgroundColor, const std::string& text, float seconds) {
    float newY = TOP_MARGIN;
    for (const auto& notification : notifications) {
        if (notification.isVisible) {
            newY += notification.height + NOTIFICATION_SPACING;
        }
    }

    Notification newNotification(text, backgroundColor, width, height, seconds);
    newNotification.targetY = newY;
    newNotification.currentY = -height;
    
    notifications.push_back(newNotification);
}

void NotificationManager::Update() {
    auto currentTime = std::chrono::steady_clock::now();
    for (auto it = notifications.begin(); it != notifications.end();) {
        auto& notification = *it;
        if (!notification.isDismissing && currentTime >= notification.dismissTime) {
            notification.isDismissing = true;
        }

        float animationProgress = 0.0f;
        if (notification.isDismissing) {
            auto dismissStartTime = notification.dismissTime;
            auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(currentTime - dismissStartTime);
            animationProgress = elapsed.count() / (ANIMATION_DURATION * 1000.0f);
            
            if (animationProgress >= 1.0f) {
                it = notifications.erase(it);
                continue;
            }

            float startY = notification.targetY;
            float endY = -notification.height;
            notification.currentY = startY + (endY - startY) * EaseInBack(animationProgress);
            
        } else {
            auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(currentTime - notification.startTime);
            animationProgress = elapsed.count() / (ANIMATION_DURATION * 1000.0f);
            
            if (animationProgress >= 1.0f) {
                animationProgress = 1.0f;
            }

            float startY = -notification.height;
            float endY = notification.targetY;
            notification.currentY = startY + (endY - startY) * EaseOutBack(animationProgress);
        }
        
        ++it;
    }

    float currentTargetY = TOP_MARGIN;
    for (auto& notification : notifications) {
        if (!notification.isDismissing) {
            notification.targetY = currentTargetY;
            currentTargetY += notification.height + NOTIFICATION_SPACING;
        }
    }
}

void NotificationManager::Draw() {
    const int fontSize = 20;
    const float padding = 15.0f;
    const float cornerRadius = 0.5f;
    
    for (const auto& notification : notifications) {
        if (!notification.isVisible) continue;
        
        float centerX = GetScreenWidth() / 2.0f;
        float notificationX = centerX - notification.width / 2.0f;
        float notificationY = notification.currentY;

        Rectangle notificationRect = {
            notificationX,
            notificationY,
            notification.width,
            notification.height
        };

        Color BGColor = {
            static_cast<unsigned char>(notification.backgroundColor.r),
            static_cast<unsigned char>(notification.backgroundColor.g),
            static_cast<unsigned char>(notification.backgroundColor.b),
            192
        };

        DrawRectangleRounded(notificationRect, cornerRadius, 16, BGColor);
        float lineThickness = 2.0f;
        DrawRectangleRoundedLinesEx(notificationRect, cornerRadius, 16, lineThickness, Color {255,255,255,64});

        std::vector<std::string> wrappedLines = WrapText(notification.text, fontSize, notification.width - 2 * padding);
        
        float textY = notificationY + padding;
        for (const auto& line : wrappedLines) {
            float textWidth = MeasureText(line.c_str(), fontSize);
            float textX = centerX - textWidth / 2.0f;

            DrawText(line.c_str(), static_cast<int>(textX + 1), static_cast<int>(textY + 1), fontSize, BLACK);
            DrawText(line.c_str(), static_cast<int>(textX), static_cast<int>(textY), fontSize, WHITE);
            
            textY += fontSize + 2;
        }
    }
}

std::vector<std::string> NotificationManager::WrapText(const std::string& text, int fontSize, float maxWidth) {
    std::vector<std::string> lines;
    std::string currentLine = "";
    std::string word = "";
    
    for (size_t i = 0; i <= text.length(); ++i) {
        char c = (i < text.length()) ? text[i] : ' ';
        
        if (c == ' ' || c == '\n' || i == text.length()) {
            if (!word.empty()) {
                std::string testLine = currentLine.empty() ? word : currentLine + " " + word;
                
                if (MeasureText(testLine.c_str(), fontSize) <= maxWidth) {
                    currentLine = testLine;
                } else {
                    if (!currentLine.empty()) {
                        lines.push_back(currentLine);
                        currentLine = word;
                    } else {
                        // Word is too long for line, break it
                        lines.push_back(word);
                        currentLine = "";
                    }
                }
                word = "";
            }
            
            if (c == '\n') {
                if (!currentLine.empty()) {
                    lines.push_back(currentLine);
                    currentLine = "";
                }
            }
        } else {
            word += c;
        }
    }
    
    if (!currentLine.empty()) {
        lines.push_back(currentLine);
    }
    
    return lines;
}

Rectangle NotificationManager::MeasureTextBounds(const std::string& text, int fontSize, float maxWidth) {
    std::vector<std::string> lines = WrapText(text, fontSize, maxWidth);
    
    float maxLineWidth = 0;
    for (const auto& line : lines) {
        float lineWidth = MeasureText(line.c_str(), fontSize);
        if (lineWidth > maxLineWidth) {
            maxLineWidth = lineWidth;
        }
    }
    
    float height = lines.size() * (fontSize + 2) - 2;
    
    return Rectangle{0, 0, maxLineWidth, height};
}

void NotificationManager::ClearAll() {
    notifications.clear();
}

void SendNotification(float width, float height, Color backgroundColor, const std::string& text, float seconds) {
    g_NotificationManager.SendNotification(width, height, backgroundColor, text, seconds);
}

// ===================================================================
// IMPROVED COLOR MANAGEMENT
// ===================================================================

#define MAX_TRACKS 64
static Color extendedColors[] = { 
    // Original 16 colors
    {51, 102, 255, 255}, {255, 102, 51, 255}, {51, 255, 102, 255}, {255, 51, 129, 255},
    {51, 255, 255, 255}, {228, 51, 255, 255}, {153, 255, 51, 255}, {75, 51, 255, 255},
    {255, 204, 51, 255}, {51, 180, 255, 255}, {255, 51, 51, 255}, {51, 255, 177, 255},
    {255, 51, 204, 255}, {78, 255, 51, 255}, {153, 51, 255, 255}, {231, 255, 51, 255},
    // Additional colors (lighter variants)
    {102, 153, 255, 255}, {255, 153, 102, 255}, {102, 255, 153, 255}, {255, 102, 180, 255},
    {102, 255, 255, 255}, {255, 102, 255, 255}, {204, 255, 102, 255}, {126, 102, 255, 255},
    // Additional colors (darker variants)  
    {25, 51, 128, 255}, {128, 51, 25, 255}, {25, 128, 51, 255}, {128, 25, 64, 255},
    {25, 128, 128, 255}, {114, 25, 128, 255}, {76, 128, 25, 255}, {37, 25, 128, 255},
    // More vibrant colors
    {255, 0, 127, 255}, {127, 255, 0, 255}, {0, 127, 255, 255}, {255, 127, 0, 255},
    {127, 0, 255, 255}, {0, 255, 127, 255}, {255, 255, 0, 255}, {0, 255, 255, 255},
    // Pastel variants
    {255, 192, 203, 255}, {173, 216, 230, 255}, {144, 238, 144, 255}, {255, 182, 193, 255},
    {221, 160, 221, 255}, {176, 196, 222, 255}, {255, 160, 122, 255}, {152, 251, 152, 255},
    // Final set
    {255, 105, 180, 255}, {64, 224, 208, 255}, {255, 215, 0, 255}, {138, 43, 226, 255},
    {50, 205, 50, 255}, {255, 69, 0, 255}, {30, 144, 255, 255}, {255, 20, 147, 255}
};

static Color currentTrackColors[MAX_TRACKS];
static int maxTracksUsed = 16;
static bool colorsInitialized = false;

void InitializeTrackColors(int numTracks = 16) {
    maxTracksUsed = std::min(numTracks, MAX_TRACKS);
    const int numExtendedColors = sizeof(extendedColors) / sizeof(extendedColors[0]);
    
    for (int i = 0; i < maxTracksUsed; i++) {
        currentTrackColors[i] = extendedColors[i % numExtendedColors];
    }
    colorsInitialized = true;
    std::cout << "Initialized colors for " << maxTracksUsed << " tracks" << std::endl;
}

inline Color GetTrackColorPFA(int channel) {
    if (!colorsInitialized) InitializeTrackColors();
    return currentTrackColors[channel % maxTracksUsed];
}

void RandomizeTrackColors() {
    if (!colorsInitialized) InitializeTrackColors();
    
    const int numExtendedColors = sizeof(extendedColors) / sizeof(extendedColors[0]);
    std::vector<Color> colorPool;
    for (int i = 0; i < numExtendedColors; i++) {
        colorPool.push_back(extendedColors[i]);
    }
    
    std::random_device rd;
    std::mt19937 g(rd());
    std::shuffle(colorPool.begin(), colorPool.end(), g);
    
    for (int i = 0; i < maxTracksUsed; i++) {
        currentTrackColors[i] = colorPool[i % numExtendedColors];
    }
    
    std::cout << "- Channel color change to randomized (" << maxTracksUsed << " tracks)" << std::endl;
}

void ResetTrackColors() {
    if (!colorsInitialized) InitializeTrackColors();
    
    const int numExtendedColors = sizeof(extendedColors) / sizeof(extendedColors[0]);
    for (int i = 0; i < maxTracksUsed; i++) {
        currentTrackColors[i] = extendedColors[i % numExtendedColors];
    }
    std::cout << "- Channel color change to default (" << maxTracksUsed << " tracks)" << std::endl;
}

void GenerateRandomTrackColors() {
    if (!colorsInitialized) InitializeTrackColors();
    
    std::random_device rd;
    std::mt19937 g(rd());
    std::uniform_int_distribution<int> colorDist(0, 255);
    
    for (int i = 0; i < maxTracksUsed; i++) {
        currentTrackColors[i] = {
            static_cast<unsigned char>(colorDist(g)),
            static_cast<unsigned char>(colorDist(g)),
            static_cast<unsigned char>(colorDist(g)),
            255
        };
    }
    
    std::cout << "- Channel color change to Generate random (" << maxTracksUsed << " tracks)" << std::endl;
}

// ===================================================================
// INFORMATION VERSION
// ===================================================================
void InformationVersion()
{
    int fontSize = 10;
    int positionY = GetScreenHeight() - 35;

    DrawText("Version: 1.0.0 (Release)", 10, positionY, fontSize, GRAY);
    positionY += 15;
    DrawText("Graphic: raylib 5.5", 10, positionY, fontSize, GRAY);

    DrawText("WARNING: This minor midi loads anything Control Change gone wrong.", GetScreenWidth() / 2 - MeasureText("Wwarning. This minor midi loads anything Control Change gone wrong.", 10) / 2, GetScreenHeight() - 30, 10, Color {255,255,128,128});
    DrawText("Check terminal after load midi", GetScreenWidth() / 2 - MeasureText("Check terminal after load midi", 10) / 2, GetScreenHeight() - 15, 10, Color {255,255,255,192});
}

// ===================================================================
// GUI FUNCTIONS
// ===================================================================
bool DrawButton(Rectangle bounds, const char* text, Color colors) {
    bool isHovered = CheckCollisionPointRec(GetMousePosition(), bounds);

    DrawRectangleRounded(bounds, 0.5f, 48, isHovered ? GRAY : colors);
    DrawRectangleRoundedLinesEx(bounds, 0.5f, 48, 2.0f, DARKGRAY);

    int textWidth = MeasureText(text, 20);
    DrawText(text, (int)(bounds.x + (bounds.width - textWidth) / 2), (int)(bounds.y + (bounds.height - 20) / 2), 20, WHITE);

    return isHovered && IsMouseButtonPressed(MOUSE_BUTTON_LEFT);
}

bool DrawInputBox(Rectangle box, std::string &inputBuffer, int &cursorPos, bool &inputActive, int fontSize = 20, int padding = 5) {
    DrawRectangle(0,0,GetScreenWidth(), GetScreenHeight(), Color {16,24,32,128});
    DrawText("Input patch with '.mid' file", GetScreenWidth() / 2 - MeasureText("Input patch with '.mid' file", 20)/2, GetScreenHeight() - 100, 20, WHITE);
    
    DrawRectangleRec(box, GRAY);

    static double blinkTimer = 0.0;
    blinkTimer += GetFrameTime();
    bool showCursor = fmod(blinkTimer, 1.0) < 0.5;

    int cursorPixelPos = MeasureText(inputBuffer.substr(0, cursorPos).c_str(), fontSize);

    static int scrollOffset = 0;
    if (cursorPixelPos - scrollOffset > (int)box.width - 2*padding) {
        scrollOffset = cursorPixelPos - ((int)box.width - 2*padding);
    }
    if (cursorPixelPos - scrollOffset < 0) {
        scrollOffset = cursorPixelPos;
    }

    std::string visibleText;
    int visibleStart = 0;

    for (int i = 0; i < (int)inputBuffer.size(); i++) {
        int w = MeasureText(inputBuffer.substr(0, i+1).c_str(), fontSize);
        if (w >= scrollOffset) {
            visibleStart = i;
            break;
        }
    }
    for (int i = visibleStart; i < (int)inputBuffer.size(); i++) {
        int w = MeasureText(inputBuffer.substr(visibleStart, i - visibleStart + 1).c_str(), fontSize);
        if (w > (int)box.width - 2*padding) break;
        visibleText = inputBuffer.substr(visibleStart, i - visibleStart + 1);
    }

    int textY = box.y + (box.height/2 - fontSize/2);
    DrawText(visibleText.c_str(), box.x + padding, textY, fontSize, WHITE);

    if (inputActive && showCursor) {
        int beforeW = MeasureText(inputBuffer.substr(visibleStart, cursorPos - visibleStart).c_str(), fontSize);
        int cursorX = box.x + padding + beforeW;
        DrawLine(cursorX, box.y + 5, cursorX, box.y + box.height - 5, WHITE);
    }

    if (inputActive) {
        int key = GetCharPressed();
        while (key > 0) {
            if (key >= 32 && key <= 125) {
                inputBuffer.insert(cursorPos, 1, (char)key);
                cursorPos++;
                blinkTimer = 0.0;
            }
            key = GetCharPressed();
        }

        if (IsKeyPressed(KEY_BACKSPACE) && cursorPos > 0) {
            inputBuffer.erase(cursorPos - 1, 1);
            cursorPos--;
            blinkTimer = 0.0;
        }
        if (IsKeyPressed(KEY_DELETE) && cursorPos < (int)inputBuffer.size()) {
            inputBuffer.erase(cursorPos, 1);
            blinkTimer = 0.0;
        }
        if (IsKeyPressed(KEY_LEFT) && cursorPos > 0) {
            cursorPos--;
            blinkTimer = 0.0;
        }
        if (IsKeyPressed(KEY_RIGHT) && cursorPos < (int)inputBuffer.size()) {
            cursorPos++;
            blinkTimer = 0.0;
        }
        if (IsKeyPressed(KEY_HOME)) {
            cursorPos = 0;
            blinkTimer = 0.0;
        }
        if (IsKeyPressed(KEY_END)) {
            cursorPos = inputBuffer.size();
            blinkTimer = 0.0;
        }

        if ((IsKeyDown(KEY_LEFT_CONTROL) || IsKeyDown(KEY_RIGHT_CONTROL)) && IsKeyPressed(KEY_V)) {
            const char* clip_cstr = GetClipboardText();
            if (clip_cstr != nullptr) {
                std::string clip_str(clip_cstr); 
                
                if (!clip_str.empty()) {
                    inputBuffer.insert(cursorPos, clip_str);
                    cursorPos += clip_str.length(); 
                    blinkTimer = 0.0;
                }
            }
        }

        if (IsKeyPressed(KEY_ENTER)) {
            if (!inputBuffer.empty() && inputBuffer.front() == '"' && inputBuffer.back() == '"') {
                inputBuffer = inputBuffer.substr(1, inputBuffer.size() - 2);
            }
            return true;
        }

        if (IsKeyPressed(KEY_ESCAPE)) {
            inputBuffer.clear();
            SendNotification(360, 50, SERROR, "Select input file cancelled", 5.0f);
            inputActive = false;
        }
    }

    return false;
}

void DrawModeSelectionMenu() {
    ClearBackground(JGRAY);
    DrawText("JIDI Player", 10, 10, 20, WHITE);

    static std::string inputBuffer;
    static int cursorPos = 0;
    static bool inputActive = false;
    static bool showInputBox = false;

    if (DrawButton({(float)GetScreenWidth() / 2 - 150, 200, 300, 50}, "Type Filename (Enter)", JGRAY)) {
        showInputBox = true;
        inputActive = true;
    }

    DrawText(TextFormat("File: %s", GetFileName(selectedMidiFile.c_str())),   GetScreenWidth()/2 - MeasureText(TextFormat("File: %s", GetFileName(selectedMidiFile.c_str())), 20)/2, 260, 20, LIGHTGRAY);

    if (DrawButton({(float)GetScreenWidth() / 2 - 150, 300, 300, 50}, "Start Playback", SINFORMATION)) {
        currentState = STATE_LOADING;
    }

    InformationVersion();

    if (showInputBox) {
        Rectangle inputRect = { GetScreenWidth()/2 - 320.0f, GetScreenHeight() - 60.0f, 640.0f, 40 };
        if (DrawInputBox(inputRect, inputBuffer, cursorPos, inputActive, 20)) {
            selectedMidiFile = inputBuffer;
            inputActive = false;
            showInputBox = false;
        }
        if (!inputActive) {
            showInputBox = false;
        }
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
    noteTracks.clear();
    eventList.clear();
    std::ifstream file(filename, std::ios::binary);
    if (!file.is_open()) return false;

    char header[14];
    file.read(header, 14);
    if (!file || strncmp(header, "MThd", 4) != 0) return false;
    
    uint16_t nTracks = ntohs(*reinterpret_cast<uint16_t*>(header + 10));
    ppq = ntohs(*reinterpret_cast<uint16_t*>(header + 12));
    if (ppq <= 0) ppq = 480;
    noteTracks.resize(nTracks);
    std::vector<std::map<uint8_t, std::queue<NoteEvent>>> activeNotes(nTracks);

    for (uint16_t trackIndex = 0; trackIndex < nTracks; ++trackIndex) {
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

            if (eventType == 0x90 && pos + 1 < trackData.size() && trackData[pos+1] > 0) {
                uint8_t note = trackData[pos];
                uint8_t vel = trackData[pos+1];
                NoteEvent noteEvent = { tick, 0, note, vel, channel };
                noteEvent.visualTrack = static_cast<uint8_t>(trackIndex);
                activeNotes[trackIndex][note].push(noteEvent);
                pos += 2;

            } else if (eventType == 0x80 || (eventType == 0x90 && pos + 1 < trackData.size() && trackData[pos+1] == 0)) {
                uint8_t note = trackData[pos];
                auto it = activeNotes[trackIndex].find(note);
                if (it != activeNotes[trackIndex].end() && !it->second.empty()) {
                    NoteEvent& oldestNote = it->second.front();
                    oldestNote.endTick = tick;
                    noteTracks[trackIndex].notes.push_back(oldestNote);
                    it->second.pop();
                }
                pos += 2;
            } else if (eventType == 0xB0 && pos + 1 < trackData.size()) {
                eventList.push_back({tick, EventType::CC, channel, trackData[pos], trackData[pos+1], 0});
                pos += 2;
            } else if (eventType == 0xE0 && pos + 1 < trackData.size()) {
                eventList.push_back({tick, EventType::PITCH_BEND, channel, trackData[pos], trackData[pos+1], 0});
                pos += 2;
            } else if (status == 0xFF) {
                uint8_t metaType = trackData[pos++];
                uint32_t len = readVarLen(trackData, pos);
                if (metaType == 0x51 && len == 3) {
                    uint32_t tempo = (trackData[pos] << 16) | (trackData[pos + 1] << 8) | trackData[pos + 2];
                    eventList.push_back({tick, EventType::TEMPO, 0, 0, 0, tempo});
                }
                pos += len;
            } else if (eventType == 0xC0 && pos < trackData.size()) {
                eventList.push_back({tick, EventType::PROGRAM_CHANGE, channel, trackData[pos], 0, 0});
                pos += 1;
            } else if (eventType == 0xD0 && pos < trackData.size()) {
                eventList.push_back({tick, EventType::CHANNEL_PRESSURE, channel, trackData[pos], 0, 0});
                pos += 1;
            } else { // Other events to skip
                if (eventType == 0xA0) pos += 2;
                else if (status == 0xF0 || status == 0xF7) pos += readVarLen(trackData, pos);
                else if (pos < trackData.size()) pos++;
            }
        }

        for (auto& pair : activeNotes[trackIndex]) {
            while (!pair.second.empty()) {
                NoteEvent& danglingNote = pair.second.front();
                danglingNote.endTick = tick;
                noteTracks[trackIndex].notes.push_back(danglingNote);
                pair.second.pop();
            }
        }
    }

    for (size_t trackIndex = 0; trackIndex < noteTracks.size(); ++trackIndex) {
        const auto& track = noteTracks[trackIndex];
        for (const auto& note : track.notes) {
            eventList.push_back({note.startTick, EventType::NOTE_ON, note.channel, note.note, note.velocity, 0, static_cast<uint8_t>(trackIndex)});
            eventList.push_back({note.endTick, EventType::NOTE_OFF, note.channel, note.note, 0, 0, static_cast<uint8_t>(trackIndex)});
        }
    }
    
    std::sort(eventList.begin(), eventList.end());
    for (auto& track : noteTracks) {
        std::sort(track.notes.begin(), track.notes.end(), [](const NoteEvent& a, const NoteEvent& b){ return a.startTick < b.startTick; });
    }
    
    noteTotal = 0;
    for (auto &e : eventList) {
        if (e.type == EventType::NOTE_ON && e.data2 > 0) {
            noteTotal++;
        }
    }
    
    std::cout << "Loaded " << nTracks << " tracks with track-based coloring and original MIDI channels" << std::endl;
    return true;
}

// ===================================================================
// IMPROVED VISUALIZER
// ===================================================================
void DrawStreamingVisualizerNotes(const std::vector<OptimizedTrackData>& tracks, uint64_t currentTick, int ppq, uint32_t currentTempo) {
    int screenWidth = GetScreenWidth(), screenHeight = GetScreenHeight();
    double microsecondsPerTick = MidiTiming::CalculateMicrosecondsPerTick(MidiTiming::DEFAULT_TEMPO_MICROSECONDS, ppq);
    const uint32_t viewWindow = std::max(1U, static_cast<uint32_t>((ScrollSpeed * 1250000.0) / microsecondsPerTick));
    int playbackLine = screenWidth / 2;
    renderNotes = 0;
    const float topMargin = 30.0f, bottomMargin = 30.0f;
    const float usableHeight = screenHeight - topMargin - bottomMargin;
    for (int trackIndex = 0; trackIndex < (int)tracks.size(); ++trackIndex) {
        const auto& track = tracks[trackIndex];
        if (track.notes.empty()) continue;
        
        auto startIt = std::lower_bound(track.notes.begin(), track.notes.end(), 
            (currentTick > viewWindow) ? (currentTick - viewWindow) : 0, 
            [](const NoteEvent& note, uint64_t tick) { 
                return note.endTick < tick; 
            });
        
        for (auto it = startIt; it != track.notes.end(); ++it) {
            const NoteEvent& note = *it;

            if (note.startTick > currentTick + viewWindow) break;

            float startX = playbackLine + ((float)((int64_t)note.startTick - (int64_t)currentTick) / (float)viewWindow) * (screenWidth - playbackLine);
            float endX = playbackLine + ((float)((int64_t)note.endTick - (int64_t)currentTick) / (float)viewWindow) * (screenWidth - playbackLine);
            
            float width = endX - startX;
            if (width < 1.0f) width = 1.0f;
            if (startX > screenWidth || endX < 0) continue;

            float normalizedNote = (note.note+1) / 128.0f;
            float y = screenHeight - bottomMargin - (normalizedNote * usableHeight);
            float height = std::max(1.0f, usableHeight / 128.0f);

            bool isActive = (note.startTick <= currentTick && note.endTick > currentTick);
            Color noteColor = GetTrackColorPFA(trackIndex);

            if (isActive && showNoteGlow) {
                noteColor = {255, 255, 255, 255};
            }
            DrawRectangleRec({startX, y, width, height}, noteColor);
            if (showNoteOutlines && width > 1.0f && height > 2.0f) {
                DrawRectangleLinesEx({startX, y, width, height}, 1.0f, {0, 0, 0, 128});
            }
            renderNotes++;
            if (renderNotes > maxRenderNotes) {
                maxRenderNotes = renderNotes;
            }
        }
    }

    if (showGuide) {
        int importantKeys[] = {0, 12, 24, 36, 48, 60, 72, 84, 96, 108, 120};
        for (int i = 0; i < 11; ++i) {
            int key = importantKeys[i];
            float normalizedNote = key / 128.0f;
            float y = screenHeight - bottomMargin - (normalizedNote * usableHeight);
            if (y >= topMargin && y <= screenHeight - bottomMargin) {
                Color lineColor = (key == 60) ? Color{255, 255, 128, 64} : Color{128, 128, 128, 64};
                DrawLine(0, (int)y, screenWidth, (int)y, lineColor);
                if (key == 60) {
                    DrawText("C4 (60)", 5, (int)y - 10, 10, Color{255, 255, 128, 192});
                } else if (key % 12 == 0 && key > 0) {
                    DrawText(TextFormat("C%d (%d)", (key / 12) - 1, key), 5, (int)y - 10, 10, Color{255, 255, 255, 128});
                }
            }
        }
    }

    DrawLine(0, topMargin, screenWidth, topMargin, Color{128, 128, 96, 128});
    DrawLine(0, screenHeight - bottomMargin, screenWidth, screenHeight - bottomMargin, Color{128, 128, 96, 128});
    DrawLine(playbackLine, topMargin, playbackLine, screenHeight - bottomMargin, {255, 192, 192, 128});
}

// ===================================================================
// DEBUG PANEL
// ===================================================================
void DrawDebugPanel(uint64_t currentVisualizerTick, int ppq, uint32_t currentTempo, size_t eventListPos, size_t totalEvents, bool isPaused, float scrollSpeed, const std::vector<OptimizedTrackData>& tracks, bool isFinished) {
    float panelX = (GetScreenWidth() - DWidth) - 10.0f;
    float panelY = 40.0f;
    float lineHeight = 12.0f;
    float padding = 10.0f;

    DrawRectangleRounded(Rectangle{panelX, panelY, DWidth, DHeight}, 0.25f, 0, Color{64, 64, 64, 128});
    DrawText("Debug Info", (int)(panelX + padding), (int)(panelY + padding), 20, WHITE);
    
    float currentY = panelY + padding + 25.0f;
    const char* statusText;
    Color statusColor;
    if (isFinished) {
        statusText = "FINISHED";
        statusColor = YELLOW;
    } else {
        statusText = isPaused ? "PAUSED" : "PLAYING";
        statusColor = isPaused ? RED : GREEN;
    }
    DrawText(TextFormat("Playback status: %s", statusText), (int)(panelX + padding), (int)currentY, 15, statusColor);
    currentY += lineHeight + 10.0f;
    DrawText(TextFormat("Ticks: %llu (PPQ: %d)", currentVisualizerTick, ppq), (int)(panelX + padding), (int)currentY, 10, WHITE);
    currentY += lineHeight;
    DrawText(TextFormat("Tempo: %u us", currentTempo), (int)(panelX + padding), (int)currentY, 10, WHITE);
    currentY += lineHeight;
    float progress = totalEvents > 0 ? ((float)eventListPos / (float)totalEvents) * 100.0f : 0.0f;
    DrawText(TextFormat("Event: %zu / %zu (%.3f%%)", eventListPos, totalEvents, progress), (int)(panelX + padding), (int)currentY, 10, WHITE);
    currentY += lineHeight;
    DrawText(TextFormat("Scroll speed: %.2fx", scrollSpeed), (int)(panelX + padding), (int)currentY, 10, WHITE);
    currentY += lineHeight;
    DrawText(TextFormat("Render notes: %llu / %llu", renderNotes, maxRenderNotes), (int)(panelX + padding), (int)currentY, 10, WHITE);
}

// ===================================================================
// PLAYBACK RESET FUNCTION
// ===================================================================
void ResetPlayback(const std::vector<MidiEvent>& eventList, int ppq, std::chrono::steady_clock::time_point& playbackStartTime, std::chrono::steady_clock::time_point& pauseTime, uint64_t& totalPausedTime, bool& isPaused, bool& isFinished, uint32_t& currentTempo, double& microsecondsPerTick, uint64_t& currentVisualizerTick, uint32_t& lastProcessedTick, uint64_t& accumulatedMicroseconds, size_t& eventListPos) {
    for (int ch = 0; ch < 16; ++ch) {
        SendDirectData((0xB0 | ch) | (123 << 8));
        SendDirectData((0xB0 | ch) | (121 << 8));
    }
    playbackStartTime = std::chrono::steady_clock::now();
    pauseTime = playbackStartTime;
    totalPausedTime = 0;
    noteCounter = 0;
    isPaused = false;
    isFinished = false;
    currentVisualizerTick = 0;
    lastProcessedTick = 0;
    accumulatedMicroseconds = 0;
    eventListPos = 0;
    currentTempo = MidiTiming::DEFAULT_TEMPO_MICROSECONDS;
    if (!eventList.empty() && eventList[0].type == EventType::TEMPO) {
        currentTempo = eventList[0].tempo;
    }
    microsecondsPerTick = MidiTiming::CalculateMicrosecondsPerTick(currentTempo, ppq);
    std::cout << "- Playback Restarted" << std::endl;
}

// ===================================================================
// MAIN FUNCTION
// ===================================================================
int main(int argc, char* argv[]) {
    std::cout << "+ Starting..." << std::endl;
    if (argc > 1) {
        selectedMidiFile = argv[1];
        std::cout << "+ File selection alived!" << std::endl;
    }
    SetTraceLogLevel(LOG_WARNING);
    InitWindow(1280, 720, "JIDI Player - v1.0.0 (Release)");
    SetWindowMinSize(420, 240);
    SetWindowState(FLAG_VSYNC_HINT);
    SetExitKey(KEY_NULL);

    if (!InitializeKDMAPIStream()) {
        CloseWindow();
        return -1;
    }

    std::cout << "+ KDMAPI Initialized!" << std::endl;
    
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
    bool isFinished = false;
    bool isLoop = false;
    uint64_t currentVisualizerTick = 0;
    uint32_t lastProcessedTick = 0;
    uint64_t accumulatedMicroseconds = 0;

    std::cout << "+ Opening window..." << std::endl;

    while (!WindowShouldClose()) {
        switch (currentState) {
            case STATE_MENU: {
                BeginDrawing();
                DrawModeSelectionMenu();
                g_NotificationManager.Update();
                g_NotificationManager.Draw();
                EndDrawing();
                break;
            }
            case STATE_LOADING: {
                BeginDrawing();
                DrawLoadingScreen();
                g_NotificationManager.Update();
                g_NotificationManager.Draw();
                EndDrawing();

                std::cout << "+ Midi selection: " << selectedMidiFile << std::endl;
                std::cout << "Please wait..." << std::endl;

                loadMidiFile(selectedMidiFile, noteTracks, eventList, ppq);

                InitializeTrackColors(static_cast<int>(noteTracks.size()));

                if (noteTracks.size() == 0) {
                    currentState = STATE_MENU;
                    SendNotification(400, 75, SERROR, "You need to load MIDI files first\n Or tracks is empty", 5.0f);
                    std::cout << "- Midi files need load" << std::endl;
                    break;
                }
                    
                ResetPlayback(eventList, ppq, playbackStartTime, pauseTime, totalPausedTime, isPaused, isFinished, currentTempo, microsecondsPerTick, currentVisualizerTick, lastProcessedTick, accumulatedMicroseconds, eventListPos);

                std::cout << "+-- Help controller --+" << std::endl;

                std::cout << "--- Playback ---" << std::endl;
                std::cout << "BACKSPACE = Return menu (This input anything keys after crash.)" << std::endl;
                std::cout << "SPACE = Pause / Resume" << std::endl;
                std::cout << "R = Restart playback" << std::endl;
                std::cout << "L = Loop playback when midi is finish" << std::endl;

                std::cout << "--- Render ---" << std::endl;
                std::cout << "UP (Hold), RIGHT (Pressed) = Slower scroll speed (+0.05x)" << std::endl;
                std::cout << "DOWN (Hold), LEFT (Pressed) = Faster scroll speeds (-0.05x)" << std::endl;
                std::cout << "N = Toggle outline notes (More notes = Lag)" << std::endl;
                std::cout << "G = Toggle glow notes" << std::endl;
                std::cout << "V = Toggle guide" << std::endl;

                std::cout << "--- Color ---" << std::endl;
                std::cout << "Keypad 1 = Randomize track colors" << std::endl;
                std::cout << "Keypad 2 = Generate completely random colors" << std::endl;
                std::cout << "Keypad 0 = Reset track colors to original" << std::endl; 

                std::cout << "--- Misc ---" << std::endl;
                std::cout << "F2 = Take Screenshot" << std::endl;
                std::cout << "F10 = Toggle VSync" << std::endl;
                std::cout << "F11 = Toggle Fullscreen (Do not return menu for because broken)" << std::endl;
                std::cout << "H = Toggle HUD" << std::endl;
                std::cout << "M = Reset max render notes (Debug visible only)" << std::endl;

                std::cout << "--- Debug ---" << std::endl;
                std::cout << "CTRL (Control) = Show debug" << std::endl << std::endl;
                
                std::cout << "+-- Let's being! --+" << std::endl;
                std::cout << "- Scroll speed default set: " << ScrollSpeed << "x" << std::endl;
                std::cout << "+ Midi loaded! - Total notes: " << FormatWithCommas(noteTotal).c_str() << " - Total tracks: " << noteTracks.size() << std::endl << std::endl;
                
                ClearWindowState(FLAG_VSYNC_HINT);
                SetWindowState(FLAG_WINDOW_RESIZABLE);
                currentState = STATE_PLAYING;
                SetWindowTitle(TextFormat("JIDI Player - %s", GetFileName(selectedMidiFile.c_str())));
            }
            case STATE_PLAYING: {
                if (IsKeyPressed(KEY_R)) {
                    ResetPlayback(eventList, ppq, playbackStartTime, pauseTime, totalPausedTime, isPaused, isFinished, currentTempo, microsecondsPerTick, currentVisualizerTick, lastProcessedTick, accumulatedMicroseconds, eventListPos); }
                if (IsKeyPressed(KEY_SPACE) && !isFinished) {
                    isPaused = !isPaused;
                    if (isPaused) {
                        pauseTime = std::chrono::steady_clock::now();
                        for (int ch = 0; ch < 16; ++ch) {
                        SendDirectData((0xB0 | ch) | (123 << 8));
                        }
                    } else {
                        totalPausedTime += std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now() - pauseTime).count();
                    }
                }
                if (IsKeyPressed(KEY_BACKSPACE)) { 
                    std::cout << "- Returning menu..." << std::endl; 
                    for (int ch = 0; ch < 16; ++ch) {
                        SendDirectData((0xB0 | ch) | (123 << 8));
                        SendDirectData((0xB0 | ch) | (121 << 8));
                    }
                    SetWindowState(FLAG_VSYNC_HINT);
                    ClearWindowState(FLAG_WINDOW_RESIZABLE);
                    SetWindowSize(1280, 720);
                    noteTracks.clear();
                    eventList.clear();
                    noteTracks.shrink_to_fit();
                    eventList.shrink_to_fit();
                    SetWindowTitle("JIDI Player - v1.0.0 (Release)");
                    currentState = STATE_MENU; 
                    continue; 
                }
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
                if (IsKeyPressed(KEY_L)) { 
                    isLoop = !isLoop; 
                    std::cout << "- Loops " << (isLoop ? "enabled" : "disabled") << std::endl; }
                if (IsKeyPressed(KEY_H)) { 
                    isHUD = !isHUD; 
                    std::cout << "- HUD " << (isHUD ? "visible" : "invisible") << std::endl; }
                if (IsKeyPressed(KEY_KP_1)) {
                    RandomizeTrackColors(); 
                    SendNotification(280, 50, SDEBUG, "Color change to Random", 3.0f); }
                if (IsKeyPressed(KEY_KP_0)) { 
                    ResetTrackColors(); 
                    SendNotification(270, 50, SDEBUG, "Color reset to Default", 3.0f); }
                if (IsKeyPressed(KEY_KP_2)) { 
                    GenerateRandomTrackColors(); 
                    SendNotification(380, 50, SDEBUG, "Color reset to Generate random", 3.0f); }
                if (IsKeyPressed(KEY_M)) {
                    maxRenderNotes = 0;
                    std::cout << "- Max render notes reset" << std::endl; }
                if (IsKeyPressed(KEY_F2)) {
                    time_t now = time(0);
                    struct tm tstruct;
                    char buf[64];
                    localtime_s(&tstruct, &now);
                    strftime(buf, sizeof(buf), "Jidi-Screenshot_%Y-%m-%d_%H-%M-%S.png", &tstruct);
                    TakeScreenshot(buf);
                    SendNotification(300, 50, SINFORMATION, "Screenshot saved files!", 5.0f);
                    std::cout << "+ Screenshot saved files: " << buf << std::endl; }
                if (IsKeyPressed(KEY_F10)) {
                    if (IsWindowState(FLAG_VSYNC_HINT)) {ClearWindowState(FLAG_VSYNC_HINT); std::cout << "- VSync disabled" << std::endl; }
                    else {SetWindowState(FLAG_VSYNC_HINT); std::cout << "+ VSync enabled" << std::endl; } }
                if  (IsKeyPressed(KEY_F11)) {
                    ToggleBorderlessWindowed();
                    SendNotification(320, 50, SDEBUG, "Toggle has now fullscreen!", 5.0f); }
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
                            } else if (event.type == EventType::PROGRAM_CHANGE) {
                                SendDirectData((0xC0 | event.channel) | (event.data1 << 8));
                            } else if (event.type == EventType::CHANNEL_PRESSURE) {
                                SendDirectData((0xD0 | event.channel) | (event.data1 << 8));
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

                    if (!isFinished && eventListPos >= eventList.size()) {
                        if (isLoop) {
                            ResetPlayback(eventList, ppq, playbackStartTime, pauseTime, totalPausedTime, isPaused, isFinished, currentTempo, microsecondsPerTick, currentVisualizerTick, lastProcessedTick, accumulatedMicroseconds, eventListPos);
                        } else {
                            isFinished = true;
                            std::cout << "- Playback Finished" << std::endl;
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
                
                static float smoothedProgress = 0.000f;
                float targetProgress = (noteTotal > 0) ? (float)noteCounter / (float)noteTotal : 0.000f;
                smoothedProgress += (targetProgress - smoothedProgress) * 0.25f;

                BeginDrawing();
                ClearBackground(JBLACK);
                DrawStreamingVisualizerNotes(noteTracks, currentVisualizerTick, ppq, currentTempo);
                if (isHUD) {
                DrawText(TextFormat("Notes: %s / %s", FormatWithCommas(noteCounter).c_str(), FormatWithCommas(noteTotal).c_str()), 10, 10, 20, JLIGHTBLUE);
                DrawText(TextFormat("%.3f BPM", MidiTiming::MicrosecondsToBPM(currentTempo)), 10, 35, 15, JLIGHTBLUE);
                if (isPaused) DrawText("PAUSED", GetScreenWidth()/2 - MeasureText("PAUSED", 20)/2, 20, 20, RED);
                DrawRectangle(3, GetScreenHeight() - 9, GetScreenWidth() - 6, 6, Color{32,32,32,128});
                int barWidth = (int)((GetScreenWidth() - 6) * smoothedProgress);
                DrawRectangle(3, GetScreenHeight() - 9, barWidth, 6, JLIGHTLIME);
                if (showDebug) DrawDebugPanel(currentVisualizerTick, ppq, currentTempo, eventListPos, eventList.size(), isPaused, ScrollSpeed, noteTracks, isFinished);
                DrawText(TextFormat("FPS: %llu", GetFPS()), (GetScreenWidth() - MeasureText(TextFormat("FPS: %llu", GetFPS()), 20)) - 10, 10, 20, JLIGHTLIME); }
                g_NotificationManager.Update();
                g_NotificationManager.Draw();
                EndDrawing();
                break;
            }
        }
    }
    std::cout << "- Exiting..." << std::endl;
    TerminateKDMAPIStream();
    CloseWindow();
    return 0;
}
