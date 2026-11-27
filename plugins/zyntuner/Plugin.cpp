/*
 * zyntuner - DSP
 * Low CPU tuner with soft mute and microtonal scales, and spectrum analyser
 *
 * Copyright (C) 2026 riban
 * License: GPL-3.0-or-later
 */

#include "DistrhoPlugin.hpp"
#include "ParamDefs.hpp"
#include "PitchDetector.hpp"
#include "SpectrumAnalyser.hpp"

#include <algorithm>
#include <cmath>
#include <cstring>

START_NAMESPACE_DISTRHO

class ZyntunerPlugin : public Plugin
{
public:
    ZyntunerPlugin()
        : Plugin(kParamCount, 0, 0)  // parameters, programs, states
    {
        for (uint32_t i = 0; i < kParamCount; ++i)
            fValues[i] = kParams[i].def;
        sampleRateChanged(getSampleRate());
    }

protected:
    // ----------------------------------------------------------------------------------------------------------------
    // Information

    const char* getLabel() const override { return "zyntuner"; }
    const char* getDescription() const override { return "Low CPU tuner with soft mute and microtonal scales, and spectrum analyser"; }
    const char* getMaker() const override { return "riban"; }
    const char* getHomePage() const override { return "https://zynthian.org"; }
    const char* getLicense() const override { return "GPL-3.0-or-later"; }
    uint32_t getVersion() const override { return d_version(0, 4, 0); }

    // ----------------------------------------------------------------------------------------------------------------
    // Ports, parameters and groups (from ParamDefs.hpp)

    void initPortGroup(const uint32_t groupId, PortGroup& portGroup) override
    {
        if (groupId < kGroupCount)
        {
            portGroup.name = kGroups[groupId].name;
            portGroup.symbol = kGroups[groupId].symbol;
        }
    }

    void initParameter(const uint32_t index, Parameter& parameter) override
    {
        if (index >= kParamCount)
            return;
        const ParamDef& p(kParams[index]);
        if (p.designation != kParameterDesignationNull)
            parameter.initDesignation(p.designation);  // bypass: LV2 port "lv2_enabled", inverted by DPF
        parameter.hints = p.hints;
        parameter.name = p.name;
        parameter.symbol = p.symbol;
        parameter.unit = p.unit;
        parameter.ranges.min = p.min;
        parameter.ranges.max = p.max;
        parameter.ranges.def = p.def;
        parameter.groupId = p.group >= 0 ? static_cast<uint32_t>(p.group) : kPortGroupNone;
        if (p.enumCount > 0)
        {
            ParameterEnumerationValue* const values = new ParameterEnumerationValue[p.enumCount];
            for (uint32_t i = 0; i < p.enumCount; ++i)
            {
                values[i].label = p.enumLabels[i];
                values[i].value = p.min + i;
            }
            parameter.enumValues.count = p.enumCount;
            parameter.enumValues.restrictedMode = true;
            parameter.enumValues.values = values;
        }
    }

    float getParameterValue(const uint32_t index) const override
    {
        return index < kParamCount ? fValues[index] : 0.0f;
    }

    void setParameterValue(const uint32_t index, const float value) override
    {
        if (index < kParamCount && (kParams[index].hints & kParameterIsOutput) == 0)
            fValues[index] = value;
    }

    // ----------------------------------------------------------------------------------------------------------------
    // Processing

    void sampleRateChanged(const double sampleRate) override
    {
        fDetector.setSampleRate(sampleRate);
        fDetector.setThreshold(kTunerThreshold);
        fAnalyser.setSampleRate(sampleRate, kBandCount, kBandMinFreq, kBandMaxFreq, kBandFloor);
        fGainStep = static_cast<float>(1.0 / (kFadeTime * sampleRate));
    }

    void activate() override
    {
        fDetector.reset();
        fAnalyser.reset();
        fLastFreq = -1.0f;
        fActive = -1;
        fActivated = true;  // no fade on activation: the 1st run() applies the host's bypass state at once
        clearOutputs();
        clearBands();
    }

