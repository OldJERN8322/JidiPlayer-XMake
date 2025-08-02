#pragma once

#include <cstdint>
#include <iostream>

/**
 * MIDI Timing Utilities
 * 
 * This header provides consistent timing calculations for MIDI playback
 * across different PPQ (Pulses Per Quarter note) values.
 * 
 * MIDI Standard PPQ values commonly used:
 * - 96, 192, 240, 480, 960, 1920, 3840, etc.
 * - Maximum allowed by MIDI standard: 32767 (15-bit value)
 * - Our implementation supports up to 65535 for compatibility
 */

namespace MidiTiming {
    
    // Default values
    constexpr uint32_t DEFAULT_TEMPO_MICROSECONDS = 500000; // 120 BPM
    constexpr uint16_t DEFAULT_PPQ = 480;
    constexpr uint16_t MAX_PPQ = 65535;
    constexpr uint16_t MIN_PPQ = 1;
    
    /**
     * Validates PPQ value and returns a corrected value if invalid
     */
    inline uint16_t ValidatePPQ(uint16_t ppq) {
        if (ppq < MIN_PPQ || ppq > MAX_PPQ) {
            std::cerr << "Warning: Invalid PPQ value " << ppq 
                      << ", using default " << DEFAULT_PPQ << std::endl;
            return DEFAULT_PPQ;
        }
        return ppq;
    }
    
    /**
     * Calculate microseconds per tick for given tempo and PPQ
     * 
     * @param tempoMicroseconds Tempo in microseconds per quarter note
     * @param ppq Pulses per quarter note
     * @return Microseconds per tick
     */
    inline double CalculateMicrosecondsPerTick(uint32_t tempoMicroseconds, uint16_t ppq) {
        ppq = ValidatePPQ(ppq);
        return static_cast<double>(tempoMicroseconds) / static_cast<double>(ppq);
    }
    
    /**
     * Convert ticks to microseconds
     * 
     * @param ticks Number of ticks
     * @param microsecondsPerTick Microseconds per tick (from CalculateMicrosecondsPerTick)
     * @return Time in microseconds
     */
    inline uint64_t TicksToMicroseconds(uint32_t ticks, double microsecondsPerTick) {
        return static_cast<uint64_t>(ticks * microsecondsPerTick);
    }
    
    /**
     * Convert microseconds to ticks
     * 
     * @param microseconds Time in microseconds
     * @param microsecondsPerTick Microseconds per tick (from CalculateMicrosecondsPerTick)
     * @return Number of ticks
     */
    inline uint32_t MicrosecondsToTicks(uint64_t microseconds, double microsecondsPerTick) {
        if (microsecondsPerTick <= 0) return 0;
        return static_cast<uint32_t>(microseconds / microsecondsPerTick);
    }
    
    /**
     * Convert BPM to microseconds per quarter note
     * 
     * @param bpm Beats per minute
     * @return Microseconds per quarter note
     */
    inline uint32_t BPMToMicroseconds(double bpm) {
        if (bpm <= 0) return DEFAULT_TEMPO_MICROSECONDS;
        return static_cast<uint32_t>(60000000.0 / bpm);
    }
    
    /**
     * Convert microseconds per quarter note to BPM
     * 
     * @param microseconds Microseconds per quarter note
     * @return Beats per minute
     */
    inline double MicrosecondsToBPM(uint32_t microseconds) {
        if (microseconds == 0) return 120.0;
        return 60000000.0 / static_cast<double>(microseconds);
    }
    
    /**
     * Normalize ticks from one PPQ to another
     * This is useful when combining MIDI data from files with different PPQ values
     * 
     * @param ticks Original tick value
     * @param fromPPQ Original PPQ
     * @param toPPQ Target PPQ
     * @return Normalized tick value
     */
    inline uint32_t NormalizeTicks(uint32_t ticks, uint16_t fromPPQ, uint16_t toPPQ) {
        fromPPQ = ValidatePPQ(fromPPQ);
        toPPQ = ValidatePPQ(toPPQ);
        
        if (fromPPQ == toPPQ) return ticks;
        
        // Use 64-bit arithmetic to avoid overflow
        uint64_t normalizedTicks = (static_cast<uint64_t>(ticks) * toPPQ) / fromPPQ;
        return static_cast<uint32_t>(normalizedTicks);
    }
    
    /**
     * Calculate the time difference between two tick positions
     * 
     * @param startTick Starting tick position
     * @param endTick Ending tick position
     * @param microsecondsPerTick Microseconds per tick
     * @return Time difference in microseconds
     */
    inline uint64_t CalculateTickDuration(uint32_t startTick, uint32_t endTick, double microsecondsPerTick) {
        if (endTick <= startTick) return 0;
        return TicksToMicroseconds(endTick - startTick, microsecondsPerTick);
    }
    
    /**
     * Debug information structure for timing analysis
     */
    struct TimingInfo {
        uint16_t ppq;
        uint32_t tempoMicroseconds;
        double microsecondsPerTick;
        double bpm;
        
        TimingInfo(uint16_t p, uint32_t t) 
            : ppq(ValidatePPQ(p))
            , tempoMicroseconds(t)
            , microsecondsPerTick(CalculateMicrosecondsPerTick(t, p))
            , bpm(MicrosecondsToBPM(t))
        {}
        
        void Print() const {
            std::cout << "MIDI Timing Info:\n"
                      << "  PPQ: " << ppq << "\n"
                      << "  Tempo: " << tempoMicroseconds << " μs/quarter\n"
                      << "  BPM: " << bpm << "\n"
                      << "  μs/tick: " << microsecondsPerTick << "\n"
                      << "  Ticks/second: " << (1000000.0 / microsecondsPerTick) << std::endl;
        }
    };
}
