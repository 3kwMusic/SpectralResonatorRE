// SPDX-FileCopyrightText: 2026 TriM
// SPDX-License-Identifier: AGPL-3.0-only

#include "SpectralEngine.h"
#include "Parameters.h"
#include <cmath>
#include <cstring>
#include <random>

namespace
{
std::complex<float> polynomialSinCos(float phase)
{
    const auto evaluate = [] (float x)
    {
        const auto squared = x * x;
        return x * (0.9999983310699463f + squared * (-0.1666494905948639f
            + squared * (0.00830482505261898f - squared * 0.00018284567340742797f)));
    };
    const auto pi = juce::MathConstants<float>::pi;
    const auto halfPi = pi * 0.5f;
    return { evaluate(halfPi - std::abs(phase)),
        evaluate(halfPi - std::abs(pi - std::abs(phase + halfPi))) };
}
}

void SpectralEngine::prepare(double newSampleRate, int, const Settings& initialSettings)
{
    sampleRate = juce::jmax(1000.0, newSampleRate);
    publishedSampleRate.store(sampleRate);
    settings = initialSettings;
    const auto pi = juce::MathConstants<double>::twoPi;
    auto windowSum = 0.0;
    for (int i = 0; i < fftSize; ++i)
    {
        const auto window = 0.5 - 0.5 * std::cos(pi * i / fftSize);
        analysisWindow[i] = static_cast<float>(window);
        windowSum += window;
    }
    const auto windowScale = static_cast<float>(hopSize / windowSum);
    for (int i = 0; i < fftSize; ++i)
    {
        analysisWindow[i] *= windowScale;
    }
    buildKernel();
    // prepare is the JUCE adapter's configuration-rebuild boundary. Native
    // config+330 also reconstructs at an unchanged rate; reset is separate.
    if (bankSeedsReady) { randomSeeded=false;bankSeedsReady=false; }
    seedRandom();
    initialiseCore();
    prepared = true;
    activationResetPending = true;
}

void SpectralEngine::buildKernel()
{
    const auto pi = juce::MathConstants<float>::pi;
    for (int index = 0; index < kernelSize; ++index)
    {
        const auto distance = std::abs(16.0f - static_cast<float>(index)) * 0.25f;
        auto value = 1.0f;
        if (distance != 0.0f)
        {
            if (distance == 1.0f)
                value = 0.5f;
            else
            {
                const auto x = distance * pi / 33.0f;
                const auto y = (distance - 1.0f) * pi / 33.0f;
                value = std::sin((distance - 1.0f) * pi) / (66.0f * std::sin(y))
                    + std::sin(distance * pi) / (33.0f * std::sin(x))
                    + std::sin((distance + 1.0f) * pi) / (66.0f * std::sin(x + pi / 33.0f));
            }
        }
        kernel[index] = value;
    }
}

void SpectralEngine::seedRandom()
{
    if (randomSeeded)
    {
        if (!bankSeedsReady)
            for (int channel=0; channel<2; ++channel)
                for (int lane=0; lane<4; ++lane) storedRandomSeeds[channel][lane]=randomState[channel][lane][1];
        bankSeedsReady = true;
        return;
    }
    std::random_device device;
    for (int channel=0; channel<2; ++channel)
    {
        for (auto& word : storedRandomSeeds[channel]) word=device();
        for (auto& lane : randomState[channel])
        {
            for (auto& word : lane)
                word = device();
            if ((lane[0] | lane[1]) == 0)
                lane[0] = 0xffffffffu;
        }
    }
    randomSeeded = true;
    bankSeedsReady = true;
}

std::uint32_t SpectralEngine::nextRandom(int channel, int lane)
{
    auto& state = randomState[channel][lane];
    const auto x = state[0] ^ (state[0] << 10);
    const auto next = state[1] ^ (state[1] >> 10) ^ x ^ (x >> 13);
    state[0] = state[1];
    state[1] = next;
    return next;
}

float SpectralEngine::randomUnit(int channel, int lane)
{
    const auto bits = (nextRandom(channel, lane) >> 9) | 0x40000000u;
    float unit = 0.0f;
    std::memcpy(&unit, &bits, sizeof(unit));
    return 0.5f * (unit - 3.0f) + 0.5f;
}

float SpectralEngine::randomPhase(int channel, int lane)
{
    return phaseFromUnit(randomUnit(channel, lane));
}

std::array<float, 4> SpectralEngine::randomUnit4(int channel)
{
    std::array<float, 4> result;
    for (int lane = 0; lane < 4; ++lane) result[lane] = randomUnit(channel, lane);
    return result;
}

void SpectralEngine::resetRandomBank(int channel, int mode, int unison)
{
    for (int lane=0; lane<4; ++lane)
        randomState[channel][lane] = { 0xffffffffu,
            mode == 0 && unison == 1 ? 5031937u : storedRandomSeeds[channel][lane] };
}

void SpectralEngine::rerandomisePhases(int channel, bool resetChorus)
{
    for (auto& slot : randomSlots[channel])
    {
        const auto random = randomUnit4(channel);
        for (int lane=0; lane<4; ++lane)
        {
            slot.phase[lane] = phaseFromUnit(random[lane]);
            if (resetChorus) slot.modulation[lane].phase = channel == 0 ? 0.25f : 0;
        }
    }
}

void SpectralEngine::initialiseRandomSlots()
{
    const auto frameRate = static_cast<float>(sampleRate / hopSize);
    const auto initialMinimum = juce::jmax(1.0f, frameRate * 0.03996004f);
    const auto initialMaximum = juce::jmax(1.0f, frameRate * 0.3996004f);
    const auto r = settings.modRate * 0.01f;
    const auto maximum = juce::jmax(1.0f, frameRate / (0.005f * (1-r) + 5*r));
    for (int channel = 0; channel < 2; ++channel)
    {
        // Temporary template draws its oscillator phase from the primary bank;
        // its Granular constructor uses the separate auxiliary stream.
        randomUnit4(channel);
        for (auto& slot : randomSlots[channel])
        {
            // Copied slot constructor: Granular countdown draw, then phase draw.
            const auto countdown = randomUnit4(channel);
            const auto phase = randomUnit4(channel);
            slot = RandomSlot {};
            for (int lane = 0; lane < 4; ++lane)
            {
                auto& state = slot.modulation[lane];
                state.countdown = countdown[lane] * (initialMaximum - initialMinimum)
                    * maximum / initialMaximum;
                state.granularStarted = true;
                slot.phase[lane] = phaseFromUnit(phase[lane]);
            }
        }
    }
    for (int channel=0; channel<2; ++channel)
    {
        // Recovered bank constructor return boundary: fixed-mode reset,
        // two per-slot phase draws, then three full phase reset passes.
        // The Granular countdown drawn above is retained throughout.
        resetRandomBank(channel, 0, 1);
        for (auto& slot : randomSlots[channel])
        {
            randomUnit4(channel);
            const auto random = randomUnit4(channel);
            for (int lane=0; lane<4; ++lane) slot.phase[lane]=phaseFromUnit(random[lane]);
        }
        for (int pass=0; pass<3; ++pass) rerandomisePhases(channel, false);
        if (settings.unisonVoices != 1)
            resetRandomBank(channel, 0, settings.unisonVoices);
        if (settings.modMode != 0)
        {
            resetRandomBank(channel, settings.modMode, settings.unisonVoices);
            rerandomisePhases(channel, settings.modMode == 1);
        }
    }
    randomGroupCursor = 0;
    allocatedRandomGroups = 0;
}

float SpectralEngine::phaseFromUnit(float unit) noexcept
{
    const auto pi = juce::MathConstants<float>::pi;
    return std::fma(unit, pi, std::fma(-unit, -pi, -pi));
}

