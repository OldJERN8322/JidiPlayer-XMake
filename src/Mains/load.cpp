// load.cpp
// MIDI file loader — 1:1 memory usage (one streaming pass, no duplicate buffers).
// Tempo stored/read as 3-byte (uint24) exactly as the MIDI spec mandates.
// Populates:
//   std::vector<MidiEvent>          → MidiOutputEngine
//   std::vector<OptimizedTrackData> → visualizer (NoteEvent note-on/off pairing)
//   std::vector<CCEvent>            → CC lane data
//   std::vector<TempoEvent>         → global tempo map
// ──────────────────────────────────────────────────────────────────────────────

#include "visualizer.hpp"
#include "midi_timing_alt.hpp"

#include <cstdio>
#include <algorithm>
#include <unordered_map>
#include <cstring>
#include <stdexcept>
#include <cassert>

// ─────────────────────────────────────────────
// Internal helpers (file-scope only)
// ─────────────────────────────────────────────

namespace {

// ── Bulk-read buffer reader ───────────────────────────────────────────────────
// Reads the ENTIRE file into RAM in one fread() call, then parses from the
// in-memory buffer.  Benefits over streaming:
//   • Single syscall for disk I/O — OS can issue one large DMA transfer.
//   • skip() is a free pointer bump (no seekg() → no kernel round-trip).
//   • All subsequent reads hit L1/L2/L3 cache instead of the page cache
//     through the ifstream/streambuf pipeline.
//   • No ifstream state-machine overhead per byte read.
// Trade-off: peak RSS increases by one file-size worth of bytes, which is
// acceptable for MIDI files (even large "black MIDI" files rarely exceed a
// few hundred MB, well within modern RAM budgets).
struct MidiReader {
    std::vector<uint8_t> buf;   // entire file loaded upfront
    size_t pos       = 0;
    size_t totalSize = 0;
    std::atomic<size_t>* progressBytes = nullptr;

    explicit MidiReader(const std::string& path, std::atomic<size_t>* pBytes = nullptr)
        : progressBytes(pBytes)
    {
        // Use C-style FILE* for the bulk read — fread() has less overhead than
        // ifstream::read() and avoids the locale/codecvt machinery.
        FILE* f = fopen(path.c_str(), "rb");
        if (!f) return;

        fseek(f, 0, SEEK_END);
        totalSize = static_cast<size_t>(ftell(f));
        fseek(f, 0, SEEK_SET);

        buf.resize(totalSize);
        if (totalSize > 0)
            fread(buf.data(), 1, totalSize, f);  // ONE disk read for the whole file

        fclose(f);
    }

    bool eof() const { return pos >= totalSize; }

    // Copy n bytes from the buffer into dst.  Returns false on overrun.
    bool readBytes(void* dst, size_t n) {
        if (pos + n > totalSize) return false;
        std::memcpy(dst, buf.data() + pos, n);
        pos += n;
        if (progressBytes && (pos % 4096 == 0))
            progressBytes->store(pos, std::memory_order_relaxed); // update every 4 KB
        return true;
    }

    // Hot-path single-byte read: direct buffer index, no memcpy overhead.
    uint8_t readU8() {
        if (pos >= totalSize) return 0;
        uint8_t v = buf[pos++];
        if (progressBytes && (pos % 4096 == 0))
            progressBytes->store(pos, std::memory_order_relaxed);
        return v;
    }

    uint16_t readU16() {
        if (pos + 2 > totalSize) return 0;
        uint16_t v = (static_cast<uint16_t>(buf[pos]) << 8) | buf[pos + 1];
        pos += 2;
        return v;
    }

    uint32_t readU32() {
        if (pos + 4 > totalSize) return 0;
        uint32_t v = (static_cast<uint32_t>(buf[pos    ]) << 24)
                   | (static_cast<uint32_t>(buf[pos + 1]) << 16)
                   | (static_cast<uint32_t>(buf[pos + 2]) <<  8)
                   |  static_cast<uint32_t>(buf[pos + 3]);
        pos += 4;
        return v;
    }

