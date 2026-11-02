// SPDX-FileCopyrightText: 2026 TriM
// SPDX-License-Identifier: AGPL-3.0-only

#include "../../Source/PluginProcessor.h"
#include "../../Source/PluginEditor.h"
#include <algorithm>
#include <cmath>
#include <functional>
#include <iostream>
#include <vector>

void setValue(SpectralResonatorProcessor& processor, const juce::String& id, float value)
{
    auto* parameter = processor.apvts.getParameter(id);
    parameter->setValueNotifyingHost(parameter->getNormalisableRange().convertTo0to1(value));
}

void setChoice(SpectralResonatorProcessor& processor, const juce::String& id, int index, int count)
{
    processor.apvts.getParameter(id)->setValueNotifyingHost(static_cast<float>(index) / static_cast<float>(count - 1));
}

void require(bool condition, const char* message)
{
    if (!condition) throw std::runtime_error(message);
}

struct Rendered
{
    std::vector<float> left;
    std::vector<float> right;
};

Rendered render(SpectralResonatorProcessor& processor, int numSamples, int blockSize, double sampleRate,
                const std::function<float(int, int)>& input,
                const std::function<void(juce::MidiBuffer&, int)>& events = {})
{
    processor.prepareToPlay(sampleRate, blockSize);
    Rendered result { std::vector<float>(numSamples), std::vector<float>(numSamples) };
    for (int start = 0; start < numSamples; start += blockSize)
    {
        const auto count = juce::jmin(blockSize, numSamples - start);
        juce::AudioBuffer<float> buffer(2, count);
        for (int channel = 0; channel < 2; ++channel)
            for (int sample = 0; sample < count; ++sample)
                buffer.setSample(channel, sample, input(start + sample, channel));
        juce::MidiBuffer midi;
        if (events) events(midi, start);
        processor.processBlock(buffer, midi);
        for (int sample = 0; sample < count; ++sample)
        {
            result.left[start + sample] = buffer.getSample(0, sample);
            result.right[start + sample] = buffer.getSample(1, sample);
        }
    }
    return result;
}

void configureNeutral(SpectralResonatorProcessor& processor)
{
    setChoice(processor, Parameters::pitchMode, 0, 2);
    setChoice(processor, Parameters::frequencyDialMode, 1, 2);
    setValue(processor, Parameters::frequencyNote, 45.0f);
    setValue(processor, Parameters::frequencyHz, 110.0f);
    setValue(processor, Parameters::stretch, 0.0f);
    setValue(processor, Parameters::shift, 0.0f);
    setValue(processor, Parameters::decayMs, 500.0f);
    setValue(processor, Parameters::highFreqDamp, 0.0f);
    setValue(processor, Parameters::lowFreqDamp, 0.0f);
    setChoice(processor, Parameters::modMode, 0, 4);
    setValue(processor, Parameters::harmonics, 8.0f);
    setValue(processor, Parameters::inputSendDb, 0.0f);
    setChoice(processor, Parameters::unison, 0, 4);
    setValue(processor, Parameters::unisonAmount, 0.0f);
    setValue(processor, Parameters::dryWet, 1.0f);
}

float sine(int sample, double sampleRate, float frequency, float amplitude)
{
    return amplitude * std::sin(juce::MathConstants<float>::twoPi * frequency
                                * static_cast<float>(sample / sampleRate));
}

float noise(int sample, int channel)
{
    auto value = static_cast<std::uint32_t>((sample + 1) * 0x9e3779b9u + (channel + 1) * 0x85ebca6bu);
    value ^= value >> 16;
    value *= 0x7feb352d;
    value ^= value >> 15;
    return (static_cast<float>((value >> 8) & 0xffffu) / 32767.5f - 1.0f) * 0.35f;
}

float rms(const std::vector<float>& samples, int start, int count)
{
    auto sum = 0.0;
    for (int i = 0; i < count; ++i) sum += static_cast<double>(samples[start + i]) * samples[start + i];
    return static_cast<float>(std::sqrt(sum / count));
}

