// SPDX-FileCopyrightText: 2026 TriM
// SPDX-License-Identifier: AGPL-3.0-only

void runFactoryDefaultsProbe()
{
    const std::pair<const char*,float> expected[] = {
        {"pitchMode",0},{"monoPolyMode",0},{"polyphony",8},{"midiGate",0},
        {"glideMs",0},{"pitchBendRange",2},{"frequencyHz",110},{"frequencyNote",45},
        {"frequencyDialMode",0},{"useScale",0},{"scale",0},{"scaleRoot",0},
        {"transpose",0},{"transposeScaleDegrees",0},{"stretch",0},{"shift",0},
        {"decayMs",222},{"highFreqDamp",0},{"lowFreqDamp",0},{"modMode",0},
        {"modRate",10},{"pitchModAmount",0},{"harmonics",256},{"quantize",0},
        {"inputSendDb",0},{"unison",0},{"unisonAmount",10},{"dryWet",1}
    };
    SpectralResonatorProcessor processor;
    require(processor.getParameters().size()==28, "Factory fixture must cover all parameters");
    for(const auto& item:expected)
    {
        const auto* parameter=processor.apvts.getParameter(item.first);
        require(parameter!=nullptr, "Factory parameter missing");
        const auto raw=processor.apvts.getRawParameterValue(item.first)->load();
        require(std::abs(raw-item.second)<=std::max(1e-5f,std::abs(item.second)*1e-6f), "Factory raw value mismatch");
        require(std::abs(parameter->getDefaultValue()-parameter->convertTo0to1(item.second))<1e-6f, "Factory default metadata mismatch");
    }
    std::cout << "factory_defaults_28: PASS" << std::endl;
}