float SpectralEngine::pitchModDepth(float semitones) noexcept
{
    const auto x = semitones * 0.083333336f;
    auto value = std::fma(0.013487903401255608f, x, 0.052174501121044159f);
    value = std::fma(x, value, 0.2412874847650528f);
    value = std::fma(x, value, 0.6930500864982605f);
    return std::fma(x, value, 1.0f) - 1.0f;
}

void SpectralEngine::resetVoice(Voice& voice)
{
    voice.active=false; voice.noteOn=false; voice.releasing=false; voice.held=false; voice.forceStopped=false; voice.note=-1;
    voice.activePartials=0; voice.activeHarmonics=0; voice.envelope=0;
    voice.rampRemaining=0; voice.age=0; voice.glidePosition=0; voice.glideDuration=0;
    voice.velocity=1; voice.frequency=1; voice.targetFrequency=1; voice.analysisFrequency=1;
    voice.glideNote=0;voice.glideOutputNote=0;voice.glideTargetNote=0;voice.glideStep=0;voice.glideRemaining=0;

}

void SpectralEngine::initialiseCore()
{
    quietSamples = std::numeric_limits<std::uint64_t>::max();
    activityGuardSamples = 0;
    hostBlockActive = false;
    spectrogram.invalidate();
    coefficientMetadataDirty=true;
    refreshDampingGainCache();
    for (auto& voice : voices) { voice = Voice {}; }
    initialiseRandomSlots();
    for (int channel = 0; channel < 2; ++channel)
    {
        inputHistory[channel].fill(0.0f);
        dryHistory[channel].fill(0.0f);
        synthesisHistory[channel].fill(0.0f);
        magnitudes[channel].fill(0.0f);
        outputSpectrum[channel].fill({ 0.0f, 0.0f });
    }
    visualizationSequence.fetch_add(1, std::memory_order_acq_rel);
    for(auto& b:visualizationData) b.store(0.0f);
    visualizationSequence.fetch_add(1, std::memory_order_seq_cst);
    sampleCounter = 0;
    frameCounter = 0;
    ageCounter = 0;
    pendingNotes.fill({});
    heldNotes.fill({});
    outputGainState.fill(0.0f);
    pitchBend = 0.0f;
    sustainPedal = false;
    displayFundamental.store(internalFrequency());
    updatePartialLayout();
}

void SpectralEngine::clearSignalBuffers()
{
    quietSamples = std::numeric_limits<std::uint64_t>::max();
    spectrogram.invalidate();
    for(int channel=0;channel<2;++channel)
    {
        inputHistory[channel].fill(0);dryHistory[channel].fill(0);synthesisHistory[channel].fill(0);
        magnitudes[channel].fill(0);outputSpectrum[channel].fill({});
    }
    outputGainState.fill(0);
    visualizationSequence.fetch_add(1,std::memory_order_acq_rel);
    for(auto& value:visualizationData)value.store(0);
    visualizationSequence.fetch_add(1,std::memory_order_seq_cst);
}

void SpectralEngine::reset()
{
    clearSignalBuffers();
    if (!prepared) return;
    coefficientMetadataDirty=true;
    for(int channel=0;channel<2;++channel)
    {
        // bcb650 consumes the current stream; it never reseeds or reconstructs
        // Wander/Granular/Chorus. The absolute clock/hop remainder is retained.
        rerandomisePhases(channel,false);
        for(auto& slot:randomSlots[channel]){slot.amplitude.fill(0);slot.target.fill(0);}
    }
    if(!sustainPedal && !(settings.monoPolyMode==0 && !settings.midiGate)) releaseVoices(true);
    tickVoices();
    updatePartialLayout();
}

float SpectralEngine::internalFrequency() const
{
    return settings.frequencyDialMode == 0 && !settings.useScale
        ? settings.frequencyHz : 440.0f * std::pow(2.0f, (mapNote(settings.frequencyNote) - 69.0f) / 12.0f);
}

void SpectralEngine::rescaleModulation(float oldRate, float newRate)
{
    if (oldRate == newRate) return;
    const auto oldWanderMaximum = juce::jmax(1.0f,
        static_cast<float>(sampleRate / hopSize) / (0.05f*(1-oldRate*0.01f)+50.0f*oldRate*0.01f));
    const auto newWanderMaximum = juce::jmax(1.0f,
        static_cast<float>(sampleRate / hopSize) / (0.05f*(1-newRate*0.01f)+50.0f*newRate*0.01f));
    const auto oldGranularMaximum = juce::jmax(1.0f,
        static_cast<float>(sampleRate / hopSize) / (0.005f*(1-oldRate*0.01f)+5.0f*oldRate*0.01f));
    const auto newGranularMaximum = juce::jmax(1.0f,
        static_cast<float>(sampleRate / hopSize) / (0.005f*(1-newRate*0.01f)+5.0f*newRate*0.01f));
    const auto wanderRatio = oldWanderMaximum / newWanderMaximum;
    const auto granularRatio = newGranularMaximum / oldGranularMaximum;
    for (auto& bank : randomSlots)
        for (auto& slot : bank)
            for (auto& state : slot.modulation)
            {
                state.increment *= wanderRatio;
                state.countdown *= granularRatio;
            }
}

void SpectralEngine::setSettings(const Settings& newSettings, unsigned integerCallbacks)
{
    const auto dampingChanged = settings.highFreqDamp!=newSettings.highFreqDamp
        || settings.lowFreqDamp!=newSettings.lowFreqDamp;
    const auto decayChanged = settings.decayMs!=newSettings.decayMs
        || dampingChanged;
    const auto internalPitchDirty = newSettings.pitchMode == 0
        && ((integerCallbacks & frequencyNoteCallback) != 0
            || settings.frequencyHz != newSettings.frequencyHz
            || settings.frequencyNote != newSettings.frequencyNote
            || settings.frequencyDialMode != newSettings.frequencyDialMode);
    // RealtimeModel<int> compares its unrounded float stream before invoking
    // a setter. Transpose/scale-degree setters dirty metadata even when their
    // rounded values stay equal. The dirty bit is consumed once at hop sync.
    coefficientMetadataDirty |= (integerCallbacks & (transposeCallback | scaleDegreeCallback)) != 0 || internalPitchDirty
        || settings.pitchMode!=newSettings.pitchMode || settings.monoPolyMode!=newSettings.monoPolyMode
        || settings.polyphony!=newSettings.polyphony || settings.unisonVoices!=newSettings.unisonVoices
        || settings.unisonAmount!=newSettings.unisonAmount || settings.stretch!=newSettings.stretch
        || settings.useScale!=newSettings.useScale
        || settings.scale!=newSettings.scale || settings.scaleRoot!=newSettings.scaleRoot
        || settings.transpose!=newSettings.transpose || settings.transposeScaleDegrees!=newSettings.transposeScaleDegrees
        || settings.quantize!=newSettings.quantize;
    const auto managerChanged = settings.monoPolyMode != newSettings.monoPolyMode
        || settings.polyphony != newSettings.polyphony || settings.midiGate != newSettings.midiGate;
    const auto modulationChanged = settings.modMode != newSettings.modMode;
    const auto unisonChanged = settings.unisonVoices != newSettings.unisonVoices;
    const auto oldModulationMode = settings.modMode;
    rescaleModulation(settings.modRate, newSettings.modRate);
    settings = newSettings;
    if (dampingChanged || !dampingGainCacheValid) refreshDampingGainCache();
    if (decayChanged) { refreshDecayCoefficients();retimeReleasedVoices(); }
    if (managerChanged) releaseVoices(true);
    if (settings.pitchMode == 0) displayFundamental.store(internalFrequency());
    // Mode; Mode setter then sees the new Unison. Separate blocks remain
    // separate callback batches, rather than being reordered globally.
    if (unisonChanged)
        for (int channel=0; channel<2; ++channel)
        {
            resetRandomBank(channel, oldModulationMode, settings.unisonVoices);
            if (settings.unisonVoices == 1) rerandomisePhases(channel, false);
        }
    if (modulationChanged)
        for (int channel=0; channel<2; ++channel)
        {
            resetRandomBank(channel, settings.modMode, settings.unisonVoices);
            rerandomisePhases(channel, settings.modMode == 1);
        }
}