float energyAt(const std::vector<float>& samples, int start, int count, double sampleRate, float frequency)
{
    auto real = 0.0;
    auto imaginary = 0.0;
    for (int i = 0; i < count; ++i)
    {
        const auto phase = juce::MathConstants<double>::twoPi * frequency * i / sampleRate;
        real += samples[start + i] * std::cos(phase);
        imaginary -= samples[start + i] * std::sin(phase);
    }
    return static_cast<float>(2.0 * std::sqrt(real * real + imaginary * imaginary) / count);
}

void requireFinite(const Rendered& result)
{
    for (const auto sample : result.left) require(std::isfinite(sample), "Non-finite left output");
    for (const auto sample : result.right) require(std::isfinite(sample), "Non-finite right output");
}

void runDryWetProbe()
{
    for (const auto wet : { 0.0f, 1.0f })
    {
        SpectralResonatorProcessor processor;
        require(processor.getLatencySamples() == SpectralEngine::latencySamples,
                "Constructor did not expose frame latency");
        configureNeutral(processor);
        setValue(processor, Parameters::dryWet, wet);
        const auto result = render(processor, 1100, 64, 48000.0,
            [] (int sample, int channel) { return sample == 0 ? (channel == 0 ? 1.0f : 0.5f) : 0.0f; });
        requireFinite(result);
        require(processor.getLatencySamples() == SpectralEngine::latencySamples, "Incorrect reported latency");
        std::cout << "dry/wet debug wet=" << wet << " left=" << result.left[1024]
                  << " right=" << result.right[1024] << std::endl;
        if (wet == 0.0f)
        {
            require(std::abs(result.left[1024] - 1.0f) < 1.0e-5f, "Delayed dry left channel mismatch");
            require(std::abs(result.right[1024] - 0.5f) < 1.0e-5f, "Delayed dry right channel mismatch");
        }
        else
            require(result.left[0] == 0.0f && result.right[0] == 0.0f, "Wet path contains undelayed dry input");
    }
    std::cout << "dry_wet_and_latency: PASS" << std::endl;
}

void runOutputDynamicsProbe()
{
    const auto renderLevel = [] (float level)
    {
        SpectralResonatorProcessor processor;
        configureNeutral(processor);
        setValue(processor, Parameters::harmonics, 1.0f);
        setChoice(processor, Parameters::frequencyDialMode, 0, 2);
        setValue(processor, Parameters::frequencyHz, 375.0f);
        return render(processor, 16384, 64, 48000.0,
            [level] (int sample, int) { return sine(sample, 48000.0, 375.0f, level); });
    };
    const auto quiet = renderLevel(0.1f);
    const auto overloaded = renderLevel(8.0f);
    requireFinite(quiet);
    requireFinite(overloaded);
    const auto quietRms = rms(quiet.left, 8192, 4096);
    const auto overloadedRms = rms(overloaded.left, 8192, 4096);
    std::cout << "output dynamics quiet=" << quietRms << " overloaded=" << overloadedRms << std::endl;


    require(overloadedRms > quietRms * 4.0f && overloadedRms < quietRms * 40.0f,
            "Wet output dynamics failed steady transfer-curve check");
    std::cout << "output_dynamics: PASS" << std::endl;
}

void runResonanceProbe()
{
    SpectralResonatorProcessor processor;
    configureNeutral(processor);
    setValue(processor, Parameters::harmonics, 4.0f);
    setValue(processor, Parameters::decayMs, 1000.0f);
    const auto result = render(processor, 8192, 64, 48000.0,
        [] (int sample, int channel) { return sine(sample, 48000.0, 110.0f, channel == 0 ? 0.35f : 0.15f); });
    requireFinite(result);
    SpectralEngine::VisualizationData visualization;
    require(processor.copyVisualizationData(visualization), "Could not acquire a coherent FFT visualization snapshot");
    require(std::any_of(visualization.begin(), visualization.end(), [] (float value) { return value > 0.0f; }),
            "FFT visualization did not contain input magnitudes");
    const auto tuned = energyAt(result.left, 4096, 2048, 48000.0, 110.0f);
    const auto offTuned = energyAt(result.left, 4096, 2048, 48000.0, 165.0f);
    std::cout << "resonance debug rms=" << rms(result.left, 4096, 2048) << " tuned=" << tuned << " off=" << offTuned << std::endl;
    require(tuned > 1.0e-4f, "No resonant output from real processBlock");
    require(tuned > offTuned * 3.0f, "Resonant peak is not selective");
    std::cout << "impulse_and_resonance: PASS (" << tuned << " vs " << offTuned << ")" << std::endl;
}