    // Read MIDI tempo as 3 bytes (uint24) — EXACTLY as the spec stores it.
    // Returns the value in a uint32_t (upper byte always 0).
    uint32_t readU24() {
        if (pos + 3 > totalSize) return 0;
        uint32_t v = (static_cast<uint32_t>(buf[pos    ]) << 16)
                   | (static_cast<uint32_t>(buf[pos + 1]) <<  8)
                   |  static_cast<uint32_t>(buf[pos + 2]);
        pos += 3;
        return v;
    }

    // Variable-length quantity (MIDI delta-time / meta event length).
    uint32_t readVLQ() {
        uint32_t val = 0;
        for (int i = 0; i < 4 && pos < totalSize; ++i) {
            uint8_t b = buf[pos++];
            val = (val << 7) | (b & 0x7F);
            if (!(b & 0x80)) break;
        }
        return val;
    }

    // Skip n bytes: free pointer arithmetic — no seekg(), no kernel call.
    void skip(uint32_t n) {
        pos += n;
        if (pos > totalSize) pos = totalSize;
    }
};

// ── Track chunk header ────────────────────────────────────────────────────────
struct TrackHeader {
    uint32_t length;   // byte length of the track data
    size_t   dataStart; // file offset of first event byte
};

// ── Note-on accumulator for building NoteEvent pairs ─────────────────────────
// Key = (channel << 7) | note  (fits in 12 bits → works as int key)
using NoteKey = uint32_t;
struct PendingNote {
    uint32_t startTick;
    uint8_t  velocity;
    uint8_t  visualTrack; // assigned below
};

inline NoteKey makeNoteKey(uint8_t ch, uint8_t note) {
    return ((uint32_t)ch << 7) | note;
}

} // namespace

// File-scope event list — filled by loadStreamingMidiData(), read via GetGlobalMidiEvents().
static std::vector<MidiEvent> s_globalEvents;

// ─────────────────────────────────────────────
// collectGlobalTempoEvents
// Single forward pass; stores only tempo meta events.
// ─────────────────────────────────────────────
std::vector<TempoEvent> collectGlobalTempoEvents(const std::string& filename) {
    std::vector<TempoEvent> tempos;
    MidiReader r(filename);

    // Header chunk
    uint32_t hdrId  = r.readU32();  // MThd
    uint32_t hdrLen = r.readU32();  // always 6
    uint16_t format = r.readU16();
    uint16_t nTracks= r.readU16();
    uint16_t ppq    = r.readU16();
    (void)format; (void)ppq;
    if (hdrLen > 6) r.skip(hdrLen - 6);

    for (uint16_t t = 0; t < nTracks && !r.eof(); ++t) {
        uint32_t chunkId  = r.readU32(); // MTrk
        uint32_t chunkLen = r.readU32();
        if (chunkId != 0x4D54726B) { r.skip(chunkLen); continue; }

        uint32_t absTick   = 0;
        uint8_t  runStatus = 0;
        size_t   bytesLeft = chunkLen;
        auto consume = [&](size_t n) { if (n <= bytesLeft) bytesLeft -= n; };

        while (bytesLeft > 0 && !r.eof()) {
            uint32_t delta = r.readVLQ(); consume(0); // VLQ: variable, handled inline
            absTick += delta;

            uint8_t statusByte = r.readU8(); consume(1);

            // Running status: if high bit clear, re-use last status
            if (statusByte & 0x80) runStatus = statusByte;
            else {
                // push back one "virtual" byte — we already consumed it as data
                // Trick: treat statusByte as first data byte under runStatus.
                // We handle this per message type below.
            }

            uint8_t status = (statusByte & 0x80) ? statusByte : runStatus;
            uint8_t firstData = (statusByte & 0x80) ? 0xFF : statusByte; // sentinel

            if (status == 0xFF) {
                // Meta event
                uint8_t  metaType = r.readU8(); consume(1);
                uint32_t metaLen  = r.readVLQ(); consume(0);
                if (metaType == 0x51 && metaLen == 3) {
                    // Tempo: 3-byte (uint24) microseconds per beat
                    uint32_t tempo = r.readU24(); consume(3);
                    tempos.push_back({ absTick, tempo });
                } else {
                    r.skip(metaLen); consume(metaLen);
                }
            } else if (status == 0xF0 || status == 0xF7) {
                // SysEx
                uint32_t sysLen = r.readVLQ(); consume(0);
                r.skip(sysLen); consume(sysLen);
                if (firstData != 0xFF) { /* first byte was status, already eaten */ }
            } else {
                // Channel event — skip data bytes
                uint8_t type = status & 0xF0;
                if (type == 0xC0 || type == 0xD0) {
                    // 1 data byte
                    if (firstData == 0xFF) { r.readU8(); consume(1); }
                    // else firstData already consumed
                } else {
                    // 2 data bytes
                    if (firstData == 0xFF) { r.readU8(); consume(1); }
                    r.readU8(); consume(1);
                }
            }
        }

        if (bytesLeft > 0) r.skip((uint32_t)bytesLeft);
    }

    std::stable_sort(tempos.begin(), tempos.end(),
        [](const TempoEvent& a, const TempoEvent& b){ return a.tick < b.tick; });
    return tempos;
}