void SpectralEngine::noteOn(int note, float velocity, bool replay)
{
    if (!replay) heldNotes[static_cast<size_t>(juce::jlimit(0, 127, note))] = {true, note, velocity, ++ageCounter};
    const auto reuse = settings.monoPolyMode == 0 && voices[0].active && !voices[0].forceStopped;
    const auto previousNote = voices[0].glideOutputNote;
    Voice* selected = nullptr;
    if (settings.monoPolyMode == 0)
    {
        for (auto& voice : voices) voice.active=false;
        pendingNotes.fill({});
    }
    else
    {
        for (auto& voice : voices)
            if (voice.active && voice.note == note) { selected = &voice; break; }

        if (selected == nullptr)
        {
            const auto activeVoices = static_cast<int>(std::count_if(voices.begin(), voices.end(),
                [] (const Voice& voice) { return voice.active; }));
            if (activeVoices >= juce::jlimit(1, maxVoices, settings.polyphony))
            {
                for (auto& pending : pendingNotes)
                    if (pending.active && pending.note == note)
                    {
                        pending.velocity = velocity;
                        pending.age = ++ageCounter;
                        return;
                    }
                for (auto& pending : pendingNotes)
                    if (!pending.active)
                    {
                        if (!forcePendingVictims()) return;
                        pending = { true, note, velocity, ++ageCounter };
                        return;
                    }
                return;
            }
        }
    }

    if (selected == nullptr)
        for (auto& voice : voices)
            if (!voice.active) { selected = &voice; break; }
    if (selected == nullptr) return;

    const auto previousAge=selected->active?selected->age:0;
    resetVoice(*selected);
    selected->active = true;
    selected->noteOn = true;
    selected->held = true;
    selected->forceStopped = false;
    selected->note = note;
    selected->velocity = velocity;
    selected->age = previousAge ? previousAge : ++ageCounter;
    if (reuse) { selected->glideNote=previousNote;selected->glideOutputNote=previousNote; }
    setVoicePitch(*selected,note,reuse);
    beginVoice(*selected);
    if (settings.pitchMode == 1) displayFundamental.store(selected->frequency);
}

void SpectralEngine::startPendingNotes()
{
    while (true)
    {
        const auto activeVoices = static_cast<int>(std::count_if(voices.begin(), voices.end(),
            [] (const Voice& voice) { return voice.active; }));
        if (activeVoices >= juce::jlimit(1, maxVoices, settings.polyphony))
        {
            if (std::any_of(pendingNotes.begin(),pendingNotes.end(),[](const auto& n){return n.active;})) forcePendingVictims();
            return;
        }

        PendingNote* oldest = nullptr;
        for (auto& pending : pendingNotes)
            if (pending.active && (oldest == nullptr || pending.age < oldest->age)) oldest = &pending;
        if (oldest == nullptr) return;
        const auto note = oldest->note;
        const auto velocity = oldest->velocity;
        oldest->active = false;
        noteOn(note, velocity, true);
    }
}

bool SpectralEngine::forcePendingVictims()
{
    const auto count=static_cast<int>(std::count_if(voices.begin(),voices.end(),[](const auto& v){return v.active;}));
    const auto victims=juce::jmax(1,count-juce::jlimit(1,maxVoices,settings.polyphony)+1);
    for(int n=0;n<victims;++n)
    {
        Voice* victim=nullptr;
        for(auto& voice:voices)
            if(voice.active && !voice.forceStopped && !voice.held && (!victim || voice.age<victim->age)) victim=&voice;
        if(!victim)for(auto& voice:voices)
            if(voice.active && !voice.forceStopped && voice.held && (!victim || voice.age<victim->age)) victim=&voice;
        if(!victim)return false;
        releaseVoice(*victim,true);
    }
    return true;
}

int SpectralEngine::voiceMode() const
{
    // The Transmute voice constructor selects generic envelope mode0.
    // Public PitchMode/MonoPoly/Gate are separate bank/manager fields.
    return 0;
}

bool SpectralEngine::excitationEnabled(const Voice& voice) const
{
    const auto gate = settings.monoPolyMode == 1 || settings.midiGate;
    return settings.pitchMode == 0 || !gate || (voice.held && !voice.forceStopped);
}

void SpectralEngine::setVoiceStage(Voice& voice, VoiceStage stage)
{
    voice.stage = stage;
    if (stage == VoiceStage::inactive)
    {
        voice.active = false;
        voice.envelope = 0.0f;
        return;
    }

    auto start = voice.envelope;
    auto target = 1.0f;
    auto seconds = 0.0f;
    auto curve = 0.0f;
    auto curveScale = 7.5f;
    switch (stage)
    {
        case VoiceStage::attack:
            start = 0.0f;
            target = 0.0f;
            curveScale = 0.0f;
            break;
        case VoiceStage::decay:
            target = 1.0f;
            seconds = 0.0f;
            break;
        case VoiceStage::sustain:
            start = 1.0f;
            target = 1.0f;
            break;
        case VoiceStage::hold:
            start = 1.0f;
            target = 1.0f;
            break;
        case VoiceStage::release:
            target = 0.0f;
            seconds = voice.forceStopped ? 0.0f : juce::jmax(0.0f, settings.decayMs * 0.001f);
            break;
        case VoiceStage::releaseFromSustain:
            start = 1.0f;
            target = 0.0f;
            seconds = voice.forceStopped ? 0.0f : juce::jmax(0.0f, settings.decayMs * 0.001f);
            break;
        case VoiceStage::inactive:
            break;
    }
    const auto samples = juce::jmax<std::uint64_t>(1,
        static_cast<std::uint64_t>(seconds * (sampleRate / hopSize)));
    voice.rampStart = start;
    voice.rampDelta = target - start;
    voice.rampRemaining = samples;
    voice.rampIncrement = 1.0f / static_cast<float>(samples);
    voice.rampPosition = 0.0f;
    voice.curve = -curve * curveScale;
    voice.curveNormalization = std::abs(curve * curveScale) > 0.001f
        ? 1.0f / (std::exp2(voice.curve) - 1.0f) : 1.0f;
}

void SpectralEngine::beginVoice(Voice& voice)
{
    // bd46c0's final overrides make attack/decay zero and sustain1.
    // Glide controls the independent pitch mapper, never these stage times.
    setVoiceStage(voice, VoiceStage::hold);
    voice.envelope = 1.0f;
}