void runDecayProbe()
{
    SpectralResonatorProcessor processor;
    configureNeutral(processor);
    setValue(processor, Parameters::harmonics, 4.0f);
    setValue(processor, Parameters::decayMs, 200.0f);
    const auto result = render(processor, 12288, 64, 48000.0,
        [] (int sample, int channel) { return sample < 4096 ? sine(sample, 48000.0, 110.0f, 0.35f) : 0.0f; });
    requireFinite(result);
    const auto sustained = rms(result.left, 2048, 1024);
    const auto tail = rms(result.left, 8192, 1024);
    require(sustained > 1.0e-5f, "No sustained signal for decay probe");
    require(tail < sustained * 0.35f, "Decay does not reduce the tail");
    std::cout << "decay_tail: PASS (" << sustained << " -> " << tail << ")" << std::endl;
}

Rendered renderDamping(float high, float low)
{
    SpectralResonatorProcessor processor;
    configureNeutral(processor);
    setValue(processor, Parameters::harmonics, 128.0f);
    setValue(processor, Parameters::decayMs, 2000.0f);
    setValue(processor, Parameters::highFreqDamp, high);
    setValue(processor, Parameters::lowFreqDamp, low);
    return render(processor, 12288, 64, 96000.0,
        [] (int sample, int channel) { return sample < 4096 ? noise(sample, channel) : 0.0f; });
}

Rendered renderLowDamping(float low)
{
    SpectralResonatorProcessor processor;
    configureNeutral(processor);
    setValue(processor, Parameters::harmonics, 8.0f);
    setValue(processor, Parameters::decayMs, 2000.0f);
    setValue(processor, Parameters::lowFreqDamp, low);
    return render(processor, 12288, 64, 96000.0,
        [] (int sample, int) { return sample < 4096 ? sine(sample, 96000.0, 110.0f, 0.35f) : 0.0f; });
}

void runDampingProbe()
{
    const auto reference = renderDamping(0.0f, 0.0f);
    const auto high = renderDamping(100.0f, 0.0f);
    const auto low = renderLowDamping(100.0f);
    const auto lowReference = renderLowDamping(0.0f);
    requireFinite(reference);
    requireFinite(high);
    requireFinite(low);
    const auto referenceHigh = energyAt(reference.left, 8192, 1024, 96000.0, 14080.0f);
    const auto dampedHigh = energyAt(high.left, 8192, 1024, 96000.0, 14080.0f);
    const auto referenceLow = energyAt(lowReference.left, 8192, 1024, 96000.0, 110.0f);
    const auto dampedLow = energyAt(low.left, 8192, 1024, 96000.0, 110.0f);
    std::cout << "damping debug high=" << referenceHigh << " -> " << dampedHigh
              << " low=" << referenceLow << " -> " << dampedLow << std::endl;
    require(dampedHigh < referenceHigh * 0.8f, "HF Damp does not attenuate high partials");
    require(dampedLow < referenceLow * 0.8f, "LF Damp does not attenuate low partials");
    std::cout << "hf_lf_damping: PASS" << std::endl;
}

