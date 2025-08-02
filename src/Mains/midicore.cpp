// Streaming MIDI Player using OmniMIDI (KDMAPI) - Optimized for large MIDI files

#include <OmniMIDI.h>
#include "midi_timing.hpp"
#include <iostream>
#include <fstream>
#include <vector>
#include <thread>
#include <chrono>
#include <cstdint>
#include <queue>
#include <algorithm>

using namespace std;
using namespace chrono;

uint32_t readVarLen(vector<uint8_t>& data, size_t& i) {
    uint32_t value = 0;
    uint8_t byte = 0;
    do {
        byte = data[i++];
        value = (value << 7) | (byte & 0x7F);
    } while (byte & 0x80);
    return value;
}

struct MidiEvent {
    uint32_t tick;
    uint8_t status;
    vector<uint8_t> data;
    bool isTempo = false;
    uint32_t tempoValue = 500000; // default 120 BPM
};

struct MidiTrack {
    vector<MidiEvent> events;
};

struct MidiFile {
    uint16_t format;
    uint16_t nTracks;
    uint16_t division;
    vector<MidiTrack> tracks;
};

bool loadMidiFile(const string& path, MidiFile& midi); // <- different parameters and custom struct

bool loadMidiHeader(ifstream& file, MidiFile& midi) {
    char header[14];
    file.read(header, 14);
    if (!file || memcmp(header, "MThd", 4) != 0) return false;

    auto be16 = [](uint8_t hi, uint8_t lo) -> uint16_t {
        return (hi << 8) | lo;
    };

    midi.format = be16(header[8], header[9]);
    midi.nTracks = be16(header[10], header[11]);
    midi.division = be16(header[12], header[13]);

    return true;
}

bool loadMidiTrack(ifstream& file, MidiTrack& track) {
    char chunk[8];
    file.read(chunk, 8);
    if (memcmp(chunk, "MTrk", 4) != 0) return false;

    uint32_t length = (uint8_t(chunk[4]) << 24) | (uint8_t(chunk[5]) << 16) |
                      (uint8_t(chunk[6]) << 8) | uint8_t(chunk[7]);
    vector<uint8_t> data(length);
    file.read(reinterpret_cast<char*>(data.data()), length);

    size_t i = 0;
    uint32_t tick = 0;
    uint8_t runningStatus = 0;

    while (i < data.size()) {
        uint32_t delta = readVarLen(data, i);
        tick += delta;

        uint8_t status = data[i];
        if (status < 0x80) {
            status = runningStatus;
        } else {
            runningStatus = status;
            i++;
        }

        MidiEvent evt;
        evt.tick = tick;
        evt.status = status;

        uint8_t type = status & 0xF0;

        if (type == 0x80 || type == 0x90 || type == 0xA0 ||
            type == 0xB0 || type == 0xE0) {
            evt.data = {data[i], data[i + 1]};
            i += 2;
        } else if (type == 0xC0 || type == 0xD0) {
            evt.data = {data[i++]};
        } else if (status == 0xFF) { // Meta-event
            uint8_t metaType = data[i++];
            uint32_t len = readVarLen(data, i);
            if (metaType == 0x51 && len == 3) { // This is a Tempo Change event
                evt.isTempo = true;
                evt.tempoValue = (data[i] << 16) | (data[i + 1] << 8) | data[i + 2];
            }
            i += len;
            track.events.push_back(evt);
            continue;
        } else if (status == 0xF0 || status == 0xF7) {
            uint32_t len = readVarLen(data, i);
            i += len;
            continue;
        }

        track.events.push_back(evt);
    }

    return true;
}

bool loadMidiFile(const string& path, MidiFile& midi) {
    ifstream file(path, ios::binary);
    if (!file.is_open() || !loadMidiHeader(file, midi)) return false;

    for (int i = 0; i < midi.nTracks; ++i) {
        MidiTrack track;
        if (!loadMidiTrack(file, track)) return false;
        midi.tracks.push_back(track);
    }

    return true;
}

