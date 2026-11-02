// SPDX-FileCopyrightText: 2026 TriM
// SPDX-License-Identifier: AGPL-3.0-only

#include "PluginProcessor.h"
#include "PluginEditor.h"
#include <utility>

SpectralResonatorProcessor::SpectralResonatorProcessor()
    : AudioProcessor(BusesProperties()
          .withInput("Input", juce::AudioChannelSet::stereo(), true)
          .withOutput("Output", juce::AudioChannelSet::stereo(), true)),
      apvts(*this, nullptr, "PARAMETERS", Parameters::createLayout())
{
    parameterValues.pitchMode = apvts.getRawParameterValue(Parameters::pitchMode);
    parameterValues.monoPolyMode = apvts.getRawParameterValue(Parameters::monoPolyMode);
    parameterValues.polyphony = apvts.getRawParameterValue(Parameters::polyphony);
    parameterValues.midiGate = apvts.getRawParameterValue(Parameters::midiGate);
    parameterValues.glideMs = apvts.getRawParameterValue(Parameters::glideMs);
    parameterValues.pitchBendRange = apvts.getRawParameterValue(Parameters::pitchBendRange);
    parameterValues.frequencyHz = apvts.getRawParameterValue(Parameters::frequencyHz);
    parameterValues.frequencyNote = apvts.getRawParameterValue(Parameters::frequencyNote);
    parameterValues.frequencyDialMode = apvts.getRawParameterValue(Parameters::frequencyDialMode);
    parameterValues.useScale = apvts.getRawParameterValue(Parameters::useScale);
    parameterValues.scale = apvts.getRawParameterValue(Parameters::scale);
    parameterValues.scaleRoot = apvts.getRawParameterValue(Parameters::scaleRoot);
    parameterValues.transpose = apvts.getRawParameterValue(Parameters::transpose);
    parameterValues.transposeScaleDegrees = apvts.getRawParameterValue(Parameters::transposeScaleDegrees);
    parameterValues.stretch = apvts.getRawParameterValue(Parameters::stretch);
    parameterValues.shift = apvts.getRawParameterValue(Parameters::shift);
    parameterValues.decayMs = apvts.getRawParameterValue(Parameters::decayMs);
    parameterValues.highFreqDamp = apvts.getRawParameterValue(Parameters::highFreqDamp);
    parameterValues.lowFreqDamp = apvts.getRawParameterValue(Parameters::lowFreqDamp);
    parameterValues.modMode = apvts.getRawParameterValue(Parameters::modMode);
    parameterValues.modRate = apvts.getRawParameterValue(Parameters::modRate);
    parameterValues.pitchModAmount = apvts.getRawParameterValue(Parameters::pitchModAmount);
    parameterValues.harmonics = apvts.getRawParameterValue(Parameters::harmonics);
    parameterValues.quantize = apvts.getRawParameterValue(Parameters::quantize);
    parameterValues.inputSendDb = apvts.getRawParameterValue(Parameters::inputSendDb);
    parameterValues.unison = apvts.getRawParameterValue(Parameters::unison);
    parameterValues.unisonAmount = apvts.getRawParameterValue(Parameters::unisonAmount);
    parameterValues.dryWet = apvts.getRawParameterValue(Parameters::dryWet);
    automationCursors.resize(static_cast<size_t>(getParameters().size()) + 1);
    automationHistory.resize(automationCursors.size());
    integerParameters = {{
        {apvts.getParameter(Parameters::frequencyNote), SpectralEngine::frequencyNoteCallback},
        {apvts.getParameter(Parameters::transpose), SpectralEngine::transposeCallback},
        {apvts.getParameter(Parameters::transposeScaleDegrees), SpectralEngine::scaleDegreeCallback}
    }};
    setLatencySamples(SpectralEngine::latencySamples);
}

void SpectralResonatorProcessor::prepareToPlay(double sampleRate, int samplesPerBlock)
{
    automationGeneration.fetch_add(1, std::memory_order_relaxed);
    setLatencySamples(SpectralEngine::latencySamples);
    engine.prepare(sampleRate, samplesPerBlock, currentSettings());
}

void SpectralResonatorProcessor::releaseResources() { engine.suspend(); }

void SpectralResonatorProcessor::reset()
{
    refreshAutomationGeneration();
    engine.setSettings(currentSettings(), std::exchange(pendingIntegerCallbacks, 0u));
    engine.reset();
}