void runHarmonicsAndUnisonProbe()
{
    const auto renderCase = [] (int harmonics, int unisonChoice)
    {
        SpectralResonatorProcessor processor;
        configureNeutral(processor);
        setValue(processor, Parameters::harmonics, static_cast<float>(harmonics));
        setChoice(processor, Parameters::unison, unisonChoice, 4);
        setValue(processor, Parameters::decayMs, 1000.0f);
        return render(processor, 8192, 64, 48000.0,
            [] (int sample, int) { return sine(sample, 48000.0, 220.0f, 0.35f); });
    };
    const auto base = renderCase(1, 0);
    const auto moreHarmonics = renderCase(2, 0);
    const auto moreUnison = renderCase(1, 1);
    requireFinite(base);
    requireFinite(moreHarmonics);
    requireFinite(moreUnison);
    const auto baseEnergy = energyAt(base.left, 4096, 2048, 48000.0, 220.0f);
    const auto harmonicEnergy = energyAt(moreHarmonics.left, 4096, 2048, 48000.0, 220.0f);
    const auto unisonEnergy = energyAt(moreUnison.left, 4096, 2048, 48000.0, 220.0f);
    std::cout << "harmonics/unison debug base=" << baseEnergy << " harmonics=" << harmonicEnergy
              << " unison=" << unisonEnergy << std::endl;
    require(harmonicEnergy > baseEnergy * 3.0f, "Harmonics did not enable the second partial");
    require(unisonEnergy < harmonicEnergy * 0.2f, "Zero-detune Unison incorrectly enabled a second harmonic");
    std::cout << "harmonics_and_unison: PASS" << std::endl;
}

void runModulationProbe()
{
    std::array<Rendered, 4> results {
        Rendered { std::vector<float>(48000), std::vector<float>(48000) },
        Rendered { std::vector<float>(48000), std::vector<float>(48000) },
        Rendered { std::vector<float>(48000), std::vector<float>(48000) },
        Rendered { std::vector<float>(48000), std::vector<float>(48000) }
    };
    for (int mode = 0; mode < 4; ++mode)
    {
        SpectralResonatorProcessor processor;
        configureNeutral(processor);
        setChoice(processor, Parameters::modMode, mode, 4);
        setValue(processor, Parameters::modRate, 50.0f);
        setValue(processor, Parameters::pitchModAmount, 2.0f);
        setValue(processor, Parameters::harmonics, 4.0f);
        results[mode] = render(processor, 48000, 64, 48000.0,
            [] (int sample, int) { return sine(sample, 48000.0, 110.0f, 0.2f); });
        requireFinite(results[mode]);
        require(rms(results[mode].left, 1024, 46976) > 0.0f, "Modulation mode produced silence");
        if (mode > 0)
        {
            auto changed = false;
            for (int i = 0; i < 48000; ++i)
                if (results[mode].left[i] != results[0].left[i]) { changed = true; break; }
            require(changed, "Modulation did not alter the output waveform");
        }
    }
    std::cout << "modulation_modes: PASS" << std::endl;
}

void runMidiProbe()
{
    SpectralResonatorProcessor processor;
    configureNeutral(processor);
    setChoice(processor, Parameters::pitchMode, 1, 2);
    setChoice(processor, Parameters::monoPolyMode, 1, 2);
    setValue(processor, Parameters::harmonics, 4.0f);
    setValue(processor, Parameters::decayMs, 100.0f);
    const auto result = render(processor, 12288, 64, 48000.0,
        [] (int sample, int) { return sine(sample, 48000.0, 261.6256f, 0.2f) + sine(sample, 48000.0, 391.9954f, 0.2f); },
        [] (juce::MidiBuffer& midi, int start)
        {
            if (start == 0)
            {
                midi.addEvent(juce::MidiMessage::noteOn(1, 60, static_cast<juce::uint8>(110)), 16);
                midi.addEvent(juce::MidiMessage::noteOn(1, 67, static_cast<juce::uint8>(100)), 32);
            }
            if (start == 4096)
            {
                midi.addEvent(juce::MidiMessage::noteOff(1, 60), 0);
                midi.addEvent(juce::MidiMessage::noteOff(1, 67), 0);
            }
        });
    requireFinite(result);
    const auto c4 = energyAt(result.left, 2048, 1024, 48000.0, 261.6256f);
    const auto g4 = energyAt(result.left, 2048, 1024, 48000.0, 391.9954f);
    const auto sustained = rms(result.left, 3072, 512);
    const auto tail = rms(result.left, 10240, 512);
    const auto tailC4 = energyAt(result.left, 10240, 512, 48000.0, 261.6256f);
    const auto tailG4 = energyAt(result.left, 10240, 512, 48000.0, 391.9954f);
    std::cout << "midi debug sustained=" << sustained << " tail=" << tail
              << " tuned=" << c4 + g4 << " tailTuned=" << tailC4 + tailG4 << std::endl;
    require(c4 > 1.0e-6f && g4 > 1.0e-6f, "MIDI notes did not produce tuned resonances");
    require(tailC4 + tailG4 < (c4 + g4) * 0.7f, "MIDI note-off did not release tuned voices");
    require(std::abs(processor.fundamentalForDisplay() - 391.9954f) < 0.1f,
            "Editor fundamental does not follow the active MIDI note");
    std::cout << "midi_polyphony_and_release: PASS" << std::endl;
}

