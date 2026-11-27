/*
 * zyntuner - parameters and port groups, shared by DSP (Plugin.cpp) and GUI (UI.cpp).
 * Symbols are stable API (presets, snapshots, MIDI learn, zynthian widget): don't rename them lightly.
 * Control pages in zynthian = port groups (4 controls per page).
 */

#pragma once

#include "DistrhoDetails.hpp"
#include "Scales.hpp"

START_NAMESPACE_DISTRHO

enum ParamId : uint32_t {
    kParamReference = 0,
    kParamMode = 1,
    kParamBypass = 2,
    kParamScale = 3,
    kParamNote = 4,  // output
    kParamCents = 5,  // output
    kParamValid = 6,  // output
    kParamDegree = 7,  // output
    kParamOctave = 8,  // output
    kParamBand0 = 9,  // outputs: kBandCount spectrum bands, band_00 .. band_31
    kParamCount = kParamBand0 + 32
};

// Spectrum analyser bands: kBandCount log spaced bands from kBandMinFreq to kBandMaxFreq
static constexpr uint32_t kBandCount = kParamCount - kParamBand0;
static constexpr float kBandMinFreq = 20.0f, kBandMaxFreq = 20000.0f;
static constexpr float kBandFloor = -80.0f;  // dB, also the value of the bands while not analysing

// Fixed level below which the tuner ignores the input (was the "threshold" control until 0.3)
static constexpr float kTunerThreshold = -60.0f;  // dB

enum Mode : uint32_t {
    kModeTuner = 0,
    kModeSpectrum = 1,
    kModeCount
};

static const char* const kModeLabels[kModeCount] = { "Tuner", "Spectrum" };

enum GroupId : uint32_t {
    kGroupTuner = 0,
    kGroupCount
};

struct GroupDef {
    const char* symbol;
    const char* name;
};

struct ParamDef {
    const char* symbol;
    const char* name;
    const char* unit;
    float min, max, def;
    uint32_t hints;
    int32_t group;  // GroupId or -1
    const char* const* enumLabels;
    uint32_t enumCount;
    ParameterDesignation designation;  // kParameterDesignationBypass: 1 = bypassed (LV2 port lv2_enabled)
};


static const GroupDef kGroups[kGroupCount > 0 ? static_cast<uint32_t>(kGroupCount) : 1u] = {
    { "tuner", "Tuner" },
};

static const ParamDef kParams[kParamCount] = {
    { "reference", "A4 reference", "Hz", 415.0f, 466.0f, 440.0f, kParameterIsAutomatable, 0, nullptr, 0, kParameterDesignationNull },
    { "mode", "Mode", "", 0.0f, kModeCount - 1.0f, 0.0f, kParameterIsAutomatable | kParameterIsInteger, 0, kModeLabels, kModeCount, kParameterDesignationNull },
    { "bypass", "Bypass", "", 0.0f, 1.0f, 1.0f, kParameterIsAutomatable | kParameterIsBoolean, 0, nullptr, 0, kParameterDesignationBypass },
    { "scale", "Scale", "", 0.0f, kScaleCount - 1.0f, 0.0f, kParameterIsAutomatable | kParameterIsInteger, 0, kScaleLabels, kScaleCount, kParameterDesignationNull },
    { "note", "MIDI note", "", 0.0f, 127.0f, 0.0f, kParameterIsAutomatable | kParameterIsInteger | kParameterIsOutput, -1, nullptr, 0, kParameterDesignationNull },
    { "cents", "Cents", "", -150.0f, 150.0f, 0.0f, kParameterIsAutomatable | kParameterIsOutput, -1, nullptr, 0, kParameterDesignationNull },
    { "valid", "Pitch valid", "", 0.0f, 1.0f, 0.0f, kParameterIsAutomatable | kParameterIsBoolean | kParameterIsOutput, -1, nullptr, 0, kParameterDesignationNull },
    { "degree", "Scale degree", "", 0.0f, 63.0f, 0.0f, kParameterIsAutomatable | kParameterIsInteger | kParameterIsOutput, -1, nullptr, 0, kParameterDesignationNull },
    { "octave", "Octave", "", -1.0f, 10.0f, 4.0f, kParameterIsAutomatable | kParameterIsInteger | kParameterIsOutput, -1, nullptr, 0, kParameterDesignationNull },
#define ZYNTUNER_BAND(symbol, name) \
    { symbol, name, "dB", kBandFloor, 0.0f, kBandFloor, kParameterIsAutomatable | kParameterIsOutput, -1, nullptr, 0, kParameterDesignationNull }
    ZYNTUNER_BAND("band_00", "22 Hz"),
    ZYNTUNER_BAND("band_01", "28 Hz"),
    ZYNTUNER_BAND("band_02", "34 Hz"),
    ZYNTUNER_BAND("band_03", "43 Hz"),
    ZYNTUNER_BAND("band_04", "53 Hz"),
    ZYNTUNER_BAND("band_05", "66 Hz"),
    ZYNTUNER_BAND("band_06", "81 Hz"),
    ZYNTUNER_BAND("band_07", "101 Hz"),
    ZYNTUNER_BAND("band_08", "125 Hz"),
    ZYNTUNER_BAND("band_09", "155 Hz"),
    ZYNTUNER_BAND("band_10", "193 Hz"),
    ZYNTUNER_BAND("band_11", "239 Hz"),
    ZYNTUNER_BAND("band_12", "297 Hz"),
    ZYNTUNER_BAND("band_13", "369 Hz"),
    ZYNTUNER_BAND("band_14", "458 Hz"),
    ZYNTUNER_BAND("band_15", "568 Hz"),
    ZYNTUNER_BAND("band_16", "705 Hz"),
    ZYNTUNER_BAND("band_17", "874 Hz"),
    ZYNTUNER_BAND("band_18", "1.1 kHz"),
    ZYNTUNER_BAND("band_19", "1.3 kHz"),
    ZYNTUNER_BAND("band_20", "1.7 kHz"),
    ZYNTUNER_BAND("band_21", "2.1 kHz"),
    ZYNTUNER_BAND("band_22", "2.6 kHz"),
    ZYNTUNER_BAND("band_23", "3.2 kHz"),
    ZYNTUNER_BAND("band_24", "4.0 kHz"),
    ZYNTUNER_BAND("band_25", "4.9 kHz"),
    ZYNTUNER_BAND("band_26", "6.1 kHz"),
    ZYNTUNER_BAND("band_27", "7.6 kHz"),
    ZYNTUNER_BAND("band_28", "9.4 kHz"),
    ZYNTUNER_BAND("band_29", "11.7 kHz"),
    ZYNTUNER_BAND("band_30", "14.5 kHz"),
    ZYNTUNER_BAND("band_31", "18.0 kHz"),
#undef ZYNTUNER_BAND
};

// Bypass parameter: the audio is muted (soft) while the tuner is enabled and passes while bypassed
static const uint32_t kBypassParam = kParamBypass;

// Outputs filled by the demo meters in Plugin.cpp (peak dB of the audio outputs)
static const uint32_t kMeterCount = 0;
static const uint32_t kMeterParams[kMeterCount > 0 ? kMeterCount : 1] = { 0 };

END_NAMESPACE_DISTRHO