bool SpectralResonatorProcessor::isBusesLayoutSupported(const BusesLayout& layouts) const
{
    return layouts.getMainInputChannelSet() == juce::AudioChannelSet::stereo()
        && layouts.getMainOutputChannelSet() == juce::AudioChannelSet::stereo();
}

SpectralEngine::Settings SpectralResonatorProcessor::currentSettings()
{
    SpectralEngine::Settings result;
    result.pitchMode = static_cast<int>(parameterValues.pitchMode->load());
    result.monoPolyMode = static_cast<int>(parameterValues.monoPolyMode->load());
    result.polyphony = static_cast<int>(parameterValues.polyphony->load());
    result.midiGate = parameterValues.midiGate->load() >= 0.5f;
    result.glideMs = parameterValues.glideMs->load();
    result.pitchBendRange = static_cast<int>(parameterValues.pitchBendRange->load());
    result.frequencyHz = parameterValues.frequencyHz->load();
    result.frequencyNote = parameterValues.frequencyNote->load();
    result.frequencyDialMode = static_cast<int>(parameterValues.frequencyDialMode->load());
    result.useScale = parameterValues.useScale->load() >= 0.5f;
    result.scale = static_cast<int>(parameterValues.scale->load());
    result.scaleRoot = static_cast<int>(parameterValues.scaleRoot->load());
    result.transpose = parameterValues.transpose->load();
    result.transposeScaleDegrees = static_cast<int>(parameterValues.transposeScaleDegrees->load());
    result.stretch = parameterValues.stretch->load();
    result.shift = parameterValues.shift->load();
    result.decayMs = parameterValues.decayMs->load();
    result.highFreqDamp = parameterValues.highFreqDamp->load();
    result.lowFreqDamp = parameterValues.lowFreqDamp->load();
    result.modMode = static_cast<int>(parameterValues.modMode->load());
    result.modRate = parameterValues.modRate->load();
    result.pitchModAmount = parameterValues.pitchModAmount->load();
    result.harmonics = juce::roundToInt(parameterValues.harmonics->load());
    result.quantize = parameterValues.quantize->load() >= 0.5f;
    result.inputSendDb = parameterValues.inputSendDb->load();
    result.unisonVoices = Parameters::unisonVoices(static_cast<int>(parameterValues.unison->load()));
    result.unisonAmount = parameterValues.unisonAmount->load();
    result.dryWet = parameterValues.dryWet->load();
    return result;
}

void SpectralResonatorProcessor::processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
{
    juce::ScopedNoDenormals noDenormals;
    refreshAutomationGeneration();
    for (auto channel = getTotalNumInputChannels(); channel < getTotalNumOutputChannels(); ++channel)
        buffer.clear(channel, 0, buffer.getNumSamples());
    if (getTotalNumInputChannels() != 2 || getTotalNumOutputChannels() != 2)
        return;

    beginHostBlock(buffer, midi);
    processAudioRange(buffer, midi, 0, buffer.getNumSamples(), false);
}