void runGlideProbe()
{
    SpectralResonatorProcessor processor;
    configureNeutral(processor);
    setChoice(processor, Parameters::pitchMode, 1, 2);
    setChoice(processor, Parameters::monoPolyMode, 0, 2);
    setValue(processor, Parameters::glideMs, 100.0f);
    setValue(processor, Parameters::harmonics, 4.0f);
    setValue(processor, Parameters::decayMs, 1000.0f);
    const auto result = render(processor, 8192, 64, 48000.0,
        [] (int sample, int) { return sine(sample, 48000.0, 391.9954f, 0.2f); },
        [] (juce::MidiBuffer& midi, int start)
        {
            if (start == 0) midi.addEvent(juce::MidiMessage::noteOn(1, 60, static_cast<juce::uint8>(100)), 0);
            if (start == 2048) midi.addEvent(juce::MidiMessage::noteOn(1, 67, static_cast<juce::uint8>(100)), 0);
        });
    requireFinite(result);
    require(std::abs(processor.fundamentalForDisplay() - 391.9954f) < 0.1f,
            "Mono glide did not reach the target frequency");
    const auto target = energyAt(result.left, 7168, 1024, 48000.0, 391.9954f);
    std::cout << "mono glide debug target=" << target << std::endl;
    require(target > 1.0e-6f, "Mono glide did not produce the target resonance");
    std::cout << "mono_glide: PASS" << std::endl;
}

void runPolyphonyProbe()
{
    SpectralResonatorProcessor processor;
    configureNeutral(processor);
    setChoice(processor, Parameters::pitchMode, 1, 2);
    setChoice(processor, Parameters::monoPolyMode, 1, 2);
    setValue(processor, Parameters::polyphony, 2.0f);
    setValue(processor, Parameters::harmonics, 4.0f);
    setValue(processor, Parameters::decayMs, 100.0f);
    const auto result = render(processor, 16384, 64, 48000.0,
        [] (int sample, int)
        {
            return sine(sample, 48000.0, 65.4064f, 0.2f)
                + sine(sample, 48000.0, 97.9989f, 0.2f)
                + sine(sample, 48000.0, 523.2511f, 0.2f);
        },
        [] (juce::MidiBuffer& midi, int start)
        {
            if (start == 0)
                for (const auto note : { 36, 43, 72 })
                    midi.addEvent(juce::MidiMessage::noteOn(1, note, static_cast<juce::uint8>(100)), 0);
            if (start == 8192)
                for (const auto note : { 36, 43 })
                    midi.addEvent(juce::MidiMessage::noteOff(1, note), 0);
        });
    requireFinite(result);
    const auto first = energyAt(result.left, 3072, 4096, 48000.0, 65.4064f);
    const auto second = energyAt(result.left, 3072, 4096, 48000.0, 97.9989f);
    const auto queuedEarly = energyAt(result.left, 3072, 4096, 48000.0, 523.2511f);
    const auto queuedLate = energyAt(result.left, 14336, 2048, 48000.0, 523.2511f);
    std::cout << "polyphony debug first=" << first << " second=" << second
              << " queuedEarly=" << queuedEarly << " queuedLate=" << queuedLate << std::endl;
    require(second > 1.0e-4f && queuedEarly > 1.0e-4f, "Poly voice stealing failed to admit incoming note");
    require(queuedLate > 1.0e-4f, "Incoming note failed to remain held");
    require(first < second * 0.2f, "Poly voice stealing failed to replace the oldest held voice");
    std::cout << "polyphony_limit_and_steal: PASS" << std::endl;
}