    void run(const float** const inputs, float** const outputs, const uint32_t frames) override
    {
        const bool bypassed = fValues[kParamBypass] > 0.5f;
        const int mode = fValues[kParamMode] > 0.5f ? kModeSpectrum : kModeTuner;
        const float* const inL = inputs[0];
        const float* const inR = inputs[1];
        float* const outL = outputs[0];
        float* const outR = outputs[1];

        // Only the selected analysis runs, and none while bypassed (-1). Switching resets the one that stops.
        const int active = bypassed ? -1 : mode;
        if (active != fActive)
        {
            if (fActive == kModeTuner)
            {
                fDetector.reset();
                fLastFreq = -1.0f;
                clearOutputs();
            }
            else if (fActive == kModeSpectrum)
            {
                fAnalyser.reset();
                clearBands();
            }
            fActive = active;
        }
        if (active == kModeTuner)
            for (uint32_t i = 0; i < frames; ++i)
                fDetector.push(0.5f * (inL[i] + inR[i]));
        else if (active == kModeSpectrum)
            for (uint32_t i = 0; i < frames; ++i)
                fAnalyser.push(0.5f * (inL[i] + inR[i]));

        // Soft mute: the output fades out while the tuner is enabled and back in when it is bypassed.
        // The spectrum analyser lets the audio pass.
        const bool pass = active != kModeTuner;
        const float target = pass ? 1.0f : 0.0f;
        if (fActivated)
        {
            fActivated = false;
            fGain = target;
        }
        if (fGain == target)
        {
            if (pass)
            {
                if (outL != inL) std::memcpy(outL, inL, frames * sizeof(float));
                if (outR != inR) std::memcpy(outR, inR, frames * sizeof(float));
            }
            else
            {
                std::memset(outL, 0, frames * sizeof(float));
                std::memset(outR, 0, frames * sizeof(float));
            }
        }
        else
        {
            const float step = pass ? fGainStep : -fGainStep;
            for (uint32_t i = 0; i < frames; ++i)
            {
                fGain = std::max(0.0f, std::min(1.0f, fGain + step));
                outL[i] = inL[i] * fGain;
                outR[i] = inR[i] * fGain;
            }
        }

        if (active == kModeTuner)
        {
            fDetector.process(frames);
            updateOutputs();
        }
        else if (active == kModeSpectrum)
        {
            fAnalyser.process(frames);
            if (fAnalyser.takeUpdate())
                for (uint32_t b = 0; b < kBandCount; ++b)
                    fValues[kParamBand0 + b] = fAnalyser.level(b);
        }
    }

private:
    void clearOutputs()
    {
        fValues[kParamNote] = fValues[kParamCents] = fValues[kParamValid] = 0.0f;
    }

    void clearBands()
    {
        for (uint32_t b = 0; b < kBandCount; ++b)
            fValues[kParamBand0 + b] = kBandFloor;
    }

    void updateOutputs()
    {
        const float freq = fDetector.frequency(), ref = fValues[kParamReference];
        const int scaleIdx = std::max(0, std::min(static_cast<int>(kScaleCount) - 1,
                                                  static_cast<int>(fValues[kParamScale] + 0.5f)));
        if (freq == fLastFreq && ref == fLastRef && scaleIdx == fLastScale)
            return;
        fLastFreq = freq;
        fLastRef = ref;
        fLastScale = scaleIdx;
        if (freq <= 0.0f)
        {
            fValues[kParamValid] = 0.0f;  // the other outputs keep their last values
            return;
        }

        // Pitch in cents above the scale's root in octave 4 (period 4 for non-octave scales)
        const ScaleDef& scale(kScales[scaleIdx]);
        const double x = 1200.0 * std::log2(freq / ref) + scale.anchor;
        double period = std::floor(x / scale.period);
        const double r = x - period * scale.period;  // 0 .. period

        // Nearest degree; the root of the next period is a candidate too (cents are ascending)
        uint32_t degree = 0;
        double target = 0.0;
        for (uint32_t d = 0; d < scale.count; ++d)
        {
            if (std::fabs(r - scale.cents[d]) < std::fabs(r - target))
            {
                degree = d;
                target = scale.cents[d];
            }
        }
        double cents = r - target;
        if (scale.period - r < std::fabs(cents))
        {
            degree = 0;
            target = 0.0;
            cents = r - scale.period;
            period += 1.0;
        }

        // Nearest MIDI note (12-TET at the reference) of the target pitch, for compatibility
        const double targetCents = period * scale.period + target - scale.anchor;  // above A4
        const float note = static_cast<float>(std::floor(69.0 + targetCents / 100.0 + 0.5));

        fValues[kParamNote] = std::max(0.0f, std::min(127.0f, note));
        fValues[kParamCents] = static_cast<float>(cents);
        fValues[kParamDegree] = static_cast<float>(degree);
        fValues[kParamOctave] = static_cast<float>(period + 4.0);
        fValues[kParamValid] = 1.0f;
    }

    static constexpr double kFadeTime = 0.01;  // soft mute fade, seconds

    float fValues[kParamCount];
    PitchDetector fDetector;
    SpectrumAnalyser fAnalyser;
    float fLastFreq = -1.0f, fLastRef = 0.0f;
    int fLastScale = -1;
    int fActive = -1;  // analysis running: kModeTuner, kModeSpectrum or -1 (bypassed)
    float fGain = 0.0f, fGainStep = 0.001f;
    bool fActivated = false;

    DISTRHO_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(ZyntunerPlugin)
};

Plugin* createPlugin()
{
    return new ZyntunerPlugin();
}

END_NAMESPACE_DISTRHO