void SpectralResonatorProcessor::processBlockBypassed(juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
{
    juce::ScopedNoDenormals noDenormals;
    refreshAutomationGeneration();
    for (auto channel = getTotalNumInputChannels(); channel < getTotalNumOutputChannels(); ++channel)
        buffer.clear(channel, 0, buffer.getNumSamples());
    if (getTotalNumInputChannels() != 2 || getTotalNumOutputChannels() != 2)
        return;

    beginHostBlock(buffer, midi);
    processAudioRange(buffer, midi, 0, buffer.getNumSamples(), true);
}

void SpectralResonatorProcessor::beginHostBlock(const juce::AudioBuffer<float>& buffer, const juce::MidiBuffer& midi)
{
    auto inputActive = false;
    for (int channel = 0; channel < 2 && !inputActive; ++channel)
    {
        const auto* input = buffer.getReadPointer(channel);
        for (int sample = 0; sample < buffer.getNumSamples(); ++sample)
            if (input[sample] != 0.0f && std::isfinite(input[sample])) { inputActive = true; break; }
    }
    hostBlockActive = engine.beginHostBlock(inputActive, !midi.isEmpty(), buffer.getNumSamples());
}

void SpectralResonatorProcessor::processAudioRange(juce::AudioBuffer<float>& buffer,
                                                   const juce::MidiBuffer& midi,
                                                   int start, int count, bool bypass,
                                                   juce::MidiBufferIterator* midiCursor)
{
    engine.setSettings(currentSettings(), std::exchange(pendingIntegerCallbacks, 0u));
    if (count <= 0) return;
    auto* left = buffer.getWritePointer(0) + start;
    auto* right = buffer.getWritePointer(1) + start;
    if (!hostBlockActive)
    {
        engine.processInactive(left, right, count);
        return;
    }
    engine.process(left, right, left, right, count, midi, bypass, start, midiCursor);
}

void SpectralResonatorProcessor::processBlockWithAutomation(juce::AudioBuffer<float>& buffer,
                                                            juce::MidiBuffer& midi,
                                                            SpectralAutomationSource& source)
{
    juce::ScopedNoDenormals noDenormals;
    for (auto channel = getTotalNumInputChannels(); channel < getTotalNumOutputChannels(); ++channel)
        buffer.clear(channel, 0, buffer.getNumSamples());
    if (getTotalNumInputChannels() != 2 || getTotalNumOutputChannels() != 2)
        return;

    const auto length = buffer.getNumSamples();
    refreshAutomationGeneration();
    beginHostBlock(buffer, midi);
    auto midiCursor = midi.cbegin();
    size_t cursorCount = 0;
    const auto readNext = [&source, length] (AutomationCursor& cursor)
    {
        cursor.hasNext = false;
        while (cursor.point < cursor.count)
        {
            int offset = 0;
            double value = 0;
            if (source.getPoint(cursor.queue, cursor.point++, offset, value) && std::isfinite(value))
            {
                cursor.nextOffset = juce::jlimit(0, length, offset);
                cursor.nextValue = juce::jlimit(0.0, 1.0, value);
                cursor.hasNext = true;
                return;
            }
        }
    };
    for (int queue = 0; queue < source.queueCount(); ++queue)
    {
        const auto count = source.pointCount(queue);
        if (count <= 0) continue;
        // VST3 defines at most one queue for each parameter ID. Unknown/MIDI
        // queues have no ordinary points in the wrapper view. Excess duplicate
        // queues violate that contract; they cannot grow realtime storage.
        if (cursorCount == automationCursors.size()) break;
        auto& cursor = automationCursors[cursorCount++];
        cursor = {};
        cursor.queue = queue;
        cursor.count = count;
        cursor.discrete = source.isDiscrete(queue);
        const auto initial = source.currentNormalized(queue);
        cursor.previousValue = std::isfinite(initial) ? juce::jlimit(0.0, 1.0, initial) : 0;
        cursor.history = source.parameterIndex(queue);
        if (cursor.history >= 0 && cursor.history < static_cast<int>(automationHistory.size()))
        {
            const auto& history = automationHistory[static_cast<size_t>(cursor.history)];
            initializeIntegerStream(cursor.history, initial, !history.valid || history.published != initial);
            // Keep host normalized values separate from snapped APVTS raw
            // values. A GUI/external change invalidates the old curve origin.
            if (history.valid && history.published == initial && !cursor.discrete)
                cursor.previousValue = history.normalized;
        }
        readNext(cursor);
    }
    auto position = 0;
    const auto publish = [this, &source] (const AutomationCursor& cursor, double value)
    {
        source.applyPoint(cursor.queue, value);
        if (cursor.history >= 0 && cursor.history < static_cast<int>(automationHistory.size()))
        {
            recordIntegerStream(cursor.history, value);
            auto& history = automationHistory[static_cast<size_t>(cursor.history)];
            history.normalized = value;
            history.published = source.currentNormalized(cursor.queue);
            history.valid = true;
        }
    };
    do
    {
        auto next = length;
        for (size_t index = 0; index < cursorCount; ++index)
        {
            auto& cursor = automationCursors[index];
            auto changed = false;
            while (cursor.hasNext && cursor.nextOffset <= position)
            {
                cursor.previousOffset = cursor.nextOffset;
                cursor.previousValue = cursor.nextValue;
                changed = true;
                readNext(cursor);
            }
            if (!cursor.discrete && cursor.hasNext)
            {
                // VST3's implicit preceding point is (-1, last value). Curves
                // interpolate in normalized space before parameter mapping.
                const auto fraction = static_cast<double>(position - cursor.previousOffset)
                    / static_cast<double>(cursor.nextOffset - cursor.previousOffset);
                const auto value = std::fma(fraction, cursor.nextValue - cursor.previousValue,
                                             cursor.previousValue);
                publish(cursor, value);
                if (cursor.nextValue != cursor.previousValue) next = juce::jmin(next, position + 1);
            }
            else if (changed)
                publish(cursor, cursor.previousValue);
            if (cursor.hasNext)
            {
                next = juce::jmin(next, cursor.nextOffset);
            }
        }
        // All values at a boundary form one tuple before native callbacks run.
        // The engine applies its verified Unison(oldMode,newU) -> Mode order.
        processAudioRange(buffer, midi, position, next - position, source.isBypassed(), &midiCursor);
        if (position == length) break;
        position = next;
    } while (true);
}

void SpectralResonatorProcessor::refreshAutomationGeneration()
{
    const auto generation = automationGeneration.load(std::memory_order_acquire);
    if (generation == audioAutomationGeneration) return;
    for (auto& history : automationHistory)
    {
        history.valid = false;
        history.integerStreamValid = false;
    }
    pendingIntegerCallbacks = 0;
    audioAutomationGeneration = generation;
}

void SpectralResonatorProcessor::initializeIntegerStream(int index, double normalized, bool externalChange)
{
    for (const auto& binding : integerParameters)
        if (index == binding.parameter->getParameterIndex())
        {
            auto& history = automationHistory[static_cast<size_t>(index)];
            if (!history.integerStreamValid || externalChange)
            {
                history.integerStream = binding.parameter->convertFrom0to1(static_cast<float>(normalized));
                history.integerStreamValid = true;
            }
            return;
        }
}

void SpectralResonatorProcessor::recordIntegerStream(int index, double normalized)
{
    for (const auto& binding : integerParameters)
        if (index == binding.parameter->getParameterIndex())
        {
            auto& history = automationHistory[static_cast<size_t>(index)];
            // Range conversion deliberately skips snapToLegalValue here. The
            // native tick caches the raw float, then rounds only the callback
            // argument. Comparing snapped APVTS values loses repeated setters.
            const auto raw = binding.parameter->getNormalisableRange().convertFrom0to1(static_cast<float>(normalized));
            if (!history.integerStreamValid || raw != history.integerStream)
                pendingIntegerCallbacks |= binding.callback;
            history.integerStream = raw;
            history.integerStreamValid = true;
            return;
        }
}

void SpectralResonatorProcessor::synchronizeAutomationFlush(SpectralAutomationSource& source)
{
    // Called after the wrapper's ordinary no-audio/suspended parameter flush.
    // Preserve its last normalized point even when parameter mapping snaps it.
    refreshAutomationGeneration();
    for (int queue = 0; queue < source.queueCount(); ++queue)
    {
        const auto index = source.parameterIndex(queue);
        if (index < 0 || index >= static_cast<int>(automationHistory.size())) continue;
        for (auto point = source.pointCount(queue); point > 0; --point)
        {
            int offset = 0;
            double value = 0;
            if (source.getPoint(queue, point - 1, offset, value) && std::isfinite(value))
            {
                const auto normalized = juce::jlimit(0.0, 1.0, value);
                auto& history = automationHistory[static_cast<size_t>(index)];
                const auto current = source.currentNormalized(queue);
                initializeIntegerStream(index, current, !history.valid);
                recordIntegerStream(index, normalized);
                history.normalized = normalized;
                history.published = current;
                history.valid = true;
                break;
            }
        }
    }
}

juce::AudioProcessorEditor* SpectralResonatorProcessor::createEditor()
{
    return new SpectralResonatorEditor(*this);
}

void SpectralResonatorProcessor::getStateInformation(juce::MemoryBlock& destData)
{
    if (const auto xml = apvts.copyState().createXml())
        copyXmlToBinary(*xml, destData);
}

void SpectralResonatorProcessor::setStateInformation(const void* data, int sizeInBytes)
{
    if (const auto xml = getXmlFromBinary(data, sizeInBytes))
        if (xml->hasTagName(apvts.state.getType()))
        {
            apvts.replaceState(juce::ValueTree::fromXml(*xml));
            automationGeneration.fetch_add(1, std::memory_order_release);
        }
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new SpectralResonatorProcessor();
}
