#include "midioutput.hpp"
#include "bass_backend.hpp"   

#include <iostream>
#include <algorithm>

// ── KDMAPI prototype ──────────────────────────────────────────────────────────
extern "C" {
    void SendDirectData(unsigned long data);
}

MidiOutputEngine::MidiOutputEngine() : 
    threadRunning(false), isPlaying(false), isPaused(false), isFinished(false), isLooping(false), antiSlowdownEnabled(false),
    eventList(nullptr), currentPpq(480), currentVisualizerTick(0), playbackSpeed(1.0f) {
}

// Build a compact index of tempo-change points so Seek() can jump in O(log M)
// rather than scanning O(N) events. Called once inside Start().
void MidiOutputEngine::BuildTempoIndex() {
    tempoIndex.clear();
    if (!eventList) return;

    uint32_t tick         = 0;
    double   accumMicros  = 0.0;
    uint32_t rawTempo     = MidiTiming::DEFAULT_TEMPO_MICROSECONDS;
    double   microsPerTick = MidiTiming::CalculateMicrosecondsPerTick(rawTempo, currentPpq);

    tempoIndex.push_back({ 0, 0, 0.0, rawTempo });

    for (size_t i = 0; i < eventList->size(); ++i) {
        const auto& ev = (*eventList)[i];
        if (ev.type != (uint8_t)EventType::TEMPO) continue;

        accumMicros  += (ev.tick - tick) * microsPerTick;
        tick          = ev.tick;
        rawTempo      = ev.data.tempo;
        microsPerTick = MidiTiming::CalculateMicrosecondsPerTick(rawTempo, currentPpq);

        tempoIndex.push_back({ i, tick, accumMicros, rawTempo });
    }
}

void MidiOutputEngine::ToggleAntiSlowdown(bool enabled) {
    antiSlowdownEnabled = enabled;
}

bool MidiOutputEngine::IsAntiSlowdownEnabled() const {
    return antiSlowdownEnabled.load();
}

// ── Lag Simulator API ─────────────────────────────────────────────────────────
void MidiOutputEngine::SetSimulateEventsPerSecond(int64_t eps) {
    simulateEventsPerSecond.store(eps > 0 ? eps : 0);
    if (eps <= 0) simLagActive.store(false);
}

int64_t MidiOutputEngine::GetSimulateEventsPerSecond() const {
    return simulateEventsPerSecond.load();
}

bool MidiOutputEngine::IsSimulateLagActive() const {
    return simLagActive.load();
}

void MidiOutputEngine::SetLagSmoothRender(bool smooth) {
    simLagSmooth.store(smooth);
}

bool MidiOutputEngine::GetLagSmoothRender() const {
    return simLagSmooth.load();
}

MidiOutputEngine::~MidiOutputEngine() {
    Stop();
}

void MidiOutputEngine::Start(const std::vector<MidiEvent>& events, int ppq, uint32_t initialTempo) {
    Stop();
    eventList = &events;
    currentPpq = ppq;
    currentTempo = initialTempo;
    microsecondsPerTick = MidiTiming::CalculateMicrosecondsPerTick(currentTempo, ppq);
    accumulatedMicroseconds = 0.0;
    pauseVirtualMicros = 0.0;
    eventPos = 0;
    lastProcessedTick = 0;
    currentVisualizerTick = 0;
    isFinished = false;
    isPaused = false;

    // Reset lag-simulator token bucket
    simTokens    = 0.0;
    simLagActive = false;
    simLastRefill = std::chrono::steady_clock::now();

    BuildTempoIndex();

    // Compute total song duration from the tempo index + remaining ticks.
    if (g_BassEngine.IsInitialized() &&
        g_BassEngine.GetActiveMode() == AudioMode::BassMIDI_PreRender &&
        !events.empty())
    {
        uint64_t totalMicros = 0;
        {
            uint32_t lastTick = 0;
            double   usPerTick = MidiTiming::CalculateMicrosecondsPerTick(initialTempo, ppq);
            for (const auto& ev : events) {
                if (ev.type == (uint8_t)EventType::TEMPO) {
                    totalMicros += (uint64_t)((ev.tick - lastTick) * usPerTick);
                    lastTick     = ev.tick;
                    usPerTick    = MidiTiming::CalculateMicrosecondsPerTick(ev.data.tempo, ppq);
                }
            }
            totalMicros += (uint64_t)((events.back().tick - lastTick) * usPerTick);
        }
        g_BassEngine.StartPreRender(events.data(), events.size(),
                                    ppq, initialTempo, totalMicros);
    }

    isPlaying = true;
    threadRunning = true;
    playbackStartTime = std::chrono::steady_clock::now();
    workerThread = std::thread(&MidiOutputEngine::PlaybackThread, this);
}

