// AudioConfigPanel.hpp — Pre-render Audio config window (ImGui)
#pragma once
#ifdef _WIN32

#include "imgui.h"
#include "bass_backend.hpp"

#include <algorithm>
#include <cstdio>
#include <string>
#include <fstream>
#include <sstream>

static bool   s_AudioPanelOpen       = false; 
static float  s_PreRenderBufSec      = 60.0f;
static int    s_Voices               = 512;
static int    s_VelIgnore            = 2;
static bool   s_LowBuffer            = false;
static int    s_LowBufferMinVoices   = 16;
static bool   s_SfxEnabled           = true;
static float  s_Volume               = 1.0f;
static char   s_SfPathBuf[512]       = "";
static int    s_SampleRate           = 48000;
static int    s_LatencyMs            = 10;
static bool   s_NeedRestartPlayback  = false;
static float  s_LowVelScaleMaxSec   = 0.5f;  // buffer health at which vel ignore kicks in

inline void ToggleAudioConfigPanel() { s_AudioPanelOpen = !s_AudioPanelOpen; }
inline bool IsAudioConfigPanelOpen() { return s_AudioPanelOpen; }

inline int ExtractJsonInt(const std::string& line) {
    size_t colon = line.find(':');
    if (colon != std::string::npos) {
        size_t comma = line.find(',', colon);
        if (comma == std::string::npos) comma = line.length();
        return std::stoi(line.substr(colon + 1, comma - colon - 1));
    }
    return 0;
}

inline float ExtractJsonFloat(const std::string& line) {
    size_t colon = line.find(':');
    if (colon != std::string::npos) {
        size_t comma = line.find(',', colon);
        if (comma == std::string::npos) comma = line.length();
        return std::stof(line.substr(colon + 1, comma - colon - 1));
    }
    return 0.0f;
}

inline void SaveAudioConfig() {
    std::ofstream out("JIDIC.json");
    if (out.is_open()) {
        const BassConfig& cur = g_BassEngine.GetConfig();
        out << "{\n";
        out << "  \"AudioMode\": " << (int)cur.mode << ",\n";
        out << "  \"Voices\": " << cur.voices << ",\n";
        out << "  \"VelIgnore\": " << (int)cur.velocityIgnore << ",\n";
        out << "  \"SfxEnabled\": " << (cur.sfxEnabled ? 1 : 0) << ",\n";
        out << "  \"Volume\": " << g_BassEngine.GetVolume() << ",\n";
        out << "  \"PreRenderBufSec\": " << cur.preRenderBufferSec << ",\n";
        out << "  \"SampleRate\": " << cur.sampleRate << ",\n";
        out << "  \"LatencyMs\": " << cur.latencyMs << ",\n";
        out << "  \"LowVelScaleMaxSec\": " << cur.lowVelScaleMaxSec << ",\n";
        out << "  \"LowBufferMinVoices\": " << cur.lowBufferMinVoices << ",\n";
        
        const auto& fonts = g_BassEngine.GetSoundFonts();
        out << "  \"Soundfonts\": [\n";
        for (size_t i = 0; i < fonts.size(); ++i) {
            out << "    { \"path\": \"" << fonts[i].path << "\", \"enabled\": " << (fonts[i].enabled ? 1 : 0) << " }";
            if (i + 1 < fonts.size()) out << ",";
            out << "\n";
        }
        out << "  ]\n";
        out << "}\n";
    }
}

// Loads just what is required prior to Device BASS_Init
inline void PreInitAudioConfig() {
    std::ifstream in("JIDIC.json");
    if (in.is_open()) {
        BassConfig cfg = g_BassEngine.GetConfig();
        std::string line;
        while (std::getline(in, line)) {
            if (line.find("\"SampleRate\"") != std::string::npos) cfg.sampleRate = ExtractJsonInt(line);
            else if (line.find("\"LatencyMs\"") != std::string::npos) cfg.latencyMs = ExtractJsonInt(line);
            else if (line.find("\"LowVelScaleMaxSec\"") != std::string::npos) cfg.lowVelScaleMaxSec = ExtractJsonFloat(line);
            else if (line.find("\"LowBufferMinVoices\"") != std::string::npos) cfg.lowBufferMinVoices = ExtractJsonInt(line);
        }
        g_BassEngine.ApplyConfig(cfg);
        s_SampleRate = cfg.sampleRate;
        s_LatencyMs  = cfg.latencyMs;
    }
}