void SpectralEngine::advanceVoice(Voice& voice)
{
    if (!voice.active || voice.stage == VoiceStage::inactive) return;
    const auto previousPosition = voice.rampPosition;
    if (voice.rampRemaining > 0)
    {
        --voice.rampRemaining;
        voice.rampPosition = std::fma(-voice.rampIncrement,static_cast<float>(voice.rampRemaining),1.0f);
    }
    const auto curved = voice.curve != 0.0f
        ? (std::exp2(voice.curve * previousPosition) - 1.0f) * voice.curveNormalization
        : previousPosition;
    voice.envelope = std::fma(voice.rampDelta,curved,voice.rampStart);
    if (voice.rampRemaining != 0 || voice.stage == VoiceStage::hold) return;

    const auto mode = voiceMode();
    switch (voice.stage)
    {
        case VoiceStage::attack:
            setVoiceStage(voice, VoiceStage::decay);
            break;
        case VoiceStage::decay:
            setVoiceStage(voice, VoiceStage::sustain);
            break;
        case VoiceStage::sustain:
            if (mode == 0) setVoiceStage(voice, VoiceStage::hold);
            else if (mode == 1 || mode == 2) setVoiceStage(voice, VoiceStage::releaseFromSustain);
            else if (mode == 3) setVoiceStage(voice, VoiceStage::decay);
            else setVoiceStage(voice, VoiceStage::release);
            break;
        case VoiceStage::releaseFromSustain:
            if (mode == 3) setVoiceStage(voice, VoiceStage::decay);
            else if (mode == 2 && settings.glideMs > 0.0f) setVoiceStage(voice, VoiceStage::attack);
            else if (mode == 2) setVoiceStage(voice, VoiceStage::releaseFromSustain);
            else setVoiceStage(voice, VoiceStage::inactive);
            break;
        case VoiceStage::release:
        case VoiceStage::hold:
            setVoiceStage(voice, VoiceStage::inactive);
            break;
        case VoiceStage::inactive:
            voice.active = false;
            break;
    }
}

void SpectralEngine::noteOff(int note)
{
    heldNotes[static_cast<size_t>(juce::jlimit(0, 127, note))].active=false;
    for (auto& pending : pendingNotes)
        if (pending.active && pending.note == note) pending.active = false;
    for (auto& voice : voices)
    {
        if (!voice.active || voice.note != note) continue;
        voice.noteOn = false;
        if (settings.monoPolyMode == 0)
        {
            const PendingNote* fallback=nullptr;
            for (const auto& held : heldNotes)
                if (held.active && (!fallback || held.age > fallback->age)) fallback=&held;
            if (fallback)
            {
                // Native Mono returns to the last still-held note without
                // restarting the generic envelope or replacing the partial tail.
                voice.note=fallback->note;voice.noteOn=true;voice.forceStopped=false;
                setVoicePitch(voice,fallback->note,true);
                continue;
            }
        }
        if (sustainPedal || (settings.monoPolyMode == 0 && !settings.midiGate)) continue;
        releaseVoice(voice, false);
    }
}

void SpectralEngine::releaseVoices(bool onlySustained)
{
    for (auto& voice : voices)
    {
        if ((!voice.active && !voice.held) || (onlySustained && (voice.noteOn || voice.releasing))) continue;
        voice.noteOn=false;
        releaseVoice(voice, !onlySustained);
    }
}

void SpectralEngine::releaseVoice(Voice& voice, bool force)
{
    voice.held = false;
    voice.releasing = true;
    voice.forceStopped = force;
    setVoiceStage(voice, VoiceStage::release);
    // Native bd2a40 primes the ramp once without committing the returned value.
    if (voice.rampRemaining > 0) --voice.rampRemaining;
    voice.rampPosition = std::fma(-voice.rampIncrement,static_cast<float>(voice.rampRemaining),1.0f);
    // The native force-stop callback clears only the matching Poly voice's
    // partial states. Mono and ordinary note-off retain the resonator tail.
    if (force && settings.pitchMode == 1 && settings.monoPolyMode == 1)
    {
        const auto owner = static_cast<int>(&voice - voices.data());
        for (auto& bank : randomSlots) for (auto& slot : bank)
            for (int lane=0; lane<4; ++lane) if (slot.owner[lane] == owner)
            { slot.amplitude[lane]=0;slot.target[lane]=0;slot.cachedExcitation[lane]=false; }
    }
}

void SpectralEngine::tickVoices()
{
    for (auto& voice : voices) if (voice.active)
    {
        updateVoicePitch(voice);
        advanceVoice(voice);
    }
    if (settings.monoPolyMode == 1) startPendingNotes();
}

void SpectralEngine::retimeReleasedVoices()
{
    for(auto& voice:voices)
        if(voice.stage==VoiceStage::release || voice.stage==VoiceStage::releaseFromSustain)
        {
            const auto fraction=juce::jmax(0.0f,1.0f-voice.rampPosition);
            const auto ticks=static_cast<std::uint64_t>(fraction*juce::jmax(0.0f,settings.decayMs*.001f)
                *static_cast<float>(sampleRate/hopSize));
            voice.rampRemaining=ticks;
            if(ticks>0 && voice.rampPosition!=1)voice.rampIncrement=fraction/static_cast<float>(ticks);
            else { voice.rampPosition=1;voice.rampRemaining=0; }
        }
}

void SpectralEngine::refreshDecayCoefficients()
{
    const auto ticks=juce::jmax(1.0e-6f,static_cast<float>(sampleRate/hopSize)*settings.decayMs*.001f);
    const auto base=std::exp2(-9.965784f/ticks);
    for(auto& bank:randomSlots)for(auto& slot:bank)for(int lane=0;lane<4;++lane)
    {
        slot.decay[lane]=cachedDampingGain(slot.harmonic[lane])*base;
        // slot.decay is authoritative immediately. The Granular copy is a
        // derived cache, synchronized in synthesiseSample before its sole
        // product read in modulationGroup. Avoid dirtying all modulation-state
        // cache lines for every automation sample.
    }
}

void SpectralEngine::refreshDampingGainCache()
{
    // Invalidate by parameter revision, rather than evaluating 256 exponentials
    // when the current 16-voice/U8 layout only uses harmonics 0..15. Every slot
    // still gets its new coefficient immediately; an expanded layout computes
    // newly needed harmonics before their first use. No approximation/coalescing.
    dampingCacheHigh=juce::jlimit(0.0f,1.0f,settings.highFreqDamp*.01f);
    dampingCacheLow=juce::jlimit(0.0f,1.0f,settings.lowFreqDamp*.01f);
    dampingCacheExponent=static_cast<double>(-10.0f*(1.0f-2.0f*dampingCacheHigh));
    dampingCacheDenominator=dampingCacheHigh>.5f && dampingCacheExponent>.001f ? std::expm1(dampingCacheExponent) : 1.0;
    if (++dampingRevision == 0)
    {
        dampingGainRevision.fill(0);
        dampingRevision=1;
    }
    dampingGainCacheValid=true;
}

float SpectralEngine::cachedDampingGain(int harmonic)
{
    harmonic=juce::jlimit(0,maxHarmonics-1,harmonic);
    if(dampingGainRevision[harmonic] != dampingRevision)
    {
        const auto position=juce::jmin(1.0f,static_cast<float>(harmonic)/255.0f);
        const auto lowGain=juce::jlimit(0.0f,1.0f,1.0f+.02f*harmonic-.2f*dampingCacheLow);
        if(dampingCacheHigh>.5f && dampingCacheExponent<=.001f)
            dampingGainCache[harmonic]=(1.0f-position)*lowGain;
        else
        {
            const auto highGain=dampingCacheHigh>.5f
                ? static_cast<float>(std::expm1(dampingCacheExponent*(1.0-position))/dampingCacheDenominator)
                : 1.0f-2.0f*dampingCacheHigh*position;
            dampingGainCache[harmonic]=juce::jlimit(0.0f,1.0f,highGain)*lowGain;
        }
        dampingGainRevision[harmonic]=dampingRevision;
    }
    return dampingGainCache[harmonic];
}