void MidiOutputEngine::Stop() {
    if (threadRunning) {
        threadRunning = false;
        if (workerThread.joinable()) {
            workerThread.join();
        }
    }
    SilenceAllChannels();
    isPlaying = false;

    // Mirror stop to BassMIDI if active
    if (g_BassEngine.IsInitialized() &&
        g_BassEngine.GetActiveMode() != AudioMode::KDMAPI)
        g_BassEngine.Stop();
}

void MidiOutputEngine::Pause() {
    if (!isPaused && isPlaying) {
        auto now = std::chrono::steady_clock::now();
        uint64_t elapsedRealMicros = std::chrono::duration_cast<std::chrono::microseconds>(now - playbackStartTime).count();
        pauseVirtualMicros = (uint64_t)(elapsedRealMicros * playbackSpeed.load());
        isPaused = true;
        SilenceAllChannelsWithoutCC();

        if (g_BassEngine.IsInitialized() &&
            g_BassEngine.GetActiveMode() != AudioMode::KDMAPI)
            g_BassEngine.Pause();
    }
}

void MidiOutputEngine::Resume() {
    if (isPaused && isPlaying) {
        auto now = std::chrono::steady_clock::now();
        playbackStartTime = now - std::chrono::microseconds((uint64_t)(pauseVirtualMicros / playbackSpeed.load()));
        isPaused = false;

        if (g_BassEngine.IsInitialized() &&
            g_BassEngine.GetActiveMode() != AudioMode::KDMAPI)
            g_BassEngine.Play();
    }
}

void MidiOutputEngine::SilenceAllChannels() {
    for (int ch = 0; ch < 16; ++ch) {
        DispatchMidiOut((0xB0 | ch) | (123 << 8)); // All Notes Off
        DispatchMidiOut((0xB0 | ch) | (121 << 8)); // Reset All Controllers
    }
    memset(activeNotes, 0, sizeof(activeNotes)); // clear tracking table
}

void MidiOutputEngine::SilenceAllChannelsWithoutCC() {
    for (int ch = 0; ch < 16; ++ch) {
        DispatchMidiOut((0xB0 | ch) | (123 << 8));
    }
    memset(activeNotes, 0, sizeof(activeNotes)); 
}

void MidiOutputEngine::SetSpeed(float newSpeed) {
    if (isPlaying && !isPaused) {
        auto now = std::chrono::steady_clock::now();
        uint64_t elapsedRealMicros = std::chrono::duration_cast<std::chrono::microseconds>(now - playbackStartTime).count();
        uint64_t elapsedVirtualMicros = (uint64_t)(elapsedRealMicros * playbackSpeed.load());
        playbackSpeed = newSpeed;
        playbackStartTime = now - std::chrono::microseconds((uint64_t)(elapsedVirtualMicros / playbackSpeed.load()));
    } else {
        playbackSpeed = newSpeed;
    }
    microsecondsPerTick = MidiTiming::CalculateMicrosecondsPerTick(currentTempo, currentPpq);
    
    // Alert the audio engine so pre-render streams can perfectly adapt to the new timing 
    if (g_BassEngine.IsInitialized()) {
        g_BassEngine.SetPlaybackSpeed(newSpeed);
    }
}

void MidiOutputEngine::SetLooping(bool loop) {
    isLooping = loop;
}