// Completes full runtime config loading
inline void LoadAudioConfig() {
    std::ifstream in("JIDIC.json");
    if (in.is_open()) {
        BassConfig cfg = g_BassEngine.GetConfig();
        std::string line;
        while (std::getline(in, line)) {
            if (line.find("\"AudioMode\"") != std::string::npos) cfg.mode = (AudioMode)ExtractJsonInt(line);
            else if (line.find("\"Voices\"") != std::string::npos) cfg.voices = ExtractJsonInt(line);
            else if (line.find("\"VelIgnore\"") != std::string::npos) cfg.velocityIgnore = ExtractJsonInt(line);
            else if (line.find("\"SfxEnabled\"") != std::string::npos) cfg.sfxEnabled = ExtractJsonInt(line) != 0;
            else if (line.find("\"Volume\"") != std::string::npos) g_BassEngine.SetVolume(ExtractJsonFloat(line));
            else if (line.find("\"PreRenderBufSec\"") != std::string::npos) cfg.preRenderBufferSec = ExtractJsonFloat(line);
            else if (line.find("\"LowVelScaleMaxSec\"") != std::string::npos) {
                cfg.lowVelScaleMaxSec = ExtractJsonFloat(line);
                s_LowVelScaleMaxSec = cfg.lowVelScaleMaxSec;
            }
            else if (line.find("\"path\"") != std::string::npos) {
                size_t firstQuote = line.find("\"", line.find("\"path\"") + 6);
                if (firstQuote != std::string::npos) {
                    size_t secondQuote = line.find("\"", firstQuote + 1);
                    if (secondQuote != std::string::npos) {
                        std::string path = line.substr(firstQuote + 1, secondQuote - firstQuote - 1);
                        bool enabled = (line.find("\"enabled\": 1") != std::string::npos);
                        if (g_BassEngine.AddSoundFont(path)) {
                            size_t idx = g_BassEngine.GetSoundFonts().size() - 1;
                            g_BassEngine.SetSoundFontEnabled(idx, enabled);
                        }
                    }
                }
            }
        }
        g_BassEngine.ApplyConfig(cfg);
        s_Voices = cfg.voices;
        s_LowBufferMinVoices = cfg.lowBufferMinVoices;
        s_VelIgnore = cfg.velocityIgnore;
        s_SfxEnabled = cfg.sfxEnabled;
        s_PreRenderBufSec = cfg.preRenderBufferSec;
        s_Volume = g_BassEngine.GetVolume();
    }
}