void SpectralEngine::handleMidiEvent(const MidiEvent& event)
{
    const auto type = event.status & 0xf0;
    if (type == 0x90 && event.data2 > 0)
        noteOn(event.data1, static_cast<float>(event.data2) / 127.0f);
    else if (type == 0x80 || (type == 0x90 && event.data2 == 0))
        noteOff(event.data1);
    else if (type == 0xe0)
        pitchBend = (static_cast<int>((event.data2 << 7) | event.data1) - 8192)
            * static_cast<float>(settings.pitchBendRange) / 8192.0f;
    else if (type == 0xb0 && event.data1 == 64)
    {
        sustainPedal = event.data2 >= 64;
        if (!sustainPedal && (settings.monoPolyMode == 1 || settings.midiGate)) releaseVoices(true);
    }
    else if (type == 0xb0 && (event.data1 == 120 || event.data1 == 123))
    {
        pendingNotes.fill({});
        heldNotes.fill({});
        releaseVoices(false);
    }
}

float SpectralEngine::mapNote(float note) const
{
    return settings.useScale ? Parameters::mapNoteToScale(settings.scale, settings.scaleRoot,
        note, settings.transposeScaleDegrees) : note;
}

float SpectralEngine::midiNoteFrequency(float note) const
{
    const auto mapped=mapNote(note+(settings.useScale?0:settings.transpose)+pitchBend);
    return 440.0f*std::exp2((mapped-69.0f)/12.0f);
}

void SpectralEngine::setVoicePitch(Voice& voice, int note, bool reuse)
{
    coefficientMetadataDirty=true;
    // bc8ba0 retargets from the last returned note at mapper+48, rather
    // than the ramp's already-advanced next position at mapper+28.
    if(reuse)voice.glideNote=voice.glideOutputNote;
    voice.glideTargetNote=static_cast<float>(note);
    voice.glidePosition=0;
    const auto count=static_cast<std::uint64_t>(juce::jmax(0.0f,
        (settings.monoPolyMode==0?settings.glideMs:0)*.001f*static_cast<float>(sampleRate/hopSize)));
    if (reuse && count>1 && voice.glideNote!=voice.glideTargetNote)
    {
        voice.glideRemaining=count-1;
        voice.glideStep=(voice.glideTargetNote-voice.glideNote)/static_cast<float>(count-1);
    }
    else { voice.glideNote=voice.glideTargetNote;voice.glideRemaining=0;voice.glideStep=0; }
    voice.glideDuration=static_cast<float>(voice.glideRemaining);
    voice.glideOutputNote=voice.glideNote;
    voice.frequency=midiNoteFrequency(voice.glideNote);
    voice.targetFrequency=midiNoteFrequency(voice.glideTargetNote);
}

void SpectralEngine::updateVoicePitch(Voice& voice)
{
    if (voice.note < 0) return;
    const auto previousNote=voice.glideNote;
    voice.glideOutputNote=previousNote;
    if (voice.glideRemaining>0)
    {
        --voice.glideRemaining;
        voice.glideNote=std::fma(-voice.glideStep,static_cast<float>(voice.glideRemaining),voice.glideTargetNote);
        voice.glidePosition+=1;
    }
    voice.targetFrequency=midiNoteFrequency(voice.glideTargetNote);
    const auto frequency=midiNoteFrequency(previousNote);
    coefficientMetadataDirty |= frequency!=voice.frequency;
    voice.frequency=frequency;
    if (settings.pitchMode == 1) displayFundamental.store(voice.frequency);
}

bool SpectralEngine::copyVisualizationData(VisualizationData& destination) const
{
    for (int attempt = 0; attempt < 32; ++attempt)
    {
        const auto before = visualizationSequence.load(std::memory_order_acquire);
        if ((before & 1) != 0) continue;
        for(size_t i=0;i<destination.size();++i) destination[i]=visualizationData[i].load();
        const auto after = visualizationSequence.load(std::memory_order_acquire);
        if (before == after) return true;
    }
    return false;
}

float SpectralEngine::readMagnitude(int channel, float binPosition) const
{
    const auto position = juce::jlimit(0.0f, static_cast<float>(spectrumSize - 1), binPosition);
    const auto index = juce::jmin(spectrumSize - 2, static_cast<int>(position));
    const auto fraction = position - index;
    return magnitudes[channel][index] * (1.0f - fraction) + magnitudes[channel][index + 1] * fraction;
}

float SpectralEngine::partialDampingGain(int harmonic) const
{
    const auto high = juce::jlimit(0.0f, 1.0f, settings.highFreqDamp * 0.01f);
    // APVTS percentages and DSP model coefficients are distinct units.
    // Current binary model constructors confirm both damping coefficients in [0,1].
    const auto low = juce::jlimit(0.0f, 1.0f, settings.lowFreqDamp * 0.01f);
    const auto position = juce::jmin(1.0f, static_cast<float>(harmonic) / 255.0f);
    auto highGain = 1.0f - 2.0f * high * position;
    if (high > 0.5f)
    {
        // FMA231 creates IEEE bias 127 + log2(e)*d, hence exp(d).
        // d tends to zero at 50%; expm1 retains the continuous linear limit.
        const auto d = static_cast<double>(-10.0f * (1.0f - 2.0f * high));
        if (d <= 0.001f)
            return (1.0f - position) * juce::jlimit(0.0f, 1.0f, 1.0f + 0.02f * harmonic - 0.2f * low);
        highGain = static_cast<float>(std::expm1(d * (1.0 - position)) / std::expm1(d));
    }
    const auto lowGain = juce::jlimit(0.0f, 1.0f, 1.0f + 0.02f * harmonic - 0.2f * low);
    return juce::jlimit(0.0f, 1.0f, highGain) * lowGain;
}

std::array<SpectralEngine::ModulationResult, 4> SpectralEngine::modulationGroup(
    const std::array<ModulationState*, 4>& states, int channel,
    const std::array<float, 4>& frequencies)
{
    std::array<ModulationResult, 4> result {};
    if (settings.modMode == 0) return result;
    if (settings.modMode == 1)
    {
        for (int lane = 0; lane < 4; ++lane)
        {
            auto& state = *states[lane];
            const auto frameRate = static_cast<float>(sampleRate / hopSize);
            const auto rate = frequencies[lane] * settings.modRate * 0.01f / 30.0f;
            const auto old = state.phase;
            state.phase += juce::jlimit(0.00001f, frameRate * 0.5f, rate) / frameRate;
            if (state.phase >= 1) state.phase -= 1;
            const auto offset = std::fmod(old + 0.25f, 1.0f);
            const auto shape = std::abs(2 - std::abs(4 * offset - 3));
            result[lane] = { (1 - 0.5f * shape) * 1.4125376f,
                1 - std::abs(2 - std::abs(4 * old - 3)) };
        }
        return result;
    }
    const auto frameRate = static_cast<float>(sampleRate / hopSize);
    const auto r = settings.modRate * 0.01f;
    std::array<bool, 4> triggered {};
    bool any = false;
    if (settings.modMode == 3)
    {
        const auto minimum = juce::jmax(1.0f, frameRate / (0.05f*(1-r)+50*r));
        const auto maximum = juce::jmax(1.0f, frameRate / (0.005f*(1-r)+5*r));
        for (int lane = 0; lane < 4; ++lane)
        {
            auto& state = *states[lane];
            state.countdown -= 1;
            triggered[lane] = state.countdown < 1;
            any |= triggered[lane];
        }
        // AVX mask controls lane replacement, never which RNG lanes advance.
        const auto random = any ? randomUnit4(channel) : std::array<float, 4> {};
        for (int lane = 0; lane < 4; ++lane)
        {
            auto& state = *states[lane];
            const auto impulse = triggered[lane] ? 2.0f : 0.0f;
            if (triggered[lane])
                state.countdown = std::fma(random[lane], maximum,
                    std::fma(-random[lane], minimum, minimum));
            state.granularEnvelope = impulse < state.granularEnvelope
                ? std::fma(impulse, 1-state.granularDecayCoefficient,
                    state.granularEnvelope*state.granularDecayCoefficient) : impulse;
            result[lane] = { state.granularEnvelope,
                state.granularEnvelope * state.granularPitchScale };
        }
        return result;
    }
    std::array<float, 4> nextValue {};
    for (int lane = 0; lane < 4; ++lane)
    {
        const auto& state = *states[lane];
        nextValue[lane] = state.value + state.increment;
        triggered[lane] = state.direction * state.target <= state.direction * nextValue[lane];
        any |= triggered[lane];
    }
    const auto durationRandom = any ? randomUnit4(channel) : std::array<float, 4> {};
    const auto targetRandom = any ? randomUnit4(channel) : std::array<float, 4> {};
    const auto minimum = juce::jmax(1.0f, frameRate / (0.1f*(1-r)+100*r));
    const auto maximum = juce::jmax(1.0f, frameRate / (0.05f*(1-r)+50*r));
    for (int lane = 0; lane < 4; ++lane)
    {
        auto& state = *states[lane];
        if (triggered[lane])
        {
            const auto duration = std::fma(durationRandom[lane], maximum,
                std::fma(-durationRandom[lane], minimum, minimum));
            const auto target = 2*targetRandom[lane]-1;
            state.increment = (target-nextValue[lane]) / juce::jmax(1.0f, std::floor(duration));
            state.value = state.target;
            state.target = target;
        }
        else state.value = nextValue[lane];
        if (any) state.direction = state.increment >= 0 ? 1.0f : -1.0f;
    }
    for (int lane = 0; lane < 4; ++lane)
        result[lane] = { (0.5f * states[(lane+1)%4]->value + 0.5f) * 1.4125376f,
            states[lane]->value };
    return result;
}