void playBuffered(const MidiFile& midi) {
    vector<size_t> indices(midi.tracks.size(), 0);
    uint32_t tempo = MidiTiming::DEFAULT_TEMPO_MICROSECONDS;
    uint16_t validatedPPQ = MidiTiming::ValidatePPQ(midi.division);
    double tickDuration = MidiTiming::CalculateMicrosecondsPerTick(tempo, validatedPPQ);

    // Print timing information
    MidiTiming::TimingInfo timingInfo(validatedPPQ, tempo);
    timingInfo.Print();

    using EventItem = tuple<uint32_t, size_t, MidiEvent>;
    auto cmp = [](const EventItem& a, const EventItem& b) { return get<0>(a) > get<0>(b); };
    priority_queue<EventItem, vector<EventItem>, decltype(cmp)> eventQueue(cmp);

    for (size_t t = 0; t < midi.tracks.size(); ++t) {
        if (!midi.tracks[t].events.empty()) {
            eventQueue.emplace(midi.tracks[t].events[0].tick, t, midi.tracks[t].events[0]);
        }
    }

    auto startTime = steady_clock::now();
    uint32_t lastTick = 0;
    uint64_t accumulatedTime = 0;

    while (!eventQueue.empty()) {
        auto [tick, trackIndex, evt] = eventQueue.top();
        eventQueue.pop();

        uint32_t deltaTick = tick - lastTick;
        lastTick = tick;
        accumulatedTime += (uint64_t)(deltaTick * tickDuration);

        auto now = steady_clock::now();
        auto elapsed = duration_cast<microseconds>(now - startTime).count();
        if (accumulatedTime > elapsed) {
            this_thread::sleep_for(microseconds(accumulatedTime - elapsed));
        }

        if (evt.isTempo) {
            tempo = evt.tempoValue;
            tickDuration = MidiTiming::CalculateMicrosecondsPerTick(tempo, validatedPPQ);
            double newBPM = MidiTiming::MicrosecondsToBPM(tempo);
            // Only print significant tempo changes (more than 1 BPM difference)
            static double lastBPM = 120.0;
            if (abs(newBPM - lastBPM) > 1.0) {
                cout << "Tempo change at tick " << tick << ": " << tempo
                     << " μs/quarter (" << newBPM << " BPM)" << endl;
                lastBPM = newBPM;
            }
        } else {
            // Send all MIDI events (not just Note On/Off)
            uint8_t type = evt.status & 0xF0;

            if (type == 0x80 || type == 0x90 || type == 0xA0 ||
                type == 0xB0 || type == 0xE0) {
                // 2-byte MIDI events: Note Off, Note On, Aftertouch, Control Change, Pitch Bend
                DWORD msg = evt.status | (evt.data[0] << 8) | (evt.data[1] << 16);
                SendDirectData(msg);
            } else if (type == 0xC0 || type == 0xD0) {
                // 1-byte MIDI events: Program Change, Channel Pressure
                DWORD msg = evt.status | (evt.data[0] << 8);
                SendDirectData(msg);
            }
        }

        indices[trackIndex]++;
        if (indices[trackIndex] < midi.tracks[trackIndex].events.size()) {
            const auto& nextEvt = midi.tracks[trackIndex].events[indices[trackIndex]];
            eventQueue.emplace(nextEvt.tick, trackIndex, nextEvt);
        }
    }
}

int main(int argc, char* argv[]) {
    string filename = "test.mid";
    if (argc > 1) {
        filename = argv[1];
    }

    cout << "MIDI Core Player - Enhanced with Timing Utilities" << endl;
    cout << "Loading file: " << filename << endl;

    MidiFile midi;
    if (!loadMidiFile(filename, midi)) {
        cerr << "Failed to load MIDI file: " << filename << endl;
        return 1;
    }

    cout << "MIDI file loaded successfully!" << endl;
    cout << "Format: " << midi.format << endl;
    cout << "Tracks: " << midi.nTracks << endl;
    cout << "Division (PPQ): " << midi.division << endl;

    if (!InitializeKDMAPIStream()) {
        cerr << "Failed to initialize KDMAPI!" << endl;
        return 1;
    }

    cout << "Starting playback..." << endl;
    playBuffered(midi);

    TerminateKDMAPIStream();
    cout << "Playback completed." << endl;
    return 0;
}
