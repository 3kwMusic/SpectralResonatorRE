// SPDX-FileCopyrightText: 2026 TriM
// SPDX-License-Identifier: AGPL-3.0-only

#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_dsp/juce_dsp.h>
#include <array>
#include <atomic>
#include <complex>
#include <limits>
#include <vector>
#include "SpectrogramStream.h"

class SpectralEngine
{
public:
    struct Settings
    {
        int pitchMode = 0;
        int monoPolyMode = 0;
        int polyphony = 8;
        bool midiGate = false;
        float glideMs = 0.0f;
        int pitchBendRange = 2;
        float frequencyHz = 110.0f;
        float frequencyNote = 45.0f;
        int frequencyDialMode = 0;
        bool useScale = false;
        int scale = 0;
        int scaleRoot = 0;
        float transpose = 0.0f;
        int transposeScaleDegrees = 0;
        float stretch = 0.0f;
        float shift = 0.0f;
        float decayMs = 222.0f;
        float highFreqDamp = 0.0f;
        float lowFreqDamp = 0.0f;
        int modMode = 0;
        float modRate = 10.0f;
        float pitchModAmount = 0.0f;
        int harmonics = 256;
        bool quantize = false;
        float inputSendDb = 0.0f;
        int unisonVoices = 1;
        float unisonAmount = 10.0f;
        float dryWet = 1.0f;
    };

    static constexpr int latencySamples = 1024;
    static constexpr int visualizationBinCount = 513;
    using VisualizationData = std::array<float, visualizationBinCount * 2>;

    void prepare(double sampleRate, int samplesPerBlock, const Settings& initialSettings);
    void reset();
    void suspend() noexcept { activationResetPending = true; }
    // Live supplies input-activity metadata outside its DSP core. The VST3
    // adapter decides once per complete host callback, before automation splits.
    bool beginHostBlock(bool inputActive, bool midiPending, int numSamples) noexcept;
    void processInactive(float* outputLeft, float* outputRight, int numSamples) noexcept;
    enum IntegerCallback : unsigned
    {
        frequencyNoteCallback = 1u,
        transposeCallback = 2u,
        scaleDegreeCallback = 4u
    };
    void setSettings(const Settings& newSettings, unsigned integerCallbacks = 0);
    void process(const float* inputLeft, const float* inputRight,
                 float* outputLeft, float* outputRight, int numSamples,
                 const juce::MidiBuffer& midi, bool bypass = false,
                 int midiStartSample = 0, juce::MidiBufferIterator* midiCursor = nullptr);
    void processBypassed(const float* l, const float* r, float* ol, float* or_, int n, const juce::MidiBuffer& m)
    { process(l,r,ol,or_,n,m,true); }
    float fundamentalForDisplay() const { return displayFundamental.load(); }
    bool copyVisualizationData(VisualizationData& destination) const;
    double visualizationSampleRate() const { return publishedSampleRate.load(std::memory_order_relaxed); }
    void setSpectrogramActive(bool active) { spectrogram.setActive(active); }
    bool readSpectrogramFrame(SpectrogramStream::Frame& frame) { return spectrogram.read(frame); }
    std::uint64_t spectrogramGeneration() const { return spectrogram.currentGeneration(); }

private:
    static constexpr std::uint64_t inactivitySamples = 3584;
    static constexpr float silenceThreshold = 1.0e-6f;
    std::uint64_t quietSamples = std::numeric_limits<std::uint64_t>::max();
    std::uint64_t activityGuardSamples = 0;
    bool hostBlockActive = false;
    bool hasPendingResonance() const noexcept;
    static constexpr int fftSize = 1024;
    static constexpr int hopSize = 512;
    static constexpr int spectrumSize = fftSize / 2 + 1;
    static constexpr int historySize = 2048;
    static constexpr int maxVoices = 16;
    static constexpr int maxUnison = 8;
    static constexpr int maxHarmonics = 256;
    static constexpr int maxPartialsPerVoice = maxUnison * maxHarmonics;
    static constexpr int maxActivePartials = 2048;
    static constexpr int kernelSize = 33;
    static constexpr int kernelTaps = 8;

    struct ModulationState
    {
        float phase = 0.0f;
        float value = 0.0f;
        float target = 0.0f;
        float increment = 0.0f;
        float direction = 1.0f;
        float countdown = 0.0f;
        // Granular state: release is the partial's damped decay, not Mod Rate.
        float granularEnvelope = 0.0f;
        float granularDecayCoefficient = 0.0f;
        float granularPitchScale = 1.0f;
        bool granularStarted = false;
    };

    struct ModulationResult
    {
        float amplitude = 1.0f;
        float pitch = 0.0f;
    };

    // Native storage is 512 four-lane slots per stereo bank. These states
    // outlive audible voices, Harmonics masks and out-of-band partials.
    struct RandomSlot
    {
        std::array<ModulationState, 4> modulation {};
        std::array<float, 4> phase {};
        std::array<float, 4> frequency {};
        std::array<float, 4> decay {};
        std::array<float, 4> amplitude {};
        std::array<float, 4> target {};
        std::array<bool, 4> cachedExcitation {};
        std::array<bool, 4> renderMask {};
        std::array<int, 4> owner {};
        std::array<int, 4> harmonic {};
        std::array<int, 4> cacheIndex { -1, -1, -1, -1 };
    };

    struct Partial
    {
        bool active = false;
        int harmonic = 0;
        int unisonIndex = 0;
        float baseFrequency = 0.0f;
        float rightBaseFrequency = 0.0f;
        bool initialised = false;
        float frequency = 0.0f;
        float leftAmplitude = 0.0f;
        float rightAmplitude = 0.0f;
        float leftTarget = 0.0f;
        float rightTarget = 0.0f;
        float decayCoefficient = 0.0f;
        float leftPhase = 0.0f;
        float rightPhase = 0.0f;
        ModulationState leftModulation {};
        ModulationState rightModulation {};
        ModulationResult leftModulationResult {};
        ModulationResult rightModulationResult {};
    };

    enum class VoiceStage { attack = 0, decay, sustain, hold, release, releaseFromSustain, inactive };

    struct Voice
    {
        bool active = false;
        bool noteOn = false;
        bool releasing = false;
        bool held = false;
        bool forceStopped = false;
        int note = -1;
        int activePartials = 0;
        int activeHarmonics = 0;
        std::uint64_t age = 0;
        float velocity = 1.0f;
        float frequency = 1.0f;
        float targetFrequency = 1.0f;
        float analysisFrequency = 1.0f;
        float glideStartFrequency = 1.0f;
        float glidePosition = 0.0f;
        float glideDuration = 0.0f;
        float glideNote = 0.0f;
        float glideOutputNote = 0.0f;
        float glideTargetNote = 0.0f;
        float glideStep = 0.0f;
        std::uint64_t glideRemaining = 0;
        VoiceStage stage = VoiceStage::inactive;
        float envelope = 0.0f;
        float rampStart = 0.0f;
        float rampDelta = 0.0f;
        float rampPosition = 0.0f;
        float rampIncrement = 0.0f;
        float curve = 0.0f;
        float curveNormalization = 1.0f;
        std::uint64_t rampRemaining = 0;
        std::array<Partial, maxPartialsPerVoice> partials {};
    };

    struct MidiEvent
    {
        int sample = 0;
        std::uint8_t status = 0;
        std::uint8_t data1 = 0;
        std::uint8_t data2 = 0;
    };

    struct PendingNote
    {
        bool active = false;
        int note = -1;
        float velocity = 0.0f;
        std::uint64_t age = 0;
    };

    void resetVoice(Voice& voice);
    void initialiseCore();
    void clearSignalBuffers();
    float internalFrequency() const;
    void handleMidiEvent(const MidiEvent& event);
    void noteOn(int note, float velocity, bool replay = false);
    void noteOff(int note);
    void releaseVoices(bool onlySustained);
    void releaseVoice(Voice& voice, bool force);
    bool excitationEnabled(const Voice& voice) const;
    void tickVoices();
    void retimeReleasedVoices();
    void refreshDecayCoefficients();
    void refreshDampingGainCache();
    float cachedDampingGain(int harmonic);
    void startPendingNotes();
    bool forcePendingVictims();
    int voiceMode() const;
    void beginVoice(Voice& voice);
    void setVoiceStage(Voice& voice, VoiceStage stage);
    void advanceVoice(Voice& voice);
    void updateVoicePitch(Voice& voice);
    void setVoicePitch(Voice& voice, int note, bool reuse);
    float midiNoteFrequency(float note) const;
    float mapNote(float note) const;
    void analyseAndUpdatePartials();
    void updatePartialLayout();
    void synthesiseSample();
    void renderSpectrum();
    float applyOutputDynamics(float input, int channel);
    float readMagnitude(int channel, float binPosition) const;
    float partialDampingGain(int harmonic) const;
    ModulationResult modulationAmount(ModulationState& state, const ModulationState& pitchState,
                                      int channel, int lane, float frequency);
    void rescaleModulation(float oldRate, float newRate);
    void buildKernel();
    void seedRandom();
    void initialiseRandomSlots();
    void resetRandomBank(int channel, int mode, int unison);
    void rerandomisePhases(int channel, bool resetChorus);
    std::array<float, 4> randomUnit4(int channel);
    std::array<ModulationResult, 4> modulationGroup(
        const std::array<ModulationState*, 4>& states, int channel,
        const std::array<float, 4>& frequencies);
    std::uint32_t nextRandom(int channel, int lane);
    float randomUnit(int channel, int lane);
    float randomPhase(int channel, int lane);
    static float phaseFromUnit(float unit) noexcept;
    static float pitchModDepth(float semitones) noexcept;

    Settings settings;
    double sampleRate = 44100.0;
    juce::dsp::FFT fft { 10 };
    std::vector<Voice> voices { maxVoices };
    std::array<std::array<float, historySize>, 2> inputHistory {};
    std::array<std::array<float, historySize>, 2> dryHistory {};
    std::array<std::array<float, historySize>, 2> synthesisHistory {};
    std::array<std::array<float, spectrumSize>, 2> magnitudes {};
    std::array<std::array<std::complex<float>, spectrumSize>, 2> outputSpectrum {};
    std::array<std::complex<float>, fftSize> fftInput {};
    std::array<std::complex<float>, fftSize> fftOutput {};
    std::array<float, fftSize> analysisWindow {};
    std::array<float, kernelSize> kernel {};
    // Damping depends only on integer harmonic and HF/LF. All harmonics at
    // 255 and above have the same clamped result, including hidden slots.
    std::array<float, maxHarmonics> dampingGainCache {};
    std::array<std::uint64_t, maxHarmonics> dampingGainRevision {};
    std::uint64_t dampingRevision = 0;
    float dampingCacheHigh = 0, dampingCacheLow = 0;
    double dampingCacheExponent = 0, dampingCacheDenominator = 1;
    bool dampingGainCacheValid = false;
    std::array<PendingNote, maxVoices> pendingNotes {};
    std::array<PendingNote, 128> heldNotes {};
    std::array<std::array<std::array<std::uint32_t, 2>, 4>, 2> randomState {};
    std::array<std::array<std::uint32_t, 4>, 2> storedRandomSeeds {};
    bool bankSeedsReady = false;
    std::array<std::vector<RandomSlot>, 2> randomSlots {
        std::vector<RandomSlot>(512), std::vector<RandomSlot>(512) };
    int randomGroupCursor = 0;
    int allocatedRandomGroups = 0;
    bool randomSeeded = false;
    bool prepared = false;
    bool activationResetPending = false;
    bool coefficientMetadataDirty = true;
    std::uint64_t sampleCounter = 0;
    std::uint64_t frameCounter = 0;
    std::uint64_t ageCounter = 0;
    std::array<float, 2> outputGainState {};
    float pitchBend = 0.0f;
    bool sustainPedal = false;
    std::array<std::atomic<float>, visualizationBinCount * 2> visualizationData {};
    std::atomic<double> publishedSampleRate {44100.0};
    std::atomic<std::uint32_t> visualizationSequence { 0 };
    std::atomic<float> displayFundamental { 110.0f };
    SpectrogramStream spectrogram;
};