SpectralEngine::ModulationResult SpectralEngine::modulationAmount(
    ModulationState& state, const ModulationState& pitchState, int channel, int lane, float frequency)
{
    // Scalar diagnostic adapter; the real processor always calls modulationGroup.
    std::array<ModulationState, 4> copies;
    copies.fill(state);
    copies[(lane+1)%4] = pitchState;
    std::array<ModulationState*, 4> states;
    for (int i=0; i<4; ++i) states[i] = i == lane ? &state : &copies[i];
    std::array<float, 4> frequencies;
    frequencies.fill(frequency);
    return modulationGroup(states, channel, frequencies)[lane];
}

void SpectralEngine::analyseAndUpdatePartials()
{
    visualizationSequence.fetch_add(1, std::memory_order_seq_cst);
    for (int channel = 0; channel < 2; ++channel)
    {
        for (int i = 0; i < fftSize; ++i)
        {
            const auto index = static_cast<int>((sampleCounter + historySize - fftSize + i) % historySize);
            // Original input boundary exchanges the two half frames after windowing.
            fftInput[(i + hopSize) % fftSize] = {
                inputHistory[channel][index] * analysisWindow[i], 0.0f };
        }
        fft.perform(fftInput.data(), fftOutput.data(), false);
        for (int bin = 0; bin < spectrumSize; ++bin)
        {
            const auto dcScale = bin == 0 ? 0.5f : 1.0f;
            magnitudes[channel][bin] = std::abs(fftOutput[bin]) * dcScale;
        }
    }
    for (int channel = 0; channel < 2; ++channel)
        for (int bin = 0; bin < spectrumSize; ++bin)
            visualizationData[channel * spectrumSize + bin].store(magnitudes[channel][bin] * (4.0f / fftSize));
    visualizationSequence.fetch_add(1, std::memory_order_seq_cst);

    tickVoices();
    updatePartialLayout();
    ++frameCounter;
}

void SpectralEngine::updatePartialLayout()
{
    const auto internalBase = internalFrequency();
    const auto unison = juce::jlimit(1, maxUnison, settings.unisonVoices);
    const auto polyphony = settings.pitchMode == 1 && settings.monoPolyMode == 1
        ? juce::jlimit(1, maxVoices, settings.polyphony) : 1;
    const auto totalBudget = juce::jmin(maxActivePartials,
        juce::jlimit(1, maxVoices, settings.polyphony) * unison * maxHarmonics);
    const auto voiceBudget = totalBudget / polyphony;
    allocatedRandomGroups = totalBudget / 4;
    const auto shift = std::exp2(-settings.shift / 12.0f);
    const auto detune = 1 - std::exp2(settings.unisonAmount * 0.01f / 12);
    const auto decaySamples = juce::jmax(1.0e-6f,
        static_cast<float>(sampleRate/hopSize) * settings.decayMs * 0.001f);
    const auto decayBase = std::exp2(-9.965784f / decaySamples);

    for (int voiceIndex = 0; voiceIndex < polyphony; ++voiceIndex)
    {
        auto& voice = voices[voiceIndex];
        const auto fundamental = settings.pitchMode == 0 ? internalBase : voice.frequency;
        voice.analysisFrequency = fundamental;
        const auto harmonics = juce::jmax(0, juce::jmin(settings.harmonics, maxHarmonics, voiceBudget / unison));
        voice.activePartials = harmonics * unison;
        voice.activeHarmonics = harmonics;

        for (int unisonIndex = 0; unisonIndex < unison; ++unisonIndex)
        {
            for (int harmonic = 0; harmonic < juce::jmin(maxHarmonics, voiceBudget / unison); ++harmonic)
            {
                auto& partial = voice.partials[unisonIndex * maxHarmonics + harmonic];
                partial.harmonic = harmonic;
                partial.unisonIndex = unisonIndex;
                // Metadata/RealtimeModel constructor + callback chains identify
                // PartialStretch [-1,1] and UnisonAmount [0,1]; APVTS displays percent.
                const auto ratio = 1.0f + (1.0f + settings.stretch * 0.01f) * harmonic;
                auto frequency = fundamental * ratio;
                if (settings.quantize && frequency > 0.0f)
                {
                    const auto note = 69.0f + 12.0f * std::log2(frequency / 440.0f);
                    const auto quantized = settings.useScale
                        ? Parameters::mapNoteToScale(settings.scale, settings.scaleRoot, note)
                        : std::round(note);
                    frequency = 440.0f * std::pow(2.0f, (quantized - 69.0f) / 12.0f);
                }
                const auto harmonicFrequency = frequency;
                frequency *= 1.0f - detune * unisonIndex;
                const auto rightFrequency = harmonicFrequency * (1.0f + detune * unisonIndex);
                const auto leftActive = std::isfinite(frequency) && frequency > 0.01f
                    && frequency < sampleRate * 0.495;
                const auto rightActive = std::isfinite(rightFrequency) && rightFrequency > 0.01f
                    && rightFrequency < sampleRate * 0.495;
                partial.active = harmonic < harmonics && (leftActive || rightActive);
                partial.baseFrequency = frequency;
                partial.rightBaseFrequency = rightFrequency;
                partial.frequency = frequency;
                const auto binPosition = frequency * shift * static_cast<float>(fftSize / sampleRate);
                const auto damping = cachedDampingGain(harmonic);
                partial.decayCoefficient = damping * decayBase;
                partial.leftModulation.granularDecayCoefficient = partial.decayCoefficient;
                partial.rightModulation.granularDecayCoefficient = partial.decayCoefficient;
                const auto excitation = excitationEnabled(voice);
                partial.leftTarget = partial.active && leftActive && excitation ? readMagnitude(0, binPosition) : 0.0f;
                partial.rightTarget = partial.active && rightActive && excitation ? readMagnitude(1, rightFrequency * shift * static_cast<float>(fftSize/sampleRate)) : 0.0f;
            }
        }
    }

    // All allocated vectors have modulation state, including harmonics beyond
    // the public 256 limit when the mono/internal layout retains poly capacity.
    for (int n = 0; n < totalBudget; ++n)
    {
        const auto owner = juce::jmin(polyphony-1, n / voiceBudget);
        const auto local = n % voiceBudget;
        const auto harmonic = local / unison;
        const auto u = local % unison;
        const auto cacheIndex = harmonic < maxHarmonics ? u * maxHarmonics + harmonic : -1;
        const auto ratio = 1 + (1 + settings.stretch * 0.01f) * harmonic;
        auto base = (settings.pitchMode == 0 ? internalBase : voices[owner].frequency) * ratio;
        if (cacheIndex < 0 && settings.quantize && base > 0)
        {
            const auto note = 69 + 12*std::log2(base/440);
            const auto quantized = settings.useScale
                ? Parameters::mapNoteToScale(settings.scale, settings.scaleRoot, note) : std::round(note);
            base = 440 * std::exp2((quantized-69)/12);
        }
        const auto decay = cachedDampingGain(harmonic) * decayBase;
        for (int channel = 0; channel < 2; ++channel)
        {
            auto& slot = randomSlots[channel][n / 4];
            const auto lane = n % 4;
            slot.owner[lane] = owner;
            slot.harmonic[lane] = harmonic;
            slot.cacheIndex[lane] = cacheIndex;
            slot.frequency[lane] = cacheIndex >= 0
                ? (channel == 0 ? voices[owner].partials[cacheIndex].baseFrequency
                    : voices[owner].partials[cacheIndex].rightBaseFrequency)
                : base * (1 + (channel == 0 ? -1 : 1) * detune * u);
            slot.decay[lane] = decay;
            slot.renderMask[lane] = harmonic < settings.harmonics
                && std::isfinite(slot.frequency[lane]) && slot.frequency[lane] > 0.01f
                && slot.frequency[lane] < sampleRate * 0.495;
            slot.cachedExcitation[lane] = excitationEnabled(voices[owner]);
            slot.target[lane] = slot.renderMask[lane] && slot.cachedExcitation[lane]
                ? readMagnitude(channel, slot.frequency[lane] * shift * static_cast<float>(fftSize/sampleRate)) : 0;
        }
    }

    if(coefficientMetadataDirty && (settings.highFreqDamp!=0 || settings.lowFreqDamp!=0)) retimeReleasedVoices();
    coefficientMetadataDirty=false;

    randomGroupCursor = 0;
}