// ── Loop A/B Points ───────────────────────────────────────────────────────────
void MidiOutputEngine::SetLoopPoints(uint64_t startTick, uint64_t endTick) {
    loopStartTick.store(startTick);
    loopEndTick.store(endTick);
    hasLoopPoints.store(true);
}

void MidiOutputEngine::ClearLoopPoints() {
    hasLoopPoints.store(false);
    loopStartTick.store(0);
    loopEndTick.store(UINT64_MAX);
}

bool     MidiOutputEngine::HasLoopPoints()    const { return hasLoopPoints.load(); }
uint64_t MidiOutputEngine::GetLoopStartTick() const { return loopStartTick.load(); }
uint64_t MidiOutputEngine::GetLoopEndTick()   const { return loopEndTick.load(); }

// Convert a MIDI tick to accumulated microseconds using the tempo index.
// Used internally by LoopBackToTick().
uint64_t MidiOutputEngine::TickToMicros(uint64_t targetTick) const {
    if (tempoIndex.empty()) return 0;
    size_t lo = 0, hi = tempoIndex.size();
    while (lo + 1 < hi) {
        size_t mid = (lo + hi) / 2;
        if ((uint64_t)tempoIndex[mid].tick <= targetTick) lo = mid;
        else hi = mid;
    }
    const auto& seg = tempoIndex[lo];
    double mpt = MidiTiming::CalculateMicrosecondsPerTick(seg.rawTempo, currentPpq);
    return (uint64_t)(seg.accumMicros + (double)(targetTick - seg.tick) * mpt);
}

// Inline seek to targetTick without pausing the thread.
// Called from PlaybackThread only — do NOT call from outside the worker thread.
void MidiOutputEngine::LoopBackToTick(uint64_t loopStart) {
    SilenceAllChannels();

    // Binary-search tempoIndex for the segment that contains loopStart
    size_t segIdx = 0;
    {
        size_t lo = 0, hi = tempoIndex.size();
        while (lo + 1 < hi) {
            size_t mid = (lo + hi) / 2;
            if ((uint64_t)tempoIndex[mid].tick <= loopStart) lo = mid;
            else hi = mid;
        }
        segIdx = lo;
    }
    const TempoSegment& seg = tempoIndex[segIdx];

    uint64_t scanAccum  = (uint64_t)seg.accumMicros;
    uint32_t tempTempo  = seg.rawTempo;
    double   tempMPT    = MidiTiming::CalculateMicrosecondsPerTick(tempTempo, currentPpq);
    size_t   newEP      = seg.eventIdx;
    uint32_t newLTick   = seg.tick;

    // Scan forward through events until we reach the loop start tick
    while (newEP < eventList->size()) {
        const auto& ev = (*eventList)[newEP];
        if ((uint64_t)ev.tick >= loopStart) break;
        scanAccum = (uint64_t)(seg.accumMicros + (double)(ev.tick - seg.tick) * tempMPT);
        newLTick  = ev.tick;
        if (ev.type == (uint8_t)EventType::TEMPO) {
            tempTempo = ev.data.tempo;
            tempMPT   = MidiTiming::CalculateMicrosecondsPerTick(tempTempo, currentPpq);
        }
        newEP++;
    }

    uint64_t startMicros = scanAccum + (uint64_t)((double)(loopStart - newLTick) * tempMPT);

    // Apply new engine state
    accumulatedMicroseconds = (double)scanAccum;
    lastProcessedTick       = newLTick;
    eventPos                = newEP;
    currentTempo            = tempTempo;
    microsecondsPerTick     = tempMPT;
    currentVisualizerTick   = loopStart;

    // Re-anchor the clock so elapsedVirtualMicros == startMicros right now
    double spd = (double)playbackSpeed.load();
    uint64_t realOffset = (spd > 0.0) ? (uint64_t)((double)startMicros / spd) : 0ULL;
    playbackStartTime = std::chrono::steady_clock::now() - std::chrono::microseconds(realOffset);

    simTokens     = 0.0;
    simLagActive  = false;
    simLastRefill = std::chrono::steady_clock::now();

    if (g_BassEngine.IsInitialized() && g_BassEngine.GetActiveMode() != AudioMode::KDMAPI)
        g_BassEngine.SeekTo(startMicros);
}