void runMidiStressProbe()
{
    SpectralResonatorProcessor processor;
    configureNeutral(processor);
    setChoice(processor, Parameters::pitchMode, 1, 2);
    setChoice(processor, Parameters::monoPolyMode, 1, 2);
    setValue(processor, Parameters::pitchBendRange, 12.0f);
    setValue(processor, Parameters::harmonics, 4.0f);
    setValue(processor, Parameters::decayMs, 1000.0f);
    const auto result = render(processor, 8192, 64, 48000.0,
        [] (int sample, int) { return sine(sample, 48000.0, 523.2511f, 0.2f); },
        [] (juce::MidiBuffer& midi, int start)
        {
            if (start != 0) return;
            midi.addEvent(juce::MidiMessage::noteOn(1, 60, static_cast<juce::uint8>(100)), 0);
            for (int index = 0; index < 300; ++index)
                midi.addEvent(juce::MidiMessage::pitchWheel(1, index == 299 ? 16383 : 8192), 0);
        });
    requireFinite(result);
    const auto bent = energyAt(result.left, 4096, 2048, 48000.0, 523.2511f);
    std::cout << "midi stress debug bent=" << bent << std::endl;
    require(bent > 1.0e-5f, "MIDI events beyond the former 256-event cap were dropped");
    std::cout << "midi_event_stress: PASS" << std::endl;
}

void runSampleRateAndBlockProbe()
{
    for (const auto sampleRate : { 44100.0, 48000.0, 96000.0 })
        for (const auto blockSize : { 1, 17, 64, 511, 1024 })
        {
            SpectralResonatorProcessor processor;
            configureNeutral(processor);
            setValue(processor, Parameters::harmonics, 4.0f);
            const auto result = render(processor, 2048, blockSize, sampleRate,
                [sampleRate] (int sample, int) { return sine(sample, sampleRate, 110.0f, 0.25f); });
            requireFinite(result);
            require(rms(result.left, 1536, 256) > 0.0f, "Silent output at a sample-rate/block-size combination");
        }
    std::cout << "sample_rates_and_block_sizes: PASS" << std::endl;
}

void runExtremeAndResetProbe()
{
    SpectralResonatorProcessor processor;
    configureNeutral(processor);
    setValue(processor, Parameters::stretch, 100.0f);
    setValue(processor, Parameters::shift, 7.0f);
    setValue(processor, Parameters::decayMs, 10000.0f);
    setValue(processor, Parameters::highFreqDamp, 100.0f);
    setValue(processor, Parameters::lowFreqDamp, 100.0f);
    setChoice(processor, Parameters::modMode, 3, 4);
    setValue(processor, Parameters::modRate, 100.0f);
    setValue(processor, Parameters::pitchModAmount, 24.0f);
    setValue(processor, Parameters::harmonics, 256.0f);
    setValue(processor, Parameters::quantize, 1.0f);
    setValue(processor, Parameters::inputSendDb, 12.0f);
    setChoice(processor, Parameters::unison, 3, 4);
    setValue(processor, Parameters::unisonAmount, 100.0f);
    const auto result = render(processor, 1536, 64, 48000.0, noise);
    requireFinite(result);
    const auto resetResult = render(processor, 128, 64, 48000.0, [] (int, int) { return 0.0f; });
    requireFinite(resetResult);
    require(rms(resetResult.left, 0, 128) == 0.0f && rms(resetResult.right, 0, 128) == 0.0f,
            "prepare/reset left prior resonator state active");
    std::cout << "parameter_extremes_and_reset: PASS" << std::endl;
}