void SpectralEngine::synthesiseSample()
{
    if (randomGroupCursor >= allocatedRandomGroups) return;
    const auto group = randomGroupCursor++;
    // Native bcb820 reads the current Shift scalar and FFT for the group being
    // visited; bd2be0 uses the current H threshold. Frequency/layout and MIDI
    // excitation remain hop-cached. H must not prematurely latch a NoteOff.
    const auto analysisShift = std::exp2(-settings.shift / 12.0f);
    for (int channel = 0; channel < 2; ++channel)
    {
        auto& slot = randomSlots[channel][group];
        std::array<ModulationState*, 4> states;
        bool anyValid = false;
        for (int lane = 0; lane < 4; ++lane)
        {
            const auto frequency = slot.frequency[lane];
            slot.renderMask[lane] = slot.harmonic[lane] < settings.harmonics
                && std::isfinite(frequency) && frequency > 0.01f && frequency < sampleRate * 0.495;
            slot.target[lane] = slot.renderMask[lane] && slot.cachedExcitation[lane]
                ? readMagnitude(channel, frequency * analysisShift * static_cast<float>(fftSize / sampleRate)) : 0;
            states[lane] = &slot.modulation[lane];
            states[lane]->granularDecayCoefficient = slot.decay[lane];
            anyValid |= slot.renderMask[lane];
        }
        // Modulation always advances, even when all spectral lanes are hidden.
        const auto modulation = modulationGroup(states, channel, slot.frequency);
        bool anyAudible = false;
        if (anyValid)
            for (int lane = 0; lane < 4; ++lane)
            {
                auto value = slot.target[lane];
                if (value < slot.amplitude[lane])
                    value = std::fma(slot.decay[lane], slot.amplitude[lane],
                        std::fma(-slot.decay[lane], value, value));
                slot.amplitude[lane] = slot.renderMask[lane] && std::isfinite(value)
                    ? juce::jmax(0.0f, value) : 0;
                anyAudible |= slot.amplitude[lane] >= 0.001f;
            }
        for (int lane = 0; lane < 4; ++lane)
        {
            const auto frequency = slot.frequency[lane]
                * (1 + pitchModDepth(settings.pitchModAmount) * modulation[lane].pitch);
            if (anyAudible && std::isfinite(frequency))
            {
                const auto radians = juce::MathConstants<float>::twoPi * frequency * hopSize / static_cast<float>(sampleRate);
                slot.phase[lane] = std::remainder(slot.phase[lane] + radians, juce::MathConstants<float>::twoPi);
            }
            // Voice bookkeeping and velocity are not extra output multipliers.
            const auto amplitude = slot.amplitude[lane] * modulation[lane].amplitude;
            if (anyAudible && slot.renderMask[lane] && std::isfinite(frequency) && frequency > 0)
            {
                const auto value = polynomialSinCos(slot.phase[lane]) * amplitude;
                const auto binPosition = frequency * static_cast<float>(fftSize / sampleRate);
                const auto integerBin = static_cast<int>(std::ceil(binPosition));
                const auto phase = (static_cast<float>(integerBin) - binPosition) * 4.0f;
                const auto kernelPhase = juce::jmin(3, static_cast<int>(phase));
                const auto kernelFraction = phase - static_cast<float>(kernelPhase);
                const auto centreBin = integerBin - 4;
                for (int tap = 0; tap < kernelTaps; ++tap)
                {
                    const auto kernelIndex = kernelPhase + tap * 4;
                    const auto weight = kernel[kernelIndex] * (1.0f - kernelFraction)
                        + kernel[kernelIndex + 1] * kernelFraction;
                    const auto bin = centreBin + tap;
                    if (bin == 0)
                        outputSpectrum[channel][0].real(outputSpectrum[channel][0].real()
                            + 2.0f * value.real() * weight);
                    else if (bin < 0)
                        outputSpectrum[channel][-bin] += std::complex<float> {
                            value.real() * weight, -value.imag() * weight };
                    else if (bin < spectrumSize)
                        outputSpectrum[channel][bin] += value * weight;
                }
            }
            if (slot.cacheIndex[lane] >= 0)
            {
                auto& partial = voices[slot.owner[lane]].partials[slot.cacheIndex[lane]];
                partial.frequency = frequency;
                if (channel == 0)
                {
                    partial.leftAmplitude = slot.amplitude[lane];
                    partial.leftPhase = slot.phase[lane];
                    partial.leftModulation = *states[lane];
                    partial.leftModulationResult = modulation[lane];
                }
                else
                {
                    partial.rightAmplitude = slot.amplitude[lane];
                    partial.rightPhase = slot.phase[lane];
                    partial.rightModulation = *states[lane];
                    partial.rightModulationResult = modulation[lane];
                }
            }
        }
    }
}

