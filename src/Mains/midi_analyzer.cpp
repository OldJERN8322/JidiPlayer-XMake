// MIDI File Analyzer - Diagnoses MIDI timing and content issues

#include "midi_timing.hpp"
#include <iostream>
#include <fstream>
#include <vector>
#include <map>
#include <algorithm>
#include <iomanip>

using namespace std;

struct AnalyzerNote {
    uint32_t startTick;
    uint32_t endTick;
    uint8_t note;
    uint8_t velocity;
    uint8_t channel;
    size_t trackIndex;
};

struct TempoEvent {
    uint32_t tick;
    uint32_t tempoMicroseconds;
    double bpm;
};

bool analyzeMidiFile(const string& filename) {
    ifstream file(filename, ios::binary);
    if (!file) {
        cerr << "Cannot open file: " << filename << endl;
        return false;
    }

    // Read header
    char header[14];
    file.read(header, 14);
    if (memcmp(header, "MThd", 4) != 0) {
        cerr << "Not a valid MIDI file" << endl;
        return false;
    }

    uint16_t format = (header[8] << 8) | header[9];
    uint16_t nTracks = (header[10] << 8) | header[11];
    uint16_t ppq = (header[12] << 8) | header[13];

    cout << "=== MIDI File Analysis ===" << endl;
    cout << "File: " << filename << endl;
    cout << "Format: " << format << endl;
    cout << "Tracks: " << nTracks << endl;
    cout << "PPQ: " << ppq << endl;
    cout << "PPQ Status: " << (MidiTiming::ValidatePPQ(ppq) == ppq ? "Valid" : "Invalid") << endl;
    cout << endl;

    vector<AnalyzerNote> allNotes;
    vector<TempoEvent> tempoEvents;
    uint32_t currentTempo = MidiTiming::DEFAULT_TEMPO_MICROSECONDS;
    
    // Add initial tempo
    tempoEvents.push_back({0, currentTempo, MidiTiming::MicrosecondsToBPM(currentTempo)});

    for (int trackNum = 0; trackNum < nTracks; trackNum++) {
        char chunkHeader[8];
        file.read(chunkHeader, 8);
        if (memcmp(chunkHeader, "MTrk", 4) != 0) {
            cerr << "Invalid track header" << endl;
            continue;
        }

        uint32_t trackLength = (chunkHeader[4] << 24) | (chunkHeader[5] << 16) |
                               (chunkHeader[6] << 8) | chunkHeader[7];

        vector<uint8_t> trackData(trackLength);
        file.read(reinterpret_cast<char*>(trackData.data()), trackLength);

        cout << "--- Track " << trackNum << " ---" << endl;
        cout << "Length: " << trackLength << " bytes" << endl;

        uint32_t tick = 0;
        size_t i = 0;
        uint8_t runningStatus = 0;
        map<pair<uint8_t, uint8_t>, size_t> activeNotes; // (note, channel) -> index
        int noteCount = 0;
        int tempoChangeCount = 0;

        while (i < trackData.size()) {
            // Read delta time
            uint32_t delta = 0;
            while (i < trackData.size() && (trackData[i] & 0x80)) {
                delta = (delta << 7) | (trackData[i++] & 0x7F);
            }
            if (i < trackData.size()) delta = (delta << 7) | (trackData[i++] & 0x7F);
            tick += delta;

            if (i >= trackData.size()) break;

            uint8_t status = trackData[i];
            if (status < 0x80) {
                status = runningStatus;
            } else {
                runningStatus = status;
                i++;
            }

            if ((status & 0xF0) == 0x90 && i + 1 < trackData.size()) {
                // Note On
                uint8_t note = trackData[i++];
                uint8_t velocity = trackData[i++];
                uint8_t channel = status & 0x0F;

                if (velocity > 0) {
                    AnalyzerNote analyzerNote;
                    analyzerNote.startTick = tick;
                    analyzerNote.endTick = tick + ppq; // Default duration
                    analyzerNote.note = note;
                    analyzerNote.velocity = velocity;
                    analyzerNote.channel = channel;
                    analyzerNote.trackIndex = trackNum;
                    allNotes.push_back(analyzerNote);
                    
                    activeNotes[{note, channel}] = allNotes.size() - 1;
                    noteCount++;
                } else {
                    // Note On with velocity 0 = Note Off
                    auto key = make_pair(note, channel);
                    auto it = activeNotes.find(key);
                    if (it != activeNotes.end()) {
                        allNotes[it->second].endTick = tick;
                        activeNotes.erase(it);
                    }
                }
            } else if ((status & 0xF0) == 0x80 && i + 1 < trackData.size()) {
                // Note Off
                uint8_t note = trackData[i++];
                uint8_t velocity = trackData[i++];
                uint8_t channel = status & 0x0F;

                auto key = make_pair(note, channel);
                auto it = activeNotes.find(key);
                if (it != activeNotes.end()) {
                    allNotes[it->second].endTick = tick;
                    activeNotes.erase(it);
                }
            } else if ((status & 0xF0) >= 0xA0 && (status & 0xF0) <= 0xE0 && i + 1 < trackData.size()) {
                i += 2; // Skip 2-byte messages
            } else if (status == 0xFF && i + 1 < trackData.size()) {
                // Meta event
                uint8_t metaType = trackData[i++];
                uint32_t length = 0;
                while (i < trackData.size() && (trackData[i] & 0x80)) {
                    length = (length << 7) | (trackData[i++] & 0x7F);
                }
                if (i < trackData.size()) length = (length << 7) | (trackData[i++] & 0x7F);

                if (metaType == 0x51 && length == 3 && i + 3 <= trackData.size()) {
                    uint32_t newTempo = (trackData[i] << 16) | (trackData[i + 1] << 8) | trackData[i + 2];
                    if (newTempo >= 200000 && newTempo <= 1000000) {
                        currentTempo = newTempo;
                        tempoEvents.push_back({tick, newTempo, MidiTiming::MicrosecondsToBPM(newTempo)});
                        tempoChangeCount++;
                    }
                }
                i += length;
            } else if (status == 0xF0 || status == 0xF7) {
                // SysEx
                uint32_t length = 0;
                while (i < trackData.size() && (trackData[i] & 0x80)) {
                    length = (length << 7) | (trackData[i++] & 0x7F);
                }
                if (i < trackData.size()) length = (length << 7) | (trackData[i++] & 0x7F);
                i += length;
            } else {
                i++; // Skip unknown events
            }
        }

        // Close remaining active notes
        for (auto& [key, noteIndex] : activeNotes) {
            allNotes[noteIndex].endTick = tick;
        }

        cout << "Notes: " << noteCount << endl;
        cout << "Tempo changes: " << tempoChangeCount << endl;
        cout << "Last tick: " << tick << endl;
        cout << endl;
    }

    // Analysis summary
    cout << "=== Analysis Summary ===" << endl;
    cout << "Total notes: " << allNotes.size() << endl;
    cout << "Total tempo events: " << tempoEvents.size() << endl;

    if (!allNotes.empty()) {
        auto minMaxTick = minmax_element(allNotes.begin(), allNotes.end(),
            [](const AnalyzerNote& a, const AnalyzerNote& b) {
                return a.startTick < b.startTick;
            });
        
        uint32_t firstTick = minMaxTick.first->startTick;
        uint32_t lastTick = minMaxTick.second->startTick;
        
        double microsecondsPerTick = MidiTiming::CalculateMicrosecondsPerTick(currentTempo, ppq);
        double firstNoteTime = firstTick * microsecondsPerTick / 1000.0;
        double lastNoteTime = lastTick * microsecondsPerTick / 1000.0;
        double totalDuration = lastNoteTime / 1000.0;

        cout << "First note: tick " << firstTick << " (" << firstNoteTime << " ms)" << endl;
        cout << "Last note: tick " << lastTick << " (" << lastNoteTime << " ms)" << endl;
        cout << "Total duration: " << totalDuration << " seconds" << endl;
        
        if (firstTick > ppq * 4) {
            cout << "WARNING: Long silence at beginning (" << (firstNoteTime/1000.0) << " seconds)" << endl;
        }
    }

    cout << "\n=== Tempo Events ===" << endl;
    for (const auto& tempo : tempoEvents) {
        cout << "Tick " << tempo.tick << ": " << tempo.tempoMicroseconds 
             << " μs/quarter (" << fixed << setprecision(1) << tempo.bpm << " BPM)" << endl;
    }

    return true;
}

int main(int argc, char* argv[]) {
    string filename = "test2.mid";
    if (argc > 1) {
        filename = argv[1];
    }

    cout << "MIDI File Analyzer" << endl;
    cout << "==================" << endl;
    
    if (!analyzeMidiFile(filename)) {
        return 1;
    }

    return 0;
}