// ─────────────────────────────────────────────
// loadStreamingMidiData  (main entry point)
//
// One linear pass over the file:
//   • Builds s_globalEvents (MidiEvent list for MidiOutputEngine)
//   • Builds tracks[N]   (OptimizedTrackData for the visualizer)
//   • Builds ccEvents    (CC lane data)
//   • Returns ppq, initialTempo, totalNoteCount
//
// Memory profile:
//   Each MidiEvent    = 12 bytes (tick4 + type1 + ch1 + data4 + pad2)
//   Each NoteEvent    = 13 bytes (tick4*2 + note1 + vel1 + ch1 + vtrack1 + pad1)
//   No duplicate copies of raw track bytes are kept.
// ─────────────────────────────────────────────
std::vector<CCEvent> loadStreamingMidiData(
    const std::string& filename, std::vector<OptimizedTrackData>& tracks,
    int& ppq, int& initialTempo, uint64_t& totalNoteCount,
    uint16_t& outTimeSigNumerator, uint16_t& outTimeSigDenominator,
    LoadProgress* progress)
{
    if (progress) progress->loadPhase = 1;
    MidiReader r(filename, progress ? &progress->bytesRead : nullptr);
    if (progress) progress->totalBytes = r.totalSize;
    tracks.clear();
    totalNoteCount = 0;
    // Default to 4/4 — overwritten if a 0x58 meta event is found
    outTimeSigNumerator   = 4;
    outTimeSigDenominator = 4;
    std::vector<CCEvent> ccEvents;

    // ── s_globalEvents is filled in-place, then sorted once at the end.
    // Access after loading via GetGlobalMidiEvents().
    s_globalEvents.clear();

    // Pre-reserve based on file size to avoid O(log N) realloc doublings.
    // Black MIDIs average ~15 raw bytes per MIDI event (delta + status + 1-2 data).
    // Heuristic: file_bytes / 15 ≈ upper-bound event count.
    // Over-reserving is fine — shrink_to_fit() reclaims slack after sort.
    if (r.totalSize > 0) {
        size_t estimatedEvents = r.totalSize / 10; // conservative: ~10 bytes/event avg
        s_globalEvents.reserve(estimatedEvents);
        ccEvents.reserve(estimatedEvents / 8);     // CC is a fraction of total events
    }

    // ── Parse header chunk ────────────────────────────────────────────────────
    if (r.readU32() != 0x4D546864) throw std::runtime_error("Not a MIDI file");
    uint32_t hdrLen = r.readU32();
    uint16_t format  = r.readU16();
    uint16_t nTracks = r.readU16();
    if (progress) progress->totalTracks = nTracks;
    uint16_t ppqRaw  = r.readU16();
    ppq = (int)(ppqRaw & 0x7FFF); // mask SMPTE flag just in case
    initialTempo = (int)MidiTiming::DEFAULT_TEMPO_MICROSECONDS;
    if (hdrLen > 6) r.skip(hdrLen - 6);

    // Reserve track slots
    int visualTrackCount = (format == 0) ? 16 : (int)nTracks;
    tracks.resize(visualTrackCount);

    // Pre-reserve NoteEvent storage per track.
    // Rough heuristic: assume notes spread evenly across tracks.
    // Over-allocation is freed by shrink_to_fit() after sort.
    if (r.totalSize > 0 && visualTrackCount > 0) {
        size_t notesPerTrack = (r.totalSize / 16) / (size_t)visualTrackCount;
        for (auto& td : tracks)
            td.notes.reserve(std::max<size_t>(notesPerTrack, 1024));
    }

    // ── Per-track pending note map (for note-on/off pairing) ─────────────────
    // Indexed by visual-track (same as MIDI track index for format 1/2; channel for format 0)
    std::vector<std::unordered_map<NoteKey, PendingNote>> pendingNotes(visualTrackCount);

    // ── Pass: one track at a time ─────────────────────────────────────────────
    for (uint16_t trackIdx = 0; trackIdx < nTracks && !r.eof(); ++trackIdx) {
        uint32_t chunkId  = r.readU32();
        uint32_t chunkLen = r.readU32();
		if (progress) progress->currentTrack = trackIdx + 1;

        if (chunkId != 0x4D54726B) {   // not "MTrk"
            r.skip(chunkLen);
            continue;
        }

        uint32_t absTick   = 0;
        uint8_t  runStatus = 0;
        size_t   bytesLeft = chunkLen;

        // Visual track for format 0 is the channel; for format 1/2 it's trackIdx.
        bool isFormat0 = (format == 0);

        while (bytesLeft > 0 && !r.eof()) {
            // ── Delta time (VLQ) ──────────────────────────────────────────────
            uint32_t delta = 0;
            for (int i = 0; i < 4; ++i) {
                if (bytesLeft == 0) break;
                uint8_t b = r.readU8(); bytesLeft--;
                delta = (delta << 7) | (b & 0x7F);
                if (!(b & 0x80)) break;
            }
            absTick += delta;

            if (bytesLeft == 0) break;
            uint8_t statusByte = r.readU8(); bytesLeft--;

            uint8_t firstData = 0xFF; // sentinel: not yet consumed
            if (statusByte & 0x80) {
                runStatus = statusByte;
            } else {
                // Running status: statusByte is actually first data byte
                firstData = statusByte;
                statusByte = runStatus;
            }

            // ── Meta events ───────────────────────────────────────────────────
            if (statusByte == 0xFF) {
                if (bytesLeft < 1) break;
                uint8_t metaType = r.readU8(); bytesLeft--;

                // VLQ length
                uint32_t metaLen = 0;
                for (int i = 0; i < 4; ++i) {
                    if (bytesLeft == 0) break;
                    uint8_t b = r.readU8(); bytesLeft--;
                    metaLen = (metaLen << 7) | (b & 0x7F);
                    if (!(b & 0x80)) break;
                }

                if (metaType == 0x51 && metaLen == 3 && bytesLeft >= 3) {
                    // Tempo — read as uint24 (3 bytes), exactly as MIDI spec
                    uint32_t tempoVal = r.readU24(); bytesLeft -= 3;

                    // First tempo encountered = initialTempo for MidiOutputEngine::Start()
                    if (absTick == 0 && s_globalEvents.empty() &&
                        initialTempo == (int)MidiTiming::DEFAULT_TEMPO_MICROSECONDS) {
                        initialTempo = (int)tempoVal;
                    }

                    MidiEvent ev(absTick, EventType::TEMPO, 0);
                    ev.data.tempo = tempoVal;   // stored in uint32_t, upper byte = 0
                    s_globalEvents.push_back(ev);
                } else if (metaType == 0x58 && metaLen == 4 && bytesLeft >= 4) {
                    // Time Signature: nn dd cc bb
                    //   nn = numerator
                    //   dd = denominator exponent (denominator = 2^dd)
                    //   cc = MIDI clocks per metronome click (ignored)
                    //   bb = 32nd notes per MIDI quarter note (ignored)
                    uint8_t nn = r.readU8(); bytesLeft--;
                    uint8_t dd = r.readU8(); bytesLeft--;
                    r.readU8(); bytesLeft--; // cc — ignored
                    r.readU8(); bytesLeft--; // bb — ignored
                    // Clamp to supported range: numerator 1–32, denominator max 32 (dd≤5)
                    if (nn < 1)  nn = 1;
                    if (nn > 32) nn = 32;
                    if (dd > 5)  dd = 5; // 2^5 = 32, maximum supported denominator
                    // Only capture the first time signature event (at tick 0 or earliest)
                    if (outTimeSigNumerator == 4 && outTimeSigDenominator == 4) {
                        outTimeSigNumerator   = nn;
                        outTimeSigDenominator = (uint16_t)(1u << dd); // 2^dd
                    }
                } else if (metaType == 0x2F) {
                    // End of track — consume remaining if any
                    if (metaLen > 0 && bytesLeft >= metaLen) {
                        r.skip(metaLen); bytesLeft -= metaLen;
                    }
                    break;
                } else {
                    if (metaLen > 0 && bytesLeft >= metaLen) {
                        r.skip(metaLen); bytesLeft -= metaLen;
                    }
                }
                continue;
            }

            // ── SysEx ─────────────────────────────────────────────────────────
            if (statusByte == 0xF0 || statusByte == 0xF7) {
                uint32_t sysLen = 0;
                for (int i = 0; i < 4; ++i) {
                    if (bytesLeft == 0) break;
                    uint8_t b = r.readU8(); bytesLeft--;
                    sysLen = (sysLen << 7) | (b & 0x7F);
                    if (!(b & 0x80)) break;
                }
                if (sysLen > 0 && bytesLeft >= sysLen) {
                    r.skip(sysLen); bytesLeft -= sysLen;
                }
                continue;
            }

            // ── Channel events ────────────────────────────────────────────────
            uint8_t  evType   = statusByte & 0xF0;
            uint8_t  channel  = statusByte & 0x0F;
            uint8_t  vtrack   = (uint8_t)(isFormat0 ? channel : (trackIdx < (uint16_t)visualTrackCount ? trackIdx : 0));

            // Read data bytes (respecting running status / already-consumed firstData)
            auto readData = [&]() -> uint8_t {
                if (firstData != 0xFF) { uint8_t v = firstData; firstData = 0xFF; return v; }
                if (bytesLeft == 0) return 0;
                uint8_t v = r.readU8(); bytesLeft--;
                return v;
            };

            // Lambda handles note-off for both 0x80 and vel=0 in 0x90.
            // Avoids goto jumping over declarations (MSVC C2362).
            auto doNoteOff = [&](uint8_t note) {
                MidiEvent ev(absTick, EventType::NOTE_OFF, channel);
                ev.data.note.n = note;
                ev.data.note.v = 0;
                s_globalEvents.push_back(ev);
                NoteKey key = makeNoteKey(channel, note);
                auto& pm    = pendingNotes[vtrack];
                auto  it    = pm.find(key);
                if (it != pm.end()) {
                    NoteEvent ne{};
                    ne.startTick   = it->second.startTick;
                    ne.endTick     = absTick;
                    ne.note        = note;
                    ne.velocity    = it->second.velocity;
                    ne.channel     = channel;
                    ne.visualTrack = vtrack;
                    tracks[vtrack].notes.push_back(ne);
                    totalNoteCount++;
					if (progress && (totalNoteCount % 500 == 0)) {
						progress->currentNotes.store(totalNoteCount, std::memory_order_relaxed);
					}
                    pm.erase(it);
                }
            };

            switch (evType) {
				case 0x80: {   // Note Off
					uint8_t note = readData();
					readData(); // consume velocity byte (ignored)
					doNoteOff(note);
					break;
				}
				case 0x90: {   // Note On
					uint8_t note = readData();
					uint8_t vel  = readData();
					if (vel == 0) {
						doNoteOff(note); // vel=0 means note-off
					} else {
						MidiEvent ev(absTick, EventType::NOTE_ON, channel);
						ev.data.note.n = note;
						ev.data.note.v = vel;
						s_globalEvents.push_back(ev);
						NoteKey key = makeNoteKey(channel, note);
						auto& pm    = pendingNotes[vtrack];
						// Retrigger: close any already-open note first (VISUALIZER ONLY)
						auto it = pm.find(key);
						if (it != pm.end()) {
							NoteEvent ne{};
							ne.startTick   = it->second.startTick;
							ne.endTick     = absTick;
							ne.note        = note;
							ne.velocity    = it->second.velocity;
							ne.channel     = channel;
							ne.visualTrack = vtrack;
							tracks[vtrack].notes.push_back(ne);
							totalNoteCount++;
							if (progress && (totalNoteCount % 500 == 0))
								progress->currentNotes.store(totalNoteCount, std::memory_order_relaxed);
							pm.erase(it);
							// ← NO synthetic NOTE_OFF into s_globalEvents — let the synth handle polyphony
						}
						pm[key] = PendingNote{ absTick, vel, vtrack };
					}
                break;
            }
            case 0xB0: {   // Control Change
                uint8_t ctrl = readData();
                uint8_t val  = readData();
                {
                    MidiEvent ev(absTick, EventType::CC, channel);
                    ev.data.cc.c = ctrl;
                    ev.data.cc.v = val;
                    s_globalEvents.push_back(ev);

                    CCEvent cc{};
                    cc.tick       = absTick;
                    cc.channel    = channel;
                    cc.controller = ctrl;
                    cc.value      = val;
                    ccEvents.push_back(cc);
                }
                break;
            }
            case 0xE0: {   // Pitch Bend
                uint8_t lsb = readData();
                uint8_t msb = readData();
                MidiEvent ev(absTick, EventType::PITCH_BEND, channel);
                ev.data.raw.l1 = lsb;
                ev.data.raw.m2 = msb;
                s_globalEvents.push_back(ev);
                break;
            }
            case 0xC0: {   // Program Change
                uint8_t prog = readData();
                MidiEvent ev(absTick, EventType::PROGRAM_CHANGE, channel);
                ev.data.val = prog;
                s_globalEvents.push_back(ev);
                break;
            }
            case 0xD0: {   // Channel Pressure (Aftertouch)
                uint8_t pressure = readData();
                MidiEvent ev(absTick, EventType::CHANNEL_PRESSURE, channel);
                ev.data.val = pressure;
                s_globalEvents.push_back(ev);
                break;
            }
            case 0xA0: {   // Polyphonic Key Pressure — not in EventType, skip
                readData(); readData();
                break;
            }
            default:
                // Unknown status — try to recover by skipping one byte
                if (firstData != 0xFF) { /* already read the first data */ }
                else { if (bytesLeft > 0) { r.readU8(); bytesLeft--; } }
                break;
            }
        }

        // Flush any notes still open at end-of-track (no matching note-off)
        for (auto& pm : pendingNotes) {
            for (auto& [key, pn] : pm) {
                uint8_t note    = key & 0x7F;
                uint8_t channel = (key >> 7) & 0x0F;
                NoteEvent ne{};
                ne.startTick  = pn.startTick;
                ne.endTick    = absTick;   // close at last known tick
                ne.note       = note;
                ne.velocity   = pn.velocity;
                ne.channel    = channel;
                ne.visualTrack= pn.visualTrack;
                if (ne.visualTrack < (uint8_t)tracks.size())
                    tracks[ne.visualTrack].notes.push_back(ne);
                totalNoteCount++;
				if (progress && (totalNoteCount % 500 == 0)) {
					progress->currentNotes.store(totalNoteCount, std::memory_order_relaxed);
				}
            }
            pm.clear();
        }

        if (bytesLeft > 0) r.skip((uint32_t)bytesLeft);
    }
	
	if (progress) {
        progress->currentNotes = totalNoteCount;
        progress->loadPhase = 2; // Move to sorting/finishing phase
    }

    // ── Sort s_globalEvents by tick.
    // std::sort (introsort, O(1) extra memory) replaces stable_sort (merge sort,
    // which allocates a full N-element temp buffer — ~2 GB spike for 213M events).
    // Same-tick ordering is fully deterministic via operator<, which breaks ties
    // by type enum value: TEMPO(3) < NOTE_OFF(1) < NOTE_ON(0)... actually the
    // enum order is NOTE_ON=0, NOTE_OFF=1, CC=2, TEMPO=3 — so TEMPO events sort
    // AFTER notes at the same tick. To preserve correct playback (tempo before notes),
    // we use a custom comparator that puts TEMPO first at the same tick.
    std::sort(s_globalEvents.begin(), s_globalEvents.end(),
        [](const MidiEvent& a, const MidiEvent& b) {
            if (a.tick != b.tick) return a.tick < b.tick;
            // At same tick: TEMPO must come first so the engine picks up the new
            // tempo before processing any notes at that tick.
            bool aTempo = (a.type == (uint8_t)EventType::TEMPO);
            bool bTempo = (b.type == (uint8_t)EventType::TEMPO);
            if (aTempo != bTempo) return aTempo > bTempo; // tempo wins
            auto pri = [](uint8_t t) -> int {
				if (t == (uint8_t)EventType::TEMPO)    return 0;
				if (t == (uint8_t)EventType::NOTE_OFF) return 1;  // ← OFF before ON
				if (t == (uint8_t)EventType::NOTE_ON)  return 2;
				return 3;
			};
			if (pri(a.type) != pri(b.type)) return pri(a.type) < pri(b.type);
			return false;
        });
    s_globalEvents.shrink_to_fit(); // reclaim excess capacity after sort

    // ── Sort each track's notes by startTick (for binary search in visualizer)
    for (auto& td : tracks) {
        std::sort(td.notes.begin(), td.notes.end(),
            [](const NoteEvent& a, const NoteEvent& b){
                return a.startTick < b.startTick;
            });
        td.notes.shrink_to_fit(); // reclaim excess per-track capacity
    }

    // ── Sort CC events
    std::sort(ccEvents.begin(), ccEvents.end(),
        [](const CCEvent& a, const CCEvent& b){
            return a.tick < b.tick;
        });
    ccEvents.shrink_to_fit();

    return ccEvents;
}

// ─────────────────────────────────────────────
// Global event list accessor
//
// loadStreamingMidiData() fills s_globalEvents (file-scope static).
// Call GetGlobalMidiEvents() after loading to pass the sorted list
// into MidiOutputEngine::Start().
//
// Add to visualizer.hpp:
//   const std::vector<MidiEvent>& GetGlobalMidiEvents();
// ─────────────────────────────────────────────
const std::vector<MidiEvent>& GetGlobalMidiEvents() {
    return s_globalEvents;
}