uint64_t MidiOutputEngine::GetCurrentTick() const {
    return currentVisualizerTick.load();
}

size_t MidiOutputEngine::GetEventPos() const {
    return eventPos.load();
}

uint32_t MidiOutputEngine::GetCurrentTempo() const {
    return currentTempo.load();
}

bool MidiOutputEngine::IsFinished() const {
    return isFinished.load();
}

bool MidiOutputEngine::IsPaused() const {
    return isPaused.load();
}

void MidiOutputEngine::Seek(int64_t microsecondOffset) {
    bool wasPlaying = !isPaused.load();
    Pause(); 
    SilenceAllChannelsWithoutCC();
    
    int64_t targetMicros = (int64_t)pauseVirtualMicros + microsecondOffset;
    if (targetMicros < 0) targetMicros = 0;
    
    pauseVirtualMicros = (uint64_t)targetMicros; 
    
    size_t segIdx = 0;
    {
        size_t lo = 0, hi = tempoIndex.size();
        while (lo + 1 < hi) {
            size_t mid = (lo + hi) / 2;
            if (tempoIndex[mid].accumMicros <= (double)targetMicros) lo = mid;
            else hi = mid;
        }
        segIdx = lo;
    }

    const TempoSegment& seg   = tempoIndex[segIdx];
    uint64_t scanAccumulatedMicros = (uint64_t)seg.accumMicros;
    uint32_t tempTempo             = seg.rawTempo;
    double   tempMicrosPerTick     = MidiTiming::CalculateMicrosecondsPerTick(tempTempo, currentPpq);
    eventPos          = seg.eventIdx;
    lastProcessedTick = seg.tick;

    while (eventPos < eventList->size()) {
        const auto& event = (*eventList)[eventPos];
        uint64_t eventScheduledTime = scanAccumulatedMicros +
            (uint64_t)((event.tick - lastProcessedTick) * tempMicrosPerTick);
        if (eventScheduledTime > (uint64_t)targetMicros) break;
        scanAccumulatedMicros = eventScheduledTime;
        lastProcessedTick     = event.tick;
        if (event.type == (uint8_t)EventType::TEMPO) {
            tempTempo         = event.data.tempo;
            tempMicrosPerTick = MidiTiming::CalculateMicrosecondsPerTick(tempTempo, currentPpq);
        }
        eventPos++;
    }
    
    currentTempo        = tempTempo;
    microsecondsPerTick = tempMicrosPerTick;
    accumulatedMicroseconds = scanAccumulatedMicros;
    
    uint64_t microsSinceLastEvent = pauseVirtualMicros - scanAccumulatedMicros;
    if (tempMicrosPerTick > 0.0) {
        currentVisualizerTick = lastProcessedTick + (uint64_t)(microsSinceLastEvent / tempMicrosPerTick);
    }
    
    if (isFinished && eventPos < eventList->size()) {
        isFinished = false;
    }

    simTokens    = 0.0;
    simLagActive = false;
    simLastRefill = std::chrono::steady_clock::now();

    if (g_BassEngine.IsInitialized() &&
        g_BassEngine.GetActiveMode() != AudioMode::KDMAPI)
        g_BassEngine.SeekTo((uint64_t)targetMicros);
    
    if (wasPlaying) Resume();
}

void MidiOutputEngine::SeekAbsolute(uint64_t targetMicros) {
    bool wasPlaying = !isPaused.load();
    Pause();
    int64_t delta = (int64_t)targetMicros - (int64_t)pauseVirtualMicros;
    Seek(delta);
    if (wasPlaying) Resume();
}