void SpectralEngine::renderSpectrum()
{
    for (int channel = 0; channel < 2; ++channel)
    {
        fftInput[0] = { outputSpectrum[channel][0].real(), 0.0f };
        for (int bin = 1; bin < spectrumSize - 1; ++bin)
        {
            fftInput[bin] = outputSpectrum[channel][bin];
            fftInput[fftSize - bin] = std::conj(outputSpectrum[channel][bin]);
        }
        fftInput[spectrumSize - 1] = { outputSpectrum[channel][spectrumSize - 1].real(), 0.0f };
        fft.perform(fftInput.data(), fftOutput.data(), true);
        for (int i = 0; i < fftSize; ++i)
        {
            const auto index = static_cast<int>((sampleCounter + i) % historySize);
            // The spectral kernel already supplies the synthesis window.
            // JUCE's inverse is 1/N normalised; no additional hop gain is required.
            synthesisHistory[channel][index] += fftOutput[(i + hopSize) % fftSize].real();
        }
        outputSpectrum[channel].fill({ 0.0f, 0.0f });
    }
}

float SpectralEngine::applyOutputDynamics(float input, int channel)
{
    const auto magnitude = juce::jmax(1.0e-6f, std::abs(input));
    const auto overshoot = juce::jmax(0.0f, 20.0f * std::log10(magnitude));
    const auto target = overshoot * (1.0e-6f - 1.0f);
    const auto attack = -std::expm1(-1.0f / (static_cast<float>(sampleRate) * 0.001f));
    const auto release = -std::expm1(-1.0f / (static_cast<float>(sampleRate) * 0.25f));
    auto& state = outputGainState[channel];
    const auto coefficient = target < state ? attack : release;
    state += coefficient * (target - state);
    const auto gain = std::exp2(state * 0.1660964f);
    if (!std::isfinite(gain))
    {
        state = 0.0f;
        return input;
    }
    return input * gain;
}

bool SpectralEngine::hasPendingResonance() const noexcept
{
    for (int channel = 0; channel < 2; ++channel)
        for (int group = 0; group < allocatedRandomGroups; ++group)
        {
            const auto& slot = randomSlots[channel][static_cast<size_t>(group)];
            for (int lane = 0; lane < 4; ++lane)
                if (slot.renderMask[lane] && slot.amplitude[lane] >= 0.001f) return true;
        }
    return false;
}

bool SpectralEngine::beginHostBlock(bool inputActive, bool midiPending, int numSamples) noexcept
{
    if (numSamples <= 0) return hostBlockActive;
    const auto hasActivity = inputActive || midiPending;
    auto active = hasActivity || activityGuardSamples != 0 || quietSamples < inactivitySamples;
    // A valid Live input descriptor can keep processing even when all its
    // samples are zero. Raw VST3 samples cannot express that distinction. Keep
    // pending resonances alive too: Granular may open a silent envelope later.
    // Check only at a prospective sleep, then amortize the scan with the guard.
    const auto pendingResonance = !active && hasPendingResonance();
    active |= pendingResonance;
    if (active && !hostBlockActive) activationResetPending = true;
    hostBlockActive = active;
    // VST3 has no Live buffer-activity descriptor. Keep short input/MIDI events
    // alive through the FFT/OLA pipeline before the output detector can see
    // them. Count from this host block's end, including large/split callbacks.
    // This guard is an adapter policy, separate from the native quiet counter.
    if (hasActivity || pendingResonance)
        activityGuardSamples = inactivitySamples + static_cast<std::uint64_t>(numSamples);
    return active;
}

void SpectralEngine::processInactive(float* outputLeft, float* outputRight, int numSamples) noexcept
{
    std::fill_n(outputLeft, numSamples, 0.0f);
    std::fill_n(outputRight, numSamples, 0.0f);
    // Keep actual zero-output display frames flowing without advancing the DSP
    // clock, oscillators, MIDI envelopes or random streams while dormant.
    if (spectrogram.beginBlock(sampleRate))
        for (int sample = 0; sample < numSamples; ++sample) spectrogram.push(0, 0, 0, 0);
}

void SpectralEngine::process(const float* inputLeft, const float* inputRight,
                             float* outputLeft, float* outputRight,
                             int numSamples, const juce::MidiBuffer& midi, bool bypass,
                             int midiStartSample, juce::MidiBufferIterator* midiCursor)
{
    // The processor has already flushed its current parameter snapshot. Native
    // first activation and reactivation clear signal state after that flush,
    // before consuming MIDI/sample data. Empty blocks do not consume the flag.
    if(numSamples>0 && activationResetPending){activationResetPending=false;reset();}
    // Automation splits the audio buffer without copying or reallocating MIDI.
    // Share a forward iterator across ranges so dense automation never rescans
    // already consumed MIDI. Each range is half-open in original sample time.
    auto midiIterator = midiCursor != nullptr ? *midiCursor
        : midiStartSample > 0 ? midi.findNextSamplePosition(midiStartSample) : midi.cbegin();
    const auto midiEnd = midi.cend();
    // Keep raw dry history separate so automation never rescales older samples.
    const auto excitationGain = std::pow(10.0f, settings.inputSendDb / 20.0f)
        * std::exp2(-settings.shift / 24.0f)
        * std::sqrt(juce::jmax(0.0f, settings.stretch * 0.01f + 1.001f))
        / std::sqrt(static_cast<float>(juce::jlimit(1, maxUnison, settings.unisonVoices)));
    const auto captureSpectrogram = spectrogram.beginBlock(sampleRate);
    for (int sample = 0; sample < numSamples; ++sample)
    {
        while (midiIterator != midiEnd && (*midiIterator).samplePosition <= sample + midiStartSample)
        {
            const auto event = *midiIterator++;
            if (event.numBytes >= 2 && event.numBytes <= 3)
            {
                handleMidiEvent({ event.samplePosition, event.data[0], event.data[1],
                    static_cast<std::uint8_t>(event.numBytes > 2 ? event.data[2] : 0) });
            }
        }

        const auto historyIndex = static_cast<int>(sampleCounter % historySize);
        dryHistory[0][historyIndex] = inputLeft[sample];
        dryHistory[1][historyIndex] = inputRight[sample];
        inputHistory[0][historyIndex] = inputLeft[sample] * excitationGain;
        inputHistory[1][historyIndex] = inputRight[sample] * excitationGain;

        const auto dryIndex = static_cast<int>((sampleCounter + historySize - latencySamples) % historySize);
        const auto angle = (bypass ? 0.0f : settings.dryWet) * juce::MathConstants<float>::halfPi;
        const auto dryGain = std::cos(angle);
        const auto wetGain = std::sin(angle);
        const auto dryLeft = dryHistory[0][dryIndex] * dryGain;
        const auto dryRight = dryHistory[1][dryIndex] * dryGain;
        const auto wetLeft = applyOutputDynamics(synthesisHistory[0][historyIndex] * wetGain, 0);
        const auto wetRight = applyOutputDynamics(synthesisHistory[1][historyIndex] * wetGain, 1);
        const auto left = wetLeft + dryLeft;
        const auto right = wetRight + dryRight;
        if (captureSpectrogram) spectrogram.push(dryLeft, dryRight, wetLeft, wetRight);
        outputLeft[sample] = std::isfinite(left) ? left : 0.0f;
        outputRight[sample] = std::isfinite(right) ? right : 0.0f;
        if (std::abs(outputLeft[sample]) > silenceThreshold || std::abs(outputRight[sample]) > silenceThreshold)
            quietSamples = 0;
        else if (quietSamples != std::numeric_limits<std::uint64_t>::max())
            ++quietSamples;
        if (activityGuardSamples != 0) --activityGuardSamples;
        synthesisHistory[0][historyIndex] = 0.0f;
        synthesisHistory[1][historyIndex] = 0.0f;

        synthesiseSample();
        ++sampleCounter;
        // Native pops current wet first, then ticks groups and decrements the
        // hop counter. The frame at n=511 writes OLA beginning at n=512.
        if(sampleCounter%hopSize==0)
        {
            renderSpectrum();
            analyseAndUpdatePartials();
        }
    }
    if (midiCursor != nullptr) *midiCursor = midiIterator;
}