inline void DrawAudioConfigPanel()
{
    if (!s_AudioPanelOpen) return;
    if (!g_BassEngine.IsInitialized()) {
        ImGui::SetNextWindowSize(ImVec2(360, 80), ImGuiCond_Always);
        if (ImGui::Begin("Audio Config##AC", &s_AudioPanelOpen, ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoCollapse))
        {
            ImGui::TextColored(ImVec4(1,0.4f,0.4f,1), "BassEngine not initialized.");
            ImGui::TextDisabled("Call g_BassEngine.Init(hwnd) at startup.");
        }
        ImGui::End();
        return;
    }

    ImGui::SetNextWindowSize(ImVec2(480, 520), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSizeConstraints(ImVec2(360, 320), ImVec2(900, 1200));

    if (!ImGui::Begin("Pre-render Audio Config##AC", &s_AudioPanelOpen,
                      ImGuiWindowFlags_NoCollapse))
    {
        ImGui::End();
        return;
    }

    const BassConfig& cur = g_BassEngine.GetConfig();

    ImGui::PushStyleColor(ImGuiCol_Header,        ImVec4(0.12f, 0.22f, 0.32f, 1.f));
    ImGui::PushStyleColor(ImGuiCol_HeaderHovered, ImVec4(0.18f, 0.32f, 0.46f, 1.f));
    ImGui::PushStyleColor(ImGuiCol_HeaderActive,  ImVec4(0.22f, 0.40f, 0.58f, 1.f));
    bool modeOpen = ImGui::CollapsingHeader("Input Mode", ImGuiTreeNodeFlags_DefaultOpen);
    ImGui::PopStyleColor(3);

    if (modeOpen) {
        ImGui::Indent(8.f);
        ImGui::Spacing();

        static const char* kModeLabels[] = {
            "KDMAPI  (OmniMIDI / default)",
            "BassMIDI  Real-Time",
            "BassMIDI  Pre-Render (Live Streaming Buffer)",
        };
        int modeIdx = (int)cur.mode;
        ImGui::TextDisabled("Audio backend:");
        for (int i = 0; i < 3; ++i) {
            bool sel = (modeIdx == i);
            if (sel) ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.18f, 0.38f, 0.58f, 1.f));
            if (ImGui::Button(kModeLabels[i])) {
                AudioMode newMode = (AudioMode)i;
                if (newMode == AudioMode::BassMIDI_PreRender && cur.mode != AudioMode::BassMIDI_PreRender) {
                    s_NeedRestartPlayback = true;
                } else if (newMode != AudioMode::BassMIDI_PreRender) {
                    s_NeedRestartPlayback = false;
                }
                g_BassEngine.SetMode(newMode);
            }
            if (sel) ImGui::PopStyleColor();
        }
        
        if (s_NeedRestartPlayback) {
            ImGui::TextColored(ImVec4(1.f, 0.8f, 0.2f, 1.f), "Mode changed to Pre-Render. Press 'R' to reload stream!");
        }
        ImGui::Unindent(8.f);
    }

    ImGui::Spacing();

    ImGui::PushStyleColor(ImGuiCol_Header,        ImVec4(0.12f, 0.22f, 0.32f, 1.f));
    ImGui::PushStyleColor(ImGuiCol_HeaderHovered, ImVec4(0.18f, 0.32f, 0.46f, 1.f));
    ImGui::PushStyleColor(ImGuiCol_HeaderActive,  ImVec4(0.22f, 0.40f, 0.58f, 1.f));
    bool commonOpen = ImGui::CollapsingHeader("Common Settings", ImGuiTreeNodeFlags_DefaultOpen);
    ImGui::PopStyleColor(3);

    if (commonOpen) {
        ImGui::Indent(8.f);
        ImGui::Spacing();
        ImGui::SetNextItemWidth(120.f);
        if (ImGui::DragInt("Voices##vc", &s_Voices, 8.f, 1, 262144, "%d")) {
            s_Voices = std::clamp(s_Voices, 1, 262144);
            g_BassEngine.SetVoices(s_Voices);
        }
        ImGui::SameLine();
        struct VoicePreset { const char* lbl; int v; };
        static constexpr VoicePreset kVP[] = { {"64",1<<6},{"128",1<<7},{"256",1<<8},{"512",1<<9},{"1K",1024},{"4K",4096} };
        for (auto& vp : kVP) {
            bool cur2 = (s_Voices == vp.v);
            if (cur2) ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.22f,0.44f,0.22f,1));
            if (ImGui::SmallButton(vp.lbl)) { s_Voices = vp.v; g_BassEngine.SetVoices(s_Voices); }
            if (cur2) ImGui::PopStyleColor();
            ImGui::SameLine();
        }
        ImGui::NewLine();

        // Low-buffer voice floor: linearly scales from s_LowBufferMinVoices (at 0s health)
        // up to cfg.voices (at 2s health). Set to same as Voices to disable scaling.
        ImGui::SetNextItemWidth(120.f);
        if (ImGui::DragInt("Low Buf Min Voices##lbmv", &s_LowBufferMinVoices, 1.f, 1, s_Voices, "%d")) {
            s_LowBufferMinVoices = std::clamp(s_LowBufferMinVoices, 1, s_Voices);
            BassConfig cfg2 = g_BassEngine.GetConfig();
            cfg2.lowBufferMinVoices = s_LowBufferMinVoices;
            g_BassEngine.ApplyConfig(cfg2);
        }
        ImGui::SameLine(); ImGui::TextDisabled("(floor at 0s health, linear → Voices at 2s)");

        ImGui::SetNextItemWidth(120.f);
        if (ImGui::SliderInt("Vel. Ignore##vi", &s_VelIgnore, 0, 127)) {
            g_BassEngine.SetVelocityIgnore((uint8_t)s_VelIgnore);
        }

        if (ImGui::Checkbox("Sound Effects (Soundfont)", &s_SfxEnabled)) g_BassEngine.SetSfxEnabled(s_SfxEnabled);

        ImGui::SetNextItemWidth(160.f);
        if (ImGui::SliderFloat("Volume##vol", &s_Volume, 0.f, 1.f, "%.2f")) g_BassEngine.SetVolume(s_Volume);

        ImGui::Spacing();
        ImGui::Separator();
        ImGui::TextDisabled("Low-Buffer Velocity Scaling (Pre-Render mode)");
        ImGui::SetNextItemWidth(160.f);
        if (ImGui::SliderFloat("Low-vel scale window (s)##lvs", &s_LowVelScaleMaxSec, 0.0f, 2.0f, "%.2f s")) {
            BassConfig cfg2 = g_BassEngine.GetConfig();
            cfg2.lowVelScaleMaxSec = s_LowVelScaleMaxSec;
            g_BassEngine.ApplyConfig(cfg2);
        }
        ImGui::SameLine();
        if (ImGui::Button("Off##lvsoff")) {
            s_LowVelScaleMaxSec = 0.0f;
            BassConfig cfg2 = g_BassEngine.GetConfig();
            cfg2.lowVelScaleMaxSec = 0.0f;
            g_BassEngine.ApplyConfig(cfg2);
        }
        ImGui::TextDisabled("0.5s = silence when buf<0.5s; 0 = disabled; 0.01 = 10ms threshold");

        ImGui::Unindent(8.f);
    }

    ImGui::Spacing();

    bool isPR = (cur.mode == AudioMode::BassMIDI_PreRender);
    if (isPR) {
        ImGui::PushStyleColor(ImGuiCol_Header,        ImVec4(0.22f, 0.12f, 0.32f, 1.f));
        ImGui::PushStyleColor(ImGuiCol_HeaderHovered, ImVec4(0.32f, 0.18f, 0.46f, 1.f));
        ImGui::PushStyleColor(ImGuiCol_HeaderActive,  ImVec4(0.42f, 0.22f, 0.58f, 1.f));
        bool prOpen = ImGui::CollapsingHeader("Live Pre-Render Progress", ImGuiTreeNodeFlags_DefaultOpen);
        ImGui::PopStyleColor(3);

        if (prOpen) {
            ImGui::Indent(8.f);
            ImGui::Spacing();

            ImGui::SetNextItemWidth(200.f);
            if (ImGui::SliderFloat("Buffer Size (sec)##prbuf", &s_PreRenderBufSec, 1.0f, 1800.0f, "%.1f")) {
                g_BassEngine.SetPreRenderBufferSec(s_PreRenderBufSec);
            }
            ImGui::Spacing();

            auto prStatus = g_BassEngine.GetPreRenderStatus();
            if (prStatus.busy) {
                ImGui::PushStyleColor(ImGuiCol_PlotHistogram, ImVec4(0.2f, 0.6f, 1.f, 1.f));
                char ovr[32]; snprintf(ovr, sizeof(ovr), "%.0f%%", prStatus.progress * 100.f);
                ImGui::ProgressBar(prStatus.progress, ImVec2(-1.f, 0.f), ovr);
                ImGui::PopStyleColor();
                ImGui::TextDisabled("Decoding audio stream into buffer...");
                if (ImGui::Button("Cancel Background Decode")) g_BassEngine.CancelPreRender();
            } else if (prStatus.error) {
                ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.f, 0.3f, 0.3f, 1.f));
                ImGui::TextWrapped("Error: %s", prStatus.errorMsg.c_str());
                ImGui::PopStyleColor();
            } else if (prStatus.done) {
                ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.3f, 1.f, 0.4f, 1.f));
                ImGui::TextWrapped("Live Decode 100%% complete.");
                ImGui::PopStyleColor();
            } else {
                ImGui::TextDisabled("Pre-render: idle (load a MIDI file to start).");
            }
            ImGui::Unindent(8.f);
        }
    }

    ImGui::Spacing();

    ImGui::PushStyleColor(ImGuiCol_Header,        ImVec4(0.12f, 0.28f, 0.18f, 1.f));
    ImGui::PushStyleColor(ImGuiCol_HeaderHovered, ImVec4(0.18f, 0.40f, 0.26f, 1.f));
    ImGui::PushStyleColor(ImGuiCol_HeaderActive,  ImVec4(0.22f, 0.52f, 0.32f, 1.f));
    bool sfOpen = ImGui::CollapsingHeader("Soundfont Loader", ImGuiTreeNodeFlags_DefaultOpen);
    ImGui::PopStyleColor(3);

    if (sfOpen) {
        ImGui::Indent(8.f);
        ImGui::Spacing();

        ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x - 60.f);
        ImGui::InputText("##sfpath", s_SfPathBuf, sizeof(s_SfPathBuf));
        ImGui::SameLine();
        if (ImGui::Button("Add SF2")) {
            if (s_SfPathBuf[0] != '\0') {
                if (!g_BassEngine.AddSoundFont(s_SfPathBuf)) ImGui::SetTooltip("Failed to load soundfont.");
                else s_SfPathBuf[0] = '\0';
            }
        }

        const auto& fonts = g_BassEngine.GetSoundFonts();
        ImGui::Spacing();
        if (fonts.empty()) {
            ImGui::TextDisabled("(no soundfonts loaded)");
        } else {
            ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(2, 2));
            for (size_t i = 0; i < fonts.size(); ++i) {
                const auto& fe = fonts[i];

                bool en = fe.enabled;
                char chkId[32]; snprintf(chkId, sizeof(chkId), "##sfen%zu", i);
                if (ImGui::Checkbox(chkId, &en)) g_BassEngine.SetSoundFontEnabled(i, en);
                ImGui::SameLine();

                std::string fname = fe.path;
                auto slash = fname.find_last_of("\\/");
                if (slash != std::string::npos) fname = fname.substr(slash + 1);
                if (fname.size() > 36) fname = fname.substr(0, 33) + "...";

                ImGui::PushStyleColor(ImGuiCol_Text, fe.enabled ? ImVec4(1,1,1,1) : ImVec4(0.5f,0.5f,0.5f,1));
                ImGui::TextUnformatted(fname.c_str());
                ImGui::PopStyleColor();
                if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", fe.path.c_str());

                ImGui::SameLine();
                char upId[32]; snprintf(upId, sizeof(upId), "^##sfu%zu", i);
                if (i == 0) ImGui::BeginDisabled();
                if (ImGui::SmallButton(upId)) g_BassEngine.MoveSoundFontUp(i);
                if (i == 0) ImGui::EndDisabled();
                ImGui::SameLine();
                char dnId[32]; snprintf(dnId, sizeof(dnId), "v##sfd%zu", i);
                if (i + 1 >= fonts.size()) ImGui::BeginDisabled();
                if (ImGui::SmallButton(dnId)) g_BassEngine.MoveSoundFontDown(i);
                if (i + 1 >= fonts.size()) ImGui::EndDisabled();
                ImGui::SameLine();
                char rmId[32]; snprintf(rmId, sizeof(rmId), "X##sfr%zu", i);
                ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.5f,0.1f,0.1f,1));
                if (ImGui::SmallButton(rmId)) {
                    g_BassEngine.RemoveSoundFont(i);
                    ImGui::PopStyleColor();
                    break;
                }
                ImGui::PopStyleColor();
            }
            ImGui::PopStyleVar();
        }

        ImGui::Spacing();
        if (ImGui::Button("Reload All##sfrl")) g_BassEngine.ReloadAllSoundFonts();
        ImGui::Unindent(8.f);
    }

    ImGui::Spacing();

    ImGui::PushStyleColor(ImGuiCol_Header,        ImVec4(0.32f, 0.22f, 0.12f, 1.f));
    ImGui::PushStyleColor(ImGuiCol_HeaderHovered, ImVec4(0.46f, 0.32f, 0.18f, 1.f));
    ImGui::PushStyleColor(ImGuiCol_HeaderActive,  ImVec4(0.58f, 0.40f, 0.22f, 1.f));
    bool devOpen = ImGui::CollapsingHeader("Device / Config Save", ImGuiTreeNodeFlags_DefaultOpen);
    ImGui::PopStyleColor(3);

    if (devOpen) {
        ImGui::Indent(8.f);
        ImGui::Spacing();
        ImGui::TextDisabled("Changes to Sample Rate/Latency require app restart.");
        ImGui::SetNextItemWidth(120.f);
        if (ImGui::InputInt("Sample Rate##sr", &s_SampleRate, 1000, 1000)) {
            s_SampleRate = std::clamp(s_SampleRate, 11025, 192000);
        }
        ImGui::SetNextItemWidth(120.f);
        if (ImGui::InputInt("Latency (ms)##lat", &s_LatencyMs, 1, 5)) {
            s_LatencyMs = std::clamp(s_LatencyMs, 5, 200);
        }

        ImGui::Spacing();
        if (ImGui::Button("Save JIDIC.json Config")) {
            BassConfig cfg = g_BassEngine.GetConfig();
            cfg.sampleRate = s_SampleRate;
            cfg.latencyMs = s_LatencyMs;
            g_BassEngine.ApplyConfig(cfg);
            SaveAudioConfig();
        }
        ImGui::SameLine();
        if (ImGui::Button("Load JIDIC.json Config")) {
            LoadAudioConfig();
        }
        ImGui::Unindent(8.f);
    }

    ImGui::End();
}

#endif // _WIN32