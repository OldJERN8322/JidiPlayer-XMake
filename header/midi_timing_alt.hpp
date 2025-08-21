#pragma once

#include <cstdint>

namespace MidiTiming {
    // Standard MIDI tempo for 120 BPM
    const uint32_t DEFAULT_TEMPO_MICROSECONDS = 500000;

    /**
     * @brief Converts microseconds per quarter note to Beats Per Minute (BPM).
     * @param microsecondsPerQuarterNote The tempo value from a MIDI file.
     * @return The tempo in BPM as a double.
     */
    inline double MicrosecondsToBPM(uint32_t microsecondsPerQuarterNote) {
        if (microsecondsPerQuarterNote == 0) return 0.0;
        return 60000000.0 / microsecondsPerQuarterNote;
    }

    /**
     * @brief Calculates the duration of a single MIDI tick in microseconds.
     * @param tempoMicroseconds The current song tempo in microseconds per quarter note.
     * @param ppq The pulses (ticks) per quarter note from the MIDI file header.
     * @return The duration of one tick in microseconds as a double.
     */
    inline double CalculateMicrosecondsPerTick(uint32_t tempoMicroseconds, int ppq) {
        if (ppq == 0) return 0.0;
        return static_cast<double>(tempoMicroseconds) / ppq;
    }
}