void runScaleProbe()
{
    const auto scales = Parameters::scaleChoices();
    require(scales.size() == 36, "Scale inventory is incomplete");
    for (int scale = 0; scale < scales.size(); ++scale)
        for (int root = 0; root < 12; ++root)
            for (int note = 0; note <= 127; ++note)
            {
                const auto mapped = Parameters::mapNoteToScale(scale, root, static_cast<float>(note));
                require(std::isfinite(mapped), "Manual scale produced a non-finite note");
                const auto repeated = Parameters::mapNoteToScale(scale, root, mapped);
                require(std::abs(repeated - mapped) < 1.0e-4f, "Manual scale mapping is not idempotent");
            }
    require(std::abs(Parameters::mapNoteToScale(3, 0, 61.0f) - 60.0f) < 1.0e-6f,
            "C Major nearest-note mapping failed");
    require(std::abs(Parameters::mapNoteToScale(3, 0, 60.0f, 1) - 62.0f) < 1.0e-6f,
            "Scale-degree shift failed");

    SpectralResonatorProcessor processor;
    configureNeutral(processor);
    setValue(processor, Parameters::useScale, 1.0f);
    setChoice(processor, Parameters::scale, 3, 36);
    setChoice(processor, Parameters::scaleRoot, 0, 12);
    setValue(processor, Parameters::frequencyNote, 61.0f);
    const auto result = render(processor, 4096, 64, 48000.0,
        [] (int sample, int) { return sine(sample, 48000.0, 261.6256f, 0.25f); });
    requireFinite(result);
    require(std::abs(processor.fundamentalForDisplay() - 261.6256f) < 0.01f,
            "Internal voice did not use the manual scale");
    std::cout << "manual_scales: PASS" << std::endl;
}

void runStateProbe()
{
    SpectralResonatorProcessor processor;
    SpectralEngine::Settings defaultSettings;
    require(defaultSettings.frequencyNote == 45.0f && defaultSettings.frequencyHz == 110.0f,
            "Engine default frequency state is inconsistent");
    require(std::abs(processor.apvts.getRawParameterValue(Parameters::frequencyNote)->load() - 45.0f) < 1.0e-6f,
            "Incorrect default note parameter");
    setValue(processor, Parameters::frequencyHz, 220.0f);
    setValue(processor, Parameters::decayMs, 750.0f);
    setChoice(processor, Parameters::modMode, static_cast<int>(Parameters::ModMode::granular), 4);
    setValue(processor, Parameters::useScale, 1.0f);
    setChoice(processor, Parameters::scale, 3, 36);
    setChoice(processor, Parameters::scaleRoot, 2, 12);
    juce::MemoryBlock state;
    processor.getStateInformation(state);
    SpectralResonatorProcessor restored;
    restored.setStateInformation(state.getData(), static_cast<int>(state.getSize()));
    require(std::abs(restored.apvts.getRawParameterValue(Parameters::frequencyHz)->load() - 220.0f) < 0.02f,
            "Frequency state did not restore");
    require(std::abs(restored.apvts.getRawParameterValue(Parameters::decayMs)->load() - 750.0f) < 0.02f,
            "Decay state did not restore");
    require(static_cast<int>(restored.apvts.getRawParameterValue(Parameters::modMode)->load())
            == static_cast<int>(Parameters::ModMode::granular), "Modulation mode did not restore");
    require(restored.apvts.getRawParameterValue(Parameters::useScale)->load() >= 0.5f
            && static_cast<int>(restored.apvts.getRawParameterValue(Parameters::scale)->load()) == 3
            && static_cast<int>(restored.apvts.getRawParameterValue(Parameters::scaleRoot)->load()) == 2,
            "Manual scale state did not restore");
    std::cout << "state_restore: PASS" << std::endl;
}

