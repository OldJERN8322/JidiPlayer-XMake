// MIDI Timing Test Program
// Tests the timing calculations with different PPQ values

#include "midi_timing.hpp"
#include <iostream>
#include <vector>
#include <iomanip>

using namespace std;

void testPPQValues() {
    cout << "=== Testing Common PPQ Values ===" << endl;
    
    vector<uint16_t> commonPPQs = {96u, 192u, 240u, 480u, 960u, 1920u, 3840u};
    uint32_t testTempo = 500000; // 120 BPM
    
    cout << fixed << setprecision(6);
    cout << "Tempo: " << testTempo << " μs/quarter (" 
         << MidiTiming::MicrosecondsToBPM(testTempo) << " BPM)" << endl << endl;
    
    cout << "PPQ\tμs/tick\t\tTicks/sec\tQuarter Note Duration" << endl;
    cout << "---\t-------\t\t---------\t-----------------" << endl;
    
    for (uint16_t ppq : commonPPQs) {
        double microsecondsPerTick = MidiTiming::CalculateMicrosecondsPerTick(testTempo, ppq);
        double ticksPerSecond = 1000000.0 / microsecondsPerTick;
        double quarterNoteDuration = ppq * microsecondsPerTick / 1000.0; // in milliseconds
        
        cout << ppq << "\t" << microsecondsPerTick << "\t\t" 
             << ticksPerSecond << "\t\t" << quarterNoteDuration << " ms" << endl;
    }
    cout << endl;
}

void testTempoChanges() {
    cout << "=== Testing Tempo Changes ===" << endl;
    
    vector<double> testBPMs = {60, 90, 120, 140, 180, 200};
    uint16_t ppq = 480u;
    
    cout << "PPQ: " << ppq << endl << endl;
    cout << "BPM\tμs/quarter\tμs/tick\t\tTicks/sec" << endl;
    cout << "---\t----------\t-------\t\t---------" << endl;
    
    for (double bpm : testBPMs) {
        uint32_t tempoMicroseconds = MidiTiming::BPMToMicroseconds(bpm);
        double microsecondsPerTick = MidiTiming::CalculateMicrosecondsPerTick(tempoMicroseconds, ppq);
        double ticksPerSecond = 1000000.0 / microsecondsPerTick;
        
        cout << bpm << "\t" << tempoMicroseconds << "\t\t" 
             << microsecondsPerTick << "\t\t" << ticksPerSecond << endl;
    }
    cout << endl;
}

void testPPQNormalization() {
    cout << "=== Testing PPQ Normalization ===" << endl;
    
    // Test normalizing from different PPQs to a standard 480 PPQ
    uint16_t targetPPQ = 480u;
    vector<pair<uint16_t, uint32_t>> testCases = {
        {96u, 96u},    // 1 quarter note at 96 PPQ
        {192u, 192u},  // 1 quarter note at 192 PPQ
        {240u, 240u},  // 1 quarter note at 240 PPQ
        {960u, 960u},  // 1 quarter note at 960 PPQ
        {1920u, 1920u} // 1 quarter note at 1920 PPQ
    };
    
    cout << "Original PPQ\tOriginal Ticks\tNormalized to " << targetPPQ << " PPQ" << endl;
    cout << "------------\t--------------\t-------------------" << endl;
    
    for (auto& [originalPPQ, originalTicks] : testCases) {
        uint32_t normalizedTicks = MidiTiming::NormalizeTicks(originalTicks, originalPPQ, targetPPQ);
        cout << originalPPQ << "\t\t" << originalTicks << "\t\t" << normalizedTicks << endl;
    }
    cout << endl;
}

void testSyncScenario() {
    cout << "=== Testing Sync Scenario ===" << endl;
    
    // Simulate a scenario where we have MIDI files with different PPQs
    // that need to be synchronized
    
    struct MidiFileInfo {
        string name;
        uint16_t ppq;
        uint32_t tempo;
        uint32_t noteStartTick;
    };
    
    vector<MidiFileInfo> files = {
        {"File A", 96u, 500000u, 96u},    // Note at 1 quarter note
        {"File B", 480u, 500000u, 480u},  // Note at 1 quarter note
        {"File C", 960u, 500000u, 960u},  // Note at 1 quarter note
        {"File D", 1920u, 500000u, 1920u} // Note at 1 quarter note
    };
    
    cout << "All files should have notes starting at the same time:" << endl;
    cout << "File\t\tPPQ\tTick\tTime (ms)" << endl;
    cout << "----\t\t---\t----\t---------" << endl;
    
    for (const auto& file : files) {
        double microsecondsPerTick = MidiTiming::CalculateMicrosecondsPerTick(file.tempo, file.ppq);
        uint64_t timeMicroseconds = MidiTiming::TicksToMicroseconds(file.noteStartTick, microsecondsPerTick);
        double timeMilliseconds = timeMicroseconds / 1000.0;
        
        cout << file.name << "\t\t" << file.ppq << "\t" << file.noteStartTick 
             << "\t" << timeMilliseconds << endl;
    }
    cout << endl;
}

void testInvalidValues() {
    cout << "=== Testing Invalid Value Handling ===" << endl;
    
    vector<uint16_t> invalidPPQs = {0u, 65535u}; // Note: 65536 doesn't fit in uint16_t
    
    cout << "Testing invalid PPQ values:" << endl;
    for (uint16_t ppq : invalidPPQs) {
        uint16_t corrected = MidiTiming::ValidatePPQ(ppq);
        cout << "PPQ " << ppq << " -> " << corrected << endl;
    }
    cout << endl;
    
    cout << "Testing edge case BPM values:" << endl;
    vector<double> edgeBPMs = {0, -10, 0.1, 1000};
    for (double bpm : edgeBPMs) {
        uint32_t tempo = MidiTiming::BPMToMicroseconds(bpm);
        double backToBPM = MidiTiming::MicrosecondsToBPM(tempo);
        cout << "BPM " << bpm << " -> " << tempo << " μs -> " << backToBPM << " BPM" << endl;
    }
    cout << endl;
}

int main() {
    cout << "MIDI Timing Test Suite" << endl;
    cout << "======================" << endl << endl;
    
    testPPQValues();
    testTempoChanges();
    testPPQNormalization();
    testSyncScenario();
    testInvalidValues();
    
    cout << "All tests completed!" << endl;
    return 0;
}