void MidiOutputEngine::PlaybackThread() {
    while (threadRunning) {
        if (isPaused || isFinished) {
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
            continue;
        }

        auto now = std::chrono::steady_clock::now();
        uint64_t elapsedRealMicros = std::chrono::duration_cast<std::chrono::microseconds>(now - playbackStartTime).count();
        uint64_t elapsedVirtualMicros = (uint64_t)(elapsedRealMicros * playbackSpeed.load());
        
		double microsSinceLastEvent = ((double)elapsedVirtualMicros > accumulatedMicroseconds)
			? (double)elapsedVirtualMicros - accumulatedMicroseconds : 0.0;
		double effectiveMicrosPerTick = microsecondsPerTick;
		const int64_t eps = simulateEventsPerSecond.load();
		if (eps > 0 && simLagActive.load() && !simLagSmooth.load()) {
			microsSinceLastEvent = 0.0; 
		}
		
        if (effectiveMicrosPerTick > 0.0) {
            uint64_t rawVizTick = lastProcessedTick + (uint64_t)(microsSinceLastEvent / effectiveMicrosPerTick);
            // Cap at B so the visualiser never shows ticks past the loop-end point
            // and so the realtime-tick check below cannot falsely trigger early loop-back.
            if (hasLoopPoints.load() && isLooping.load())
                currentVisualizerTick = std::min(rawVizTick, loopEndTick.load());
            else
                currentVisualizerTick = rawVizTick;
        }

        // ── Token-bucket refill ───────────────────────────────────────────────
        if (eps > 0) {
            auto nowSim = std::chrono::steady_clock::now();
            double dt = std::chrono::duration<double>(nowSim - simLastRefill).count();
            simLastRefill = nowSim;
            const double burstCap = (double)eps * 0.002; // 2 ms burst window
            simTokens = std::min(simTokens + dt * (double)eps, burstCap);
        }

        int processedInBatch = 0;

        while (eventPos < eventList->size() && threadRunning && !isPaused) {
            const auto& event = (*eventList)[eventPos];

            // ── A/B loop end gate: stop processing events at or past loopEndTick ──
            if (hasLoopPoints.load() && isLooping.load()) {
                if ((uint64_t)event.tick >= loopEndTick.load()) break;
            }

            double scheduledTime = accumulatedMicroseconds + (event.tick - lastProcessedTick) * effectiveMicrosPerTick;    
            if (scheduledTime > (double)elapsedVirtualMicros) {
                double waitTimeMicros = scheduledTime - (double)elapsedVirtualMicros;
                if (waitTimeMicros > 2000.0) {
                    uint64_t sleepTime = (uint64_t)(waitTimeMicros - 1500.0);
                    if (sleepTime > 2000) sleepTime = 2000; 
                    std::this_thread::sleep_for(std::chrono::microseconds(sleepTime));
                }
                break; 
            }

            // ── Lag Simulator gate ────────────────────────────────────────────
            const bool bassActive = g_BassEngine.IsInitialized() &&
                                    g_BassEngine.GetActiveMode() != AudioMode::KDMAPI;
            if (eps > 0 && !bassActive) {
                if (simTokens < 1.0) {
                    simLagActive.store(true);
                    break;
                }
                simTokens -= 1.0;
                simLagActive.store(false);
            }
			
            accumulatedMicroseconds = scheduledTime;
            lastProcessedTick = event.tick;
            processedInBatch++;
            
            if (processedInBatch % 4096 == 0) {
                currentVisualizerTick = event.tick;
            }
            
            if (event.type == (uint8_t)EventType::TEMPO) {
                currentTempo = event.data.tempo;
                microsecondsPerTick = MidiTiming::CalculateMicrosecondsPerTick(currentTempo, currentPpq);
                effectiveMicrosPerTick = microsecondsPerTick;
            } else if (event.type == (uint8_t)EventType::NOTE_OFF) {
                // Completely pure, unaltered Note-Off stream for OmniMIDI reference counting!
				DispatchMidiOut((0x80 | event.channel) | (event.data.note.n << 8) | (event.data.note.v << 16));
				activeNotes[event.channel][event.data.note.n] = false;
			} else if (event.type == (uint8_t)EventType::NOTE_ON) {
				uint8_t ch = event.channel, n = event.data.note.n, v = event.data.note.v;
                // Completely pure, unaltered Note-On stream!
                DispatchMidiOut((0x90 | ch) | (n << 8) | (v << 16));
                activeNotes[ch][n] = (v > 0);
			} else if (event.type == (uint8_t)EventType::CC) {
                DispatchMidiOut((0xB0 | event.channel) | (event.data.cc.c << 8) | (event.data.cc.v << 16));
            } else if (event.type == (uint8_t)EventType::PITCH_BEND) {
                DispatchMidiOut((0xE0 | event.channel) | (event.data.raw.l1 << 8) | (event.data.raw.m2 << 16));
            } else if (event.type == (uint8_t)EventType::PROGRAM_CHANGE) {
                DispatchMidiOut((0xC0 | event.channel) | (event.data.val << 8));
            }
            eventPos++;
        }

        if (eps > 0 && simLagActive.load()) {
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }

        // ── A/B loop-back ─────────────────────────────────────────────────────
        // Only fires when:
        //   (a) every event before B has been dispatched (inner loop stopped at B gate), AND
        //   (b) the virtual clock has actually reached B's scheduled microsecond position.
        // Without (b), the last events near B would already be sent but the loop-back
        // would happen before their scheduled time, causing audible gaps or jumps.
        if (hasLoopPoints.load() && isLooping.load() && threadRunning && !isPaused) {
            uint64_t loopEnd = loopEndTick.load();

            // Condition (a): the next event to process is at or past B (or song ended)
            bool nextPastB = (eventPos >= eventList->size()) ||
                             ((uint64_t)(*eventList)[eventPos].tick >= loopEnd);

            if (nextPastB) {
                // Condition (b): compute virtual micros at exactly tick B
                // accumulatedMicroseconds + delta_ticks * mpt  (using current tempo)
                double loopEndMicros = accumulatedMicroseconds +
                    (double)((int64_t)loopEnd - (int64_t)lastProcessedTick) * effectiveMicrosPerTick;

                if ((double)elapsedVirtualMicros >= loopEndMicros) {
                    // Virtual clock has reached B — loop back to A now
                    LoopBackToTick(loopStartTick.load());
                    continue;
                }

                // Not time yet: sleep proportionally so we wake up right at B.
                // Convert virtual-time remainder to real-time remainder.
                double realWaitUs = (loopEndMicros - (double)elapsedVirtualMicros)
                                    / std::max(0.01, (double)playbackSpeed.load());
                // Sleep conservatively — subtract 400 µs margin for wakeup overhead
                if (realWaitUs > 800.0) {
                    uint64_t sleepUs = std::min((uint64_t)(realWaitUs - 400.0), (uint64_t)2000);
                    std::this_thread::sleep_for(std::chrono::microseconds(sleepUs));
                }
            }
        }

        if (eventPos >= eventList->size()) {
            if (isLooping.load()) {
                if (hasLoopPoints.load()) {
                    // A/B loop: seek back to A point
                    LoopBackToTick(loopStartTick.load());
                    // continue so outer loop re-reads the new clock
                } else {
                    // Full-song loop (original behaviour: restart from tick 0)
                    SilenceAllChannels();
                    accumulatedMicroseconds = 0.0;
                    lastProcessedTick = 0;
                    currentVisualizerTick = 0;
                    eventPos = 0;

                    uint32_t tempTempo = MidiTiming::DEFAULT_TEMPO_MICROSECONDS;
                    if (!eventList->empty() && (*eventList)[0].type == (uint8_t)EventType::TEMPO)
                        tempTempo = (*eventList)[0].data.tempo;
                    currentTempo = tempTempo;
                    microsecondsPerTick = MidiTiming::CalculateMicrosecondsPerTick(currentTempo, currentPpq);

                    simTokens    = 0.0;
                    simLagActive = false;
                    simLastRefill = std::chrono::steady_clock::now();
                    playbackStartTime = std::chrono::steady_clock::now();

                    if (g_BassEngine.IsInitialized() &&
                        g_BassEngine.GetActiveMode() != AudioMode::KDMAPI) {
                        g_BassEngine.SeekTo(0);
                        g_BassEngine.Play();
                    }
                }
            } else {
                isFinished = true;
            }
        }
    }
}