void runEditorProbe()
{
    SpectralResonatorProcessor processor;
    std::unique_ptr<juce::AudioProcessorEditor> editor(processor.createEditor());
    require(editor->getWidth() == 880 && editor->getHeight() == 500, "Unexpected editor size");
    setChoice(processor, Parameters::pitchMode, static_cast<int>(Parameters::PitchMode::midi), 2);
    setValue(processor, Parameters::useScale, 1.0f);
    setChoice(processor, Parameters::scale, 3, 36);
    juce::MessageManager::getInstance()->runDispatchLoopUntil(100);
    std::cout << "editor_create_and_mode_update: PASS" << std::endl;
}

void runVst3LatencyProbe()
{
    const auto pluginPath = juce::File::getCurrentWorkingDirectory().getChildFile(
        juce::SystemStats::getEnvironmentVariable("SPECTRAL_TEST_VST3",
            "build/SpectralResonatorRE_artefacts/Release/VST3/Spectral Resonator RE.vst3"));
    juce::VST3PluginFormat format;
    juce::OwnedArray<juce::PluginDescription> descriptions;
    format.findAllTypesForFile(descriptions, pluginPath.getFullPathName());
    require(!descriptions.isEmpty(), "Could not enumerate the built VST3");
    std::unique_ptr<juce::AudioPluginInstance> instance;
    juce::String creationError;
    format.createPluginInstanceAsync(*descriptions[0], 48000.0, 512,
        [&instance, &creationError] (std::unique_ptr<juce::AudioPluginInstance> created, const juce::String& error)
        {
            creationError = error;
            instance = std::move(created);
        });
    for (int attempt = 0; attempt < 100 && instance == nullptr; ++attempt)
        juce::MessageManager::getInstance()->runDispatchLoopUntil(50);
    require(instance != nullptr, creationError.isNotEmpty()
        ? creationError.toStdString().c_str() : "Could not instantiate the built VST3");
    SpectralResonatorProcessor expectedDefaults;
    for (auto* parameter : expectedDefaults.getParameters())
    {
        juce::AudioProcessorParameter* hosted = nullptr;
        for (auto* candidate : instance->getParameters())
            if (candidate->getName(100) == parameter->getName(100))
                { hosted = candidate; break; }
        require(hosted != nullptr, "Hosted default parameter missing");
        require(std::abs(hosted->getValue() - parameter->getDefaultValue()) < 1e-6f
            && std::abs(hosted->getDefaultValue() - parameter->getDefaultValue()) < 1e-6f,
            "Actual VST3 factory value or default metadata mismatch");
    }
    std::cout << "vst3_factory_defaults_28: PASS" << std::endl;
    const auto beforePrepare = instance->getLatencySamples();
    instance->prepareToPlay(48000.0, 512);
    const auto afterPrepare = instance->getLatencySamples();
    std::cout << "vst3 latency debug before=" << beforePrepare << " after=" << afterPrepare << std::endl;
    require(afterPrepare == SpectralEngine::latencySamples,
            "VST3 host sees incorrect latency after prepareToPlay");
    std::cout << "vst3_host_latency: PASS" << std::endl;
}

#include "FactoryDefaults.inc"

int main()
{
    const juce::ScopedJuceInitialiser_GUI initialiseGui;
    try
    {
        runFactoryDefaultsProbe();
        runDryWetProbe();
        runOutputDynamicsProbe();
        runResonanceProbe();
        runDecayProbe();
        runDampingProbe();
        runHarmonicsAndUnisonProbe();
        runModulationProbe();
        runMidiProbe();
        runGlideProbe();
        runPolyphonyProbe();
        runMidiStressProbe();
        runSampleRateAndBlockProbe();
        runExtremeAndResetProbe();
        runScaleProbe();
        runStateProbe();
        runEditorProbe();
        runVst3LatencyProbe();
        return 0;
    }
    catch (const std::exception& error)
    {
        std::cerr << "FAIL: " << error.what() << '\n';
        return 1;
    }
}
