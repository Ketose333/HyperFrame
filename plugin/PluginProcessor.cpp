#include "PluginEditor.h"
#include "PluginProcessor.h"

#include "dsp/Bytebeat.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <cctype>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <deque>
#include <map>
#include <memory>
#include <numeric>
#include <optional>
#include <set>
#include <string>
#include <vector>

extern "C" {
#include <lsdj/instrument.h>
#include <lsdj/project.h>
#include <lsdj/sav.h>
#include <lsdj/synth.h>
#include <lsdj/table.h>
#include <lsdj/wave.h>
}

namespace {
constexpr auto kParamWaveLength = "wave_length";
constexpr auto kParamWaveBits = "wave_bits";
constexpr auto kParamRawPlayFull = "raw_play_full";
constexpr auto kParamInterpolation = "interpolation";
constexpr auto kParamAttack = "attack";
constexpr auto kParamDecay = "decay";
constexpr auto kParamSustain = "sustain";
constexpr auto kParamRelease = "release";
constexpr auto kParamLsdjPhase = "lsdj_phase";
constexpr auto kParamLsdjPhaseMode = "lsdj_phase_mode";
constexpr auto kParamSlideTime = "slide_time";
constexpr auto kParamSelectedFrame = "selected_frame";
constexpr auto kParamMotionTable = "motion_table";
constexpr auto kParamMotionLoop = "motion_loop";
constexpr auto kParamMotionClockMode = "motion_clock_mode";
constexpr auto kParamMotionRate = "motion_rate";
constexpr auto kParamMotionSteps = "motion_steps";
constexpr auto kParamMotionLoopStart = "motion_loop_start";
constexpr auto kParamEngineMode = "engine_mode";
constexpr auto kParamMonoMode = "mono_mode";
constexpr auto kParamGain = "gain";
constexpr auto kWaveTableStateType = "WAVE_TABLE";
constexpr auto kWaveFramesStateType = "WAVE_FRAMES";
constexpr auto kRawStreamStateType = "RAW_STREAM";
constexpr auto kRawStreamDataProperty = "data";
constexpr auto kRawStreamRateProperty = "sample_rate";
constexpr auto kRawStreamSourceRootNoteProperty = "source_root_note";
constexpr auto kRawStreamRootNoteProperty = "root_note";
constexpr auto kRawStreamLoopEnabledProperty = "loop_enabled";
constexpr auto kRawStreamLoopStartProperty = "loop_start";
constexpr auto kRawStreamLoopEndProperty = "loop_end";
constexpr auto kRawStreamPlayStartProperty = "play_start";
constexpr auto kRawStreamHoldInterpolationProperty = "hold_interpolation";
constexpr auto kWaveFrameFormatProperty = "format";
constexpr auto kWaveFrameLoopEnabledProperty = "loop_enabled";
constexpr auto kWaveFrameLoopStartProperty = "loop_start";
constexpr auto kWaveFrameLoopEndProperty = "loop_end";
constexpr auto kCurrentProgramProperty = "current_program";
constexpr auto kSelectedMotionStepProperty = "selected_motion_step";
constexpr float kEnvelopeTimeStepSeconds = 0.001f;
constexpr float kLegacyEnvelopeTickSeconds = 1.0f / 64.0f;
constexpr float kGbAmplitudeStep = 1.0f / 15.0f;
constexpr float kLsdjWaveVolumeStep = 1.0f / 3.0f;
constexpr double kPitchReferenceMidiNote = 60.0;
constexpr int kAudioReadChunkSize = 262144;

float getParamValue(const juce::AudioProcessorValueTreeState& parameters, const char* id) {
    return parameters.getRawParameterValue(id)->load();
}

float getParamValue(const juce::AudioProcessorValueTreeState& parameters, const juce::String& id) {
    return parameters.getRawParameterValue(id)->load();
}

double midiNoteForFrequency(double frequencyHz) {
    if (frequencyHz <= 0.0) {
        return kPitchReferenceMidiNote;
    }

    return 69.0 + 12.0 * std::log2(frequencyHz / 440.0);
}

int playableRootKeyForSourceNote(double sourceRootNote) {
    constexpr int kPlayableRootMin = 48;
    constexpr int kPlayableRootMax = 72;
    constexpr int kPlayableRootTarget = 60;

    auto foldedRoot = static_cast<int>(std::round(sourceRootNote));
    while (foldedRoot < kPlayableRootMin) {
        foldedRoot += 12;
    }
    while (foldedRoot > kPlayableRootMax) {
        foldedRoot -= 12;
    }

    auto bestKey = foldedRoot;
    auto bestDistance = std::abs(bestKey - kPlayableRootTarget);
    for (const auto candidate : { foldedRoot - 12, foldedRoot, foldedRoot + 12 }) {
        if (candidate < kPlayableRootMin || candidate > kPlayableRootMax) {
            continue;
        }

        const auto distance = std::abs(candidate - kPlayableRootTarget);
        if (distance < bestDistance) {
            bestDistance = distance;
            bestKey = candidate;
        }
    }

    return bestKey;
}

juce::Identifier wavePointProperty(std::size_t index) {
    return juce::Identifier("p" + juce::String(static_cast<int>(index)));
}

juce::Identifier frameChildType(int index) {
    return juce::Identifier("FRAME" + juce::String(index));
}

juce::String motionParamId(int step, const char* field) {
    return "motion_step_" + juce::String(step).paddedLeft('0', 2) + "_" + field;
}

juce::String legacyCommandParamId(int step, const char* field) {
    return "command_step_" + juce::String(step).paddedLeft('0', 2) + "_" + field;
}

void migrateLegacyProperty(juce::ValueTree& state, const juce::Identifier& legacyId, const juce::Identifier& motionId) {
    if (!state.hasProperty(motionId) && state.hasProperty(legacyId)) {
        state.setProperty(motionId, state.getProperty(legacyId), nullptr);
    }

    state.removeProperty(legacyId, nullptr);
}

void migrateLegacyMotionState(juce::ValueTree& state) {
    migrateLegacyProperty(state, "command_table", kParamMotionTable);
    migrateLegacyProperty(state, "command_loop", kParamMotionLoop);
    migrateLegacyProperty(state, "command_clock_mode", kParamMotionClockMode);
    migrateLegacyProperty(state, "command_rate", kParamMotionRate);
    migrateLegacyProperty(state, "command_steps", kParamMotionSteps);
    migrateLegacyProperty(state, "selected_command_step", kSelectedMotionStepProperty);

    for (int step = 0; step < hyperframe::dsp::kCommandStepCount; ++step) {
        for (const auto* field : { "frame", "pitch", "phase", "level" }) {
            migrateLegacyProperty(state, legacyCommandParamId(step, field), motionParamId(step, field));
        }
    }
}

int roundedInt(float value) {
    return static_cast<int>(std::round(value));
}

juce::String envelopeTimeText(float seconds, int maximumStringLength) {
    const auto clampedSeconds = std::max(0.0f, seconds);
    auto text = clampedSeconds < 1.0f
        ? juce::String(roundedInt(clampedSeconds * 1000.0f)) + "ms"
        : juce::String(clampedSeconds, clampedSeconds < 10.0f ? 2 : 1) + "s";
    return maximumStringLength > 0 ? text.substring(0, maximumStringLength) : text;
}

float envelopeTimeValue(const juce::String& text) {
    auto trimmed = text.trim().toLowerCase();
    if (trimmed.endsWithChar('t')) {
        return static_cast<float>(trimmed.dropLastCharacters(1).getDoubleValue() * kLegacyEnvelopeTickSeconds);
    }
    if (trimmed.endsWith("ms")) {
        return static_cast<float>(trimmed.dropLastCharacters(2).getDoubleValue() / 1000.0);
    }
    if (trimmed.endsWithChar('s')) {
        return static_cast<float>(trimmed.dropLastCharacters(1).getDoubleValue());
    }
    return trimmed.getFloatValue();
}

juce::String percentText(float value, int maximumStringLength) {
    const auto percent = std::clamp(roundedInt(value * 100.0f), 0, 100);
    auto text = juce::String(percent) + "%";
    return maximumStringLength > 0 ? text.substring(0, maximumStringLength) : text;
}

float percentValue(const juce::String& text) {
    auto trimmed = text.trim();
    if (trimmed.endsWithChar('%')) {
        return static_cast<float>(trimmed.dropLastCharacters(1).getDoubleValue() / 100.0);
    }

    const auto numeric = trimmed.getDoubleValue();
    if (!trimmed.containsChar('.') && numeric > 1.0 && numeric <= 100.0) {
        return static_cast<float>(numeric / 100.0);
    }
    return static_cast<float>(numeric);
}

juce::String amplitudeStepText(float value, int maximumStringLength) {
    const auto step = std::clamp(roundedInt(value / kGbAmplitudeStep), 0, 15);
    auto text = juce::String(step) + "/15";
    return maximumStringLength > 0 ? text.substring(0, maximumStringLength) : text;
}

float amplitudeStepValue(const juce::String& text) {
    auto trimmed = text.trim();
    if (trimmed.containsChar('/')) {
        return static_cast<float>(std::clamp(trimmed.upToFirstOccurrenceOf("/", false, false).getIntValue(), 0, 15)) * kGbAmplitudeStep;
    }
    if (trimmed.endsWithChar('%')) {
        return static_cast<float>(trimmed.dropLastCharacters(1).getDoubleValue() / 100.0);
    }

    const auto numeric = trimmed.getDoubleValue();
    if (!trimmed.containsChar('.') && numeric >= 0.0 && numeric <= 15.0) {
        return static_cast<float>(numeric) * kGbAmplitudeStep;
    }
    return static_cast<float>(numeric);
}

juce::String lsdjPhaseText(float value, int maximumStringLength) {
    const auto phase = std::clamp(roundedInt(value), 0, 31);
    auto text = "$" + juce::String::toHexString(phase).paddedLeft('0', 2).toUpperCase();
    return maximumStringLength > 0 ? text.substring(0, maximumStringLength) : text;
}

float lsdjPhaseValue(const juce::String& text) {
    const auto trimmed = text.trim();
    if (trimmed.startsWithChar('$')) {
        return static_cast<float>(std::clamp(trimmed.substring(1).getHexValue32(), 0, 31));
    }
    if (trimmed.startsWithIgnoreCase("0x")) {
        return static_cast<float>(std::clamp(trimmed.substring(2).getHexValue32(), 0, 31));
    }
    return static_cast<float>(std::clamp(trimmed.getIntValue(), 0, 31));
}

juce::String signedIntText(float value, int maximumStringLength) {
    const auto integer = roundedInt(value);
    auto text = integer > 0 ? "+" + juce::String(integer) : juce::String(integer);
    return maximumStringLength > 0 ? text.substring(0, maximumStringLength) : text;
}

float signedIntValue(const juce::String& text) {
    return static_cast<float>(text.trim().retainCharacters("+-0123456789").getIntValue());
}

juce::String semitoneText(float value, int maximumStringLength) {
    auto text = signedIntText(value, 0) + "st";
    return maximumStringLength > 0 ? text.substring(0, maximumStringLength) : text;
}

float semitoneValue(const juce::String& text) {
    return signedIntValue(text);
}

juce::String integerText(float value, int maximumStringLength) {
    auto text = juce::String(roundedInt(value));
    return maximumStringLength > 0 ? text.substring(0, maximumStringLength) : text;
}

float integerValue(const juce::String& text) {
    return static_cast<float>(text.trim().getIntValue());
}

juce::String hzText(float value, int maximumStringLength) {
    auto text = juce::String(std::clamp(roundedInt(value), 1, 64)) + "Hz";
    return maximumStringLength > 0 ? text.substring(0, maximumStringLength) : text;
}

float hzValue(const juce::String& text) {
    return static_cast<float>(std::clamp(roundedInt(static_cast<float>(text.trim().retainCharacters("0123456789.").getDoubleValue())), 1, 64));
}

float motionSyncDivision(float value) {
    static constexpr std::array<float, 7> divisions { 1.0f, 2.0f, 4.0f, 8.0f, 16.0f, 32.0f, 64.0f };
    const auto clamped = std::clamp(value, divisions.front(), divisions.back());
    return *std::min_element(divisions.begin(), divisions.end(), [clamped](float left, float right) {
        return std::abs(left - clamped) < std::abs(right - clamped);
    });
}

float bpmSyncedMotionRateHz(float division, double bpm) {
    const auto safeBpm = static_cast<float>(std::clamp(bpm, 20.0, 400.0));
    return (safeBpm / 60.0f) * (motionSyncDivision(division) / 4.0f);
}

float slideSyncDivision(float value) {
    if (value <= 0.0f) {
        return 0.0f;
    }

    return motionSyncDivision(value * 32.0f);
}

float slideSyncParameterValue(float value) {
    const auto division = slideSyncDivision(value);
    return division <= 0.0f ? 0.0f : division / 32.0f;
}

float bpmSyncedSlideTimeSeconds(float value, double bpm) {
    const auto division = slideSyncDivision(value);
    if (division <= 0.0f) {
        return 0.0f;
    }

    const auto safeBpm = static_cast<float>(std::clamp(bpm, 20.0, 400.0));
    return (60.0f / safeBpm) * (4.0f / division);
}

int lsdjWaveVolumeIndex(float value) {
    const auto step = std::clamp(roundedInt(std::clamp(value, 0.0f, 1.0f) / kLsdjWaveVolumeStep), 0, 3);
    return step;
}

float lsdjWaveVolumeParameter(float value) {
    return static_cast<float>(lsdjWaveVolumeIndex(value)) * kLsdjWaveVolumeStep;
}

float lsdjWaveVolumeAmplitude(float value) {
    static constexpr std::array<float, 4> amplitudes { 0.0f, 0.25f, 0.5f, 1.0f };
    return amplitudes[static_cast<std::size_t>(lsdjWaveVolumeIndex(value))];
}

enum class MotionStyle {
    Off,
    GbPulseDuty,
    BankSweep,
    PingPong,
    Vocal,
    Arp,
    Gate,
    NoiseBurst,
    PadShimmer,
    WsSweep,
    WsStepArp,
    PceBellLfo,
    PceDdaPluck,
    SccEchoLead,
    SccBuzzStack
};

struct SingleCycleBuildOptions {
    bool normalize = true;
    bool zeroCrossingSnap = true;
};

struct AudioSourceRegion {
    std::vector<float> samples;
    double sampleRate = 44100.0;
    int bitsPerSample = 16;
    hyperframe::dsp::RawStreamLoop loop {};
};

struct SoundFontProgram {
    juce::String name;
    std::vector<float> cycle;
    double sampleRate = 44100.0;
    float attack = 0.0f;
    float decay = 0.0f;
    float sustain = 1.0f;
    float release = 0.031f;
    hyperframe::dsp::RawStreamLoop loop {};
};

struct LsdjProgram {
    juce::String name;
    std::array<std::array<std::uint8_t, 16>, hyperframe::dsp::kWaveFrameCount> frames {};
    std::array<int, hyperframe::dsp::kCommandStepCount> motionFrames {};
    std::array<float, hyperframe::dsp::kCommandStepCount> motionPitch {};
    std::array<float, hyperframe::dsp::kCommandStepCount> motionPitchBend {};
    std::array<float, hyperframe::dsp::kCommandStepCount> motionPhase {};
    std::array<float, hyperframe::dsp::kCommandStepCount> motionVibratoRate {};
    std::array<float, hyperframe::dsp::kCommandStepCount> motionLevel {};
    int frameCount = hyperframe::dsp::kWaveFrameCount;
    int motionSteps = hyperframe::dsp::kWaveFrameCount;
    int motionLoopStart = 0;
    bool motionEnabled = true;
    bool motionLoop = true;
    float motionRate = 12.0f;
    float level = 1.0f;
    int selectedFrame = 1;
};

struct Sf2Chunk {
    std::size_t offset = 0;
    std::size_t size = 0;
};

struct Sf2PresetHeader {
    juce::String name;
    int preset = 0;
    int bank = 0;
    int bagIndex = 0;
};

struct Sf2Bag {
    int generatorIndex = 0;
};

struct Sf2Generator {
    int operatorId = 0;
    int amount = 0;
};

struct Sf2Envelope {
    float attack = 0.0f;
    float decay = 0.0f;
    float sustain = 1.0f;
    float release = 0.031f;
};

struct Sf2Instrument {
    juce::String name;
    int bagIndex = 0;
};

struct Sf2SampleHeader {
    juce::String name;
    std::uint32_t start = 0;
    std::uint32_t end = 0;
    std::uint32_t startLoop = 0;
    std::uint32_t endLoop = 0;
    std::uint32_t sampleRate = 44100;
    int originalPitch = 60;
    int pitchCorrection = 0;
    int sampleType = 0;
};

hyperframe::dsp::WaveTable::Shape shapeForFrame(hyperframe::dsp::WaveTable::Shape baseShape, int frame, MotionStyle style) {
    using Shape = hyperframe::dsp::WaveTable::Shape;

    if (style == MotionStyle::NoiseBurst) {
        return frame % 3 == 0 ? Shape::Noise : (frame % 2 == 0 ? Shape::Square : Shape::Saw);
    }

    if (style == MotionStyle::PadShimmer) {
        return frame % 3 == 0 ? Shape::Sine : (frame % 3 == 1 ? Shape::Triangle : Shape::Saw);
    }

    if (style == MotionStyle::WsSweep) {
        return frame % 4 == 0 ? Shape::Sine : (frame % 4 == 1 ? Shape::Triangle : (frame % 4 == 2 ? Shape::Square : Shape::Sine));
    }

    if (style == MotionStyle::WsStepArp) {
        return frame % 4 == 0 ? Shape::Square : (frame % 4 == 1 ? Shape::Triangle : (frame % 4 == 2 ? Shape::Sine : Shape::Square));
    }

    if (style == MotionStyle::PceBellLfo) {
        return frame % 4 == 0 ? Shape::Triangle : (frame % 4 == 1 ? Shape::Sine : (frame % 4 == 2 ? Shape::Saw : Shape::Triangle));
    }

    if (style == MotionStyle::PceDdaPluck) {
        return frame % 3 == 0 ? Shape::Saw : (frame % 3 == 1 ? Shape::Square : Shape::Triangle);
    }

    if (style == MotionStyle::SccEchoLead) {
        return frame % 4 == 0 ? Shape::Saw : (frame % 4 == 1 ? Shape::Square : (frame % 4 == 2 ? Shape::Triangle : Shape::Saw));
    }

    if (style == MotionStyle::SccBuzzStack) {
        return frame % 2 == 0 ? Shape::Saw : Shape::Square;
    }

    if (style == MotionStyle::Vocal) {
        return frame % 4 == 0 ? Shape::Square : (frame % 4 == 1 ? Shape::Saw : (frame % 4 == 2 ? Shape::Triangle : Shape::Sine));
    }

    return frame % 4 == 0 ? baseShape : (frame % 4 == 1 ? Shape::Triangle : (frame % 4 == 2 ? Shape::Saw : Shape::Square));
}

hyperframe::dsp::CommandStep presetMotionStep(MotionStyle style, int step) {
    hyperframe::dsp::CommandStep commandStep;
    const auto phaseZig = step % 2 == 0 ? 1.0f : -1.0f;

    switch (style) {
    case MotionStyle::Off:
        commandStep.frame = -1;
        commandStep.level = 1.0f;
        break;
    case MotionStyle::GbPulseDuty:
        commandStep.frame = step % 2;
        commandStep.level = 1.0f;
        break;
    case MotionStyle::BankSweep:
        commandStep.frame = step % hyperframe::dsp::kWaveFrameCount;
        commandStep.phaseAmount = phaseZig * static_cast<float>((step % 4) + 1);
        commandStep.level = step % 4 == 3 ? 0.8f : 1.0f;
        break;
    case MotionStyle::PingPong: {
        const auto folded = step % 14;
        commandStep.frame = folded < 8 ? folded : 14 - folded;
        commandStep.phaseAmount = phaseZig * 2.0f;
        commandStep.level = step % 2 == 0 ? 14.0f / 15.0f : 1.0f;
        break;
    }
    case MotionStyle::Vocal: {
        static constexpr int frames[] { 0, 1, 2, 3, 2, 1, 4, 5, 6, 5, 4, 1, 2, 3, 7, 0 };
        static constexpr float levels[] { 1.0f, 14.0f / 15.0f, 1.0f, 13.0f / 15.0f };
        commandStep.frame = frames[step % 16];
        commandStep.phaseAmount = phaseZig * static_cast<float>((step % 3) + 1);
        commandStep.pitchSemitones = step % 8 == 3 ? 2.0f : (step % 8 == 7 ? -1.0f : 0.0f);
        commandStep.level = levels[step % 4];
        break;
    }
    case MotionStyle::Arp: {
        static constexpr float pitch[] { 0.0f, 7.0f, 12.0f, 7.0f, 0.0f, 5.0f, 12.0f, 5.0f };
        commandStep.frame = step % hyperframe::dsp::kWaveFrameCount;
        commandStep.pitchSemitones = pitch[step % 8];
        commandStep.phaseAmount = phaseZig * 3.0f;
        commandStep.level = step % 2 == 0 ? 1.0f : 11.0f / 15.0f;
        break;
    }
    case MotionStyle::Gate:
        commandStep.frame = step % 2;
        commandStep.phaseAmount = phaseZig * 1.0f;
        commandStep.level = step % 2 == 0 ? 1.0f : 0.2f;
        break;
    case MotionStyle::NoiseBurst:
        commandStep.frame = step % hyperframe::dsp::kWaveFrameCount;
        commandStep.pitchSemitones = step < 3 ? 12.0f - static_cast<float>(step * 4) : 0.0f;
        commandStep.phaseAmount = phaseZig * static_cast<float>(8 - (step % 8));
        commandStep.level = step < 5 ? 1.0f : 5.0f / 15.0f;
        break;
    case MotionStyle::PadShimmer: {
        static constexpr float padLevels[] { 12.0f / 15.0f, 13.0f / 15.0f, 14.0f / 15.0f, 1.0f };
        commandStep.frame = step % hyperframe::dsp::kWaveFrameCount;
        commandStep.pitchSemitones = 0.0f;
        commandStep.phaseAmount = phaseZig * 1.0f;
        commandStep.level = padLevels[step % 4];
        break;
    }
    case MotionStyle::WsSweep: {
        static constexpr int frames[] { 0, 1, 2, 3, 4, 5, 6, 7, 6, 5, 4, 3, 2, 1, 0, 1 };
        static constexpr float levels[] { 12.0f / 15.0f, 13.0f / 15.0f, 14.0f / 15.0f, 1.0f };
        commandStep.frame = frames[step % 16];
        commandStep.pitchSemitones = step % 8 == 6 ? 5.0f : 0.0f;
        commandStep.phaseAmount = static_cast<float>((step % 4) + 1);
        commandStep.level = levels[step % 4];
        break;
    }
    case MotionStyle::WsStepArp: {
        static constexpr float pitch[] { 0.0f, 7.0f, 12.0f, 19.0f, 12.0f, 7.0f, 0.0f, -5.0f };
        static constexpr int frames[] { 0, 2, 4, 6, 7, 5, 3, 1 };
        commandStep.frame = frames[step % 8];
        commandStep.pitchSemitones = pitch[step % 8];
        commandStep.phaseAmount = step % 2 == 0 ? 0.0f : 2.0f;
        commandStep.level = step % 4 == 3 ? 11.0f / 15.0f : 1.0f;
        break;
    }
    case MotionStyle::PceBellLfo: {
        static constexpr int frames[] { 0, 1, 2, 3, 4, 5, 4, 3, 2, 1, 0, 1, 2, 3, 4, 5 };
        static constexpr float levels[] { 1.0f, 14.0f / 15.0f, 13.0f / 15.0f, 12.0f / 15.0f, 11.0f / 15.0f, 10.0f / 15.0f, 9.0f / 15.0f, 8.0f / 15.0f };
        commandStep.frame = frames[step % 16];
        commandStep.pitchSemitones = step % 8 == 4 ? 12.0f : 0.0f;
        commandStep.phaseAmount = 1.0f + static_cast<float>(step % 3);
        commandStep.level = levels[step % 8];
        break;
    }
    case MotionStyle::PceDdaPluck: {
        static constexpr int frames[] { 0, 3, 6, 9, 12, 9, 6, 3 };
        static constexpr float levels[] { 1.0f, 1.0f, 13.0f / 15.0f, 11.0f / 15.0f, 9.0f / 15.0f, 7.0f / 15.0f, 5.0f / 15.0f, 4.0f / 15.0f };
        commandStep.frame = frames[step % 8];
        commandStep.pitchSemitones = step == 1 ? 12.0f : (step == 2 ? 7.0f : 0.0f);
        commandStep.phaseAmount = static_cast<float>(step % 5);
        commandStep.level = levels[step % 8];
        break;
    }
    case MotionStyle::SccEchoLead: {
        static constexpr int frames[] { 0, 1, 2, 1, 3, 4, 5, 4, 6, 7, 8, 7, 9, 10, 11, 10 };
        static constexpr float levels[] { 1.0f, 10.0f / 15.0f, 14.0f / 15.0f, 9.0f / 15.0f };
        commandStep.frame = frames[step % 16];
        commandStep.pitchSemitones = step % 4 == 1 ? 0.0f : (step % 4 == 3 ? -12.0f : 0.0f);
        commandStep.phaseAmount = phaseZig * static_cast<float>((step % 3) + 1);
        commandStep.level = levels[step % 4];
        break;
    }
    case MotionStyle::SccBuzzStack: {
        static constexpr float pitch[] { 0.0f, 0.0f, 7.0f, 0.0f, 12.0f, 0.0f, 7.0f, -12.0f };
        commandStep.frame = (step * 3) % hyperframe::dsp::kWaveFrameCount;
        commandStep.pitchSemitones = pitch[step % 8];
        commandStep.phaseAmount = phaseZig * static_cast<float>((step % 4) + 2);
        commandStep.level = step % 2 == 0 ? 1.0f : 12.0f / 15.0f;
        break;
    }
    }

    return commandStep;
}

int engineModeIndex(hyperframe::dsp::CommandSettings::EngineMode engineMode) {
    switch (engineMode) {
    case hyperframe::dsp::CommandSettings::EngineMode::Raw:
        return 0;
    case hyperframe::dsp::CommandSettings::EngineMode::Draw:
        return 1;
    case hyperframe::dsp::CommandSettings::EngineMode::Wave:
        return 2;
    case hyperframe::dsp::CommandSettings::EngineMode::WonderSwan:
        return 3;
    case hyperframe::dsp::CommandSettings::EngineMode::PcEngine:
        return 4;
    case hyperframe::dsp::CommandSettings::EngineMode::Scc:
        return 5;
    default:
        return 0;
    }
}

hyperframe::dsp::CommandSettings::EngineMode engineModeFromIndex(int index) {
    switch (index) {
    case 0:
        return hyperframe::dsp::CommandSettings::EngineMode::Raw;
    case 1:
        return hyperframe::dsp::CommandSettings::EngineMode::Draw;
    case 2:
        return hyperframe::dsp::CommandSettings::EngineMode::Wave;
    case 3:
        return hyperframe::dsp::CommandSettings::EngineMode::WonderSwan;
    case 4:
        return hyperframe::dsp::CommandSettings::EngineMode::PcEngine;
    case 5:
        return hyperframe::dsp::CommandSettings::EngineMode::Scc;
    default:
        return hyperframe::dsp::CommandSettings::EngineMode::Raw;
    }
}

hyperframe::dsp::WaveTable::SampleFormat sampleFormatForEngineMode(hyperframe::dsp::CommandSettings::EngineMode engineMode) {
    return hyperframe::dsp::hardwareWaveSampleFormat(engineMode);
}

int sampleFormatIndex(hyperframe::dsp::WaveTable::SampleFormat format) {
    return format == hyperframe::dsp::WaveTable::SampleFormat::Signed ? 1 : 0;
}

hyperframe::dsp::WaveTable::SampleFormat sampleFormatFromIndex(int index) {
    return index == 1
        ? hyperframe::dsp::WaveTable::SampleFormat::Signed
        : hyperframe::dsp::WaveTable::SampleFormat::Unsigned;
}

int lsdjPhaseModeIndex(hyperframe::dsp::LsdjPhaseMode phaseMode) {
    switch (phaseMode) {
    case hyperframe::dsp::LsdjPhaseMode::Resync:
        return 1;
    case hyperframe::dsp::LsdjPhaseMode::Resync2:
        return 2;
    case hyperframe::dsp::LsdjPhaseMode::Normal:
    default:
        return 0;
    }
}

hyperframe::dsp::LsdjPhaseMode lsdjPhaseModeFromIndex(int index) {
    switch (index) {
    case 1:
        return hyperframe::dsp::LsdjPhaseMode::Resync;
    case 2:
        return hyperframe::dsp::LsdjPhaseMode::Resync2;
    case 0:
    default:
        return hyperframe::dsp::LsdjPhaseMode::Normal;
    }
}

int gbWaveRamNibbleFromValue(float value) {
    const auto normalized = std::clamp((value + 1.0f) * 0.5f, 0.0f, 1.0f);
    return std::clamp(static_cast<int>(std::round(normalized * 15.0f)), 0, 15);
}

float gbWaveRamValueFromNibble(int nibble) {
    return (static_cast<float>(std::clamp(nibble, 0, 15)) / 15.0f * 2.0f) - 1.0f;
}

std::size_t sourceIndexForActivePoint(std::size_t index, std::size_t activeLength) {
    const auto clampedLength = std::clamp(activeLength,
        hyperframe::dsp::WaveTable::kMinActiveLength,
        hyperframe::dsp::WaveTable::kMaxPoints);
    return ((index % clampedLength) * hyperframe::dsp::WaveTable::kMaxPoints) / clampedLength;
}

int hexDigitValue(juce::juce_wchar character) {
    if (character >= '0' && character <= '9') {
        return static_cast<int>(character - '0');
    }

    if (character >= 'A' && character <= 'F') {
        return static_cast<int>(character - 'A') + 10;
    }

    if (character >= 'a' && character <= 'f') {
        return static_cast<int>(character - 'a') + 10;
    }

    return -1;
}

juce::String compactHexText(juce::String text) {
    text = text.replace("0x", "", true).replace("$", "", true);
    juce::String compact;

    for (int i = 0; i < text.length(); ++i) {
        const auto character = text[i];
        if (hexDigitValue(character) >= 0) {
            compact += juce::String::charToString(character);
        }
    }

    return compact;
}

void writeGbWaveRamHexToFrame(hyperframe::dsp::WaveTable& waveFrame, const juce::String& text) {
    const auto compact = compactHexText(text);
    if (compact.length() != 32) {
        return;
    }

    waveFrame.setActiveLength(32);
    waveFrame.setSampleFormat(hyperframe::dsp::WaveTable::SampleFormat::Unsigned);
    waveFrame.setBitDepth(4);
    waveFrame.clearLoop();
    for (int i = 0; i < compact.length(); ++i) {
        const auto nibble = hexDigitValue(compact[i]);
        if (nibble < 0) {
            continue;
        }

        const auto value = gbWaveRamValueFromNibble(nibble);
        waveFrame.setPoint(sourceIndexForActivePoint(static_cast<std::size_t>(i), 32), value);
    }
}

void writeGbWavePulseDefaultFrame(hyperframe::dsp::WaveTable& waveFrame, int frame) {
    static constexpr std::array<const char*, 2> dutyHex {
        "FFFFFFFFFFFFFFFF0000000000000000",
        "FFFFFFFFFFFF00000000000000000000",
    };

    writeGbWaveRamHexToFrame(waveFrame, dutyHex[static_cast<std::size_t>(frame) % dutyHex.size()]);
}

void writeGbWaveRamBytesToFrame(hyperframe::dsp::WaveTable& waveFrame, const std::array<std::uint8_t, 16>& bytes) {
    waveFrame.setActiveLength(32);
    waveFrame.setSampleFormat(hyperframe::dsp::WaveTable::SampleFormat::Unsigned);
    waveFrame.setBitDepth(4);
    waveFrame.clearLoop();

    for (int byteIndex = 0; byteIndex < 16; ++byteIndex) {
        const auto packed = bytes[static_cast<std::size_t>(byteIndex)];
        const auto high = (packed >> 4) & 0x0F;
        const auto low = packed & 0x0F;
        waveFrame.setPoint(sourceIndexForActivePoint(static_cast<std::size_t>(byteIndex * 2), 32), gbWaveRamValueFromNibble(high));
        waveFrame.setPoint(sourceIndexForActivePoint(static_cast<std::size_t>((byteIndex * 2) + 1), 32), gbWaveRamValueFromNibble(low));
    }
}

juce::String sanitizedFixedText(const char* text, std::size_t length, juce::String fallback) {
    juce::String result;
    for (std::size_t i = 0; i < length; ++i) {
        const auto character = static_cast<unsigned char>(text[i]);
        if (character >= 33 && character <= 126) {
            result += juce::String::charToString(static_cast<juce::juce_wchar>(character));
        } else if (character == ' ' && result.isNotEmpty()) {
            result += " ";
        }
    }

    result = result.trim();
    return result.isEmpty() ? fallback : result;
}

std::string lsdjFramesKey(const LsdjProgram& program) {
    std::string key;
    key.reserve(static_cast<std::size_t>(program.frameCount) * 16);
    for (int frame = 0; frame < program.frameCount; ++frame) {
        const auto& bytes = program.frames[static_cast<std::size_t>(frame)];
        key.append(reinterpret_cast<const char*>(bytes.data()), bytes.size());
    }
    key.append(reinterpret_cast<const char*>(&program.motionSteps), sizeof(program.motionSteps));
    key.append(reinterpret_cast<const char*>(&program.motionLoopStart), sizeof(program.motionLoopStart));
    key.append(reinterpret_cast<const char*>(&program.motionEnabled), sizeof(program.motionEnabled));
    key.append(reinterpret_cast<const char*>(&program.motionLoop), sizeof(program.motionLoop));
    key.append(reinterpret_cast<const char*>(&program.motionRate), sizeof(program.motionRate));
    key.append(reinterpret_cast<const char*>(&program.level), sizeof(program.level));
    key.append(reinterpret_cast<const char*>(&program.selectedFrame), sizeof(program.selectedFrame));
    key.append(reinterpret_cast<const char*>(program.motionFrames.data()), static_cast<std::size_t>(program.motionSteps) * sizeof(int));
    key.append(reinterpret_cast<const char*>(program.motionPitch.data()), static_cast<std::size_t>(program.motionSteps) * sizeof(float));
    key.append(reinterpret_cast<const char*>(program.motionPitchBend.data()), static_cast<std::size_t>(program.motionSteps) * sizeof(float));
    key.append(reinterpret_cast<const char*>(program.motionPhase.data()), static_cast<std::size_t>(program.motionSteps) * sizeof(float));
    key.append(reinterpret_cast<const char*>(program.motionVibratoRate.data()), static_cast<std::size_t>(program.motionSteps) * sizeof(float));
    key.append(reinterpret_cast<const char*>(program.motionLevel.data()), static_cast<std::size_t>(program.motionSteps) * sizeof(float));
    return key;
}

bool lsdjProgramHasSound(const LsdjProgram& program) {
    for (int frame = 0; frame < program.frameCount; ++frame) {
        const auto& bytes = program.frames[static_cast<std::size_t>(frame)];
        for (const auto byte : bytes) {
            if (byte != 0x88) {
                return true;
            }
        }
    }
    return false;
}

float lsdjWaveVolumeParameterFromRaw(std::uint8_t volume) {
    switch (volume) {
    case LSDJ_INSTRUMENT_WAVE_VOLUME_0:
        return 0.0f;
    case LSDJ_INSTRUMENT_WAVE_VOLUME_1:
        return kLsdjWaveVolumeStep;
    case LSDJ_INSTRUMENT_WAVE_VOLUME_2:
        return kLsdjWaveVolumeStep * 2.0f;
    case LSDJ_INSTRUMENT_WAVE_VOLUME_3:
        return 1.0f;
    default:
        return 1.0f;
    }
}

float lsdjWaveSpeedToMotionRate(std::uint8_t speed) {
    const auto displaySpeed = std::max(1, static_cast<int>(speed));
    return static_cast<float>(std::clamp(64 / displaySpeed, 1, 64));
}

int lsdjSignedByte(std::uint8_t value) {
    return value < 0x80 ? static_cast<int>(value) : static_cast<int>(value) - 0x100;
}

float lsdjWaveLevelFromEnvelope(std::uint8_t envelope) {
    const auto amplitude = static_cast<float>((envelope >> 4) & 0x0F) / 15.0f;
    return lsdjWaveVolumeParameter(amplitude);
}

float lsdjWaveLevelFromCommand(std::uint8_t value) {
    return static_cast<float>(std::clamp(static_cast<int>(value & 0x03), 0, 3)) * kLsdjWaveVolumeStep;
}

float lsdjPitchBendRate(std::uint8_t value) {
    return static_cast<float>(std::clamp(lsdjSignedByte(value) * 4, -96, 96));
}

float lsdjVibratoRate(std::uint8_t value) {
    const auto period = std::max(1, static_cast<int>((value >> 4) & 0x0F));
    return static_cast<float>(std::clamp(64 / period, 1, 64));
}

float lsdjVibratoDepth(std::uint8_t value) {
    return static_cast<float>(std::clamp(static_cast<int>(value & 0x0F), 0, 15));
}

void applyLsdjTableCommand(LsdjProgram& program, lsdj_command_t command, std::uint8_t value, int row, int& runningFrame) {
    const auto step = static_cast<std::size_t>(std::clamp(row, 0, hyperframe::dsp::kCommandStepCount - 1));
    switch (command) {
    case LSDJ_COMMAND_E:
        program.motionLevel[step] = lsdjWaveLevelFromCommand(value);
        break;
    case LSDJ_COMMAND_F:
        runningFrame = (runningFrame + lsdjSignedByte(value)) & 0x0F;
        program.motionFrames[step] = runningFrame;
        break;
    case LSDJ_COMMAND_H:
        if (value <= static_cast<std::uint8_t>(row)) {
            program.motionLoop = true;
            program.motionLoopStart = std::clamp(static_cast<int>(value), 0, row);
            program.motionSteps = std::max(1, row + 1);
        }
        break;
    case LSDJ_COMMAND_K:
        program.motionLevel[step] = 0.0f;
        program.motionLoop = false;
        program.motionSteps = std::max(1, row + 1);
        break;
    case LSDJ_COMMAND_P:
    case LSDJ_COMMAND_L:
        program.motionPitchBend[step] = lsdjPitchBendRate(value);
        break;
    case LSDJ_COMMAND_V:
        program.motionPhase[step] = lsdjVibratoDepth(value);
        program.motionVibratoRate[step] = lsdjVibratoRate(value);
        break;
    default:
        break;
    }
}

void configureLsdjProgramFromTable(LsdjProgram& program, const lsdj_song_t& song, std::uint8_t instrument) {
    if (!lsdj_instrument_is_table_enabled(&song, instrument)) {
        return;
    }

    const auto table = lsdj_instrument_get_table(&song, instrument);
    if (table >= LSDJ_TABLE_COUNT || !lsdj_table_is_allocated(&song, table)) {
        return;
    }

    program.motionEnabled = true;
    program.motionSteps = hyperframe::dsp::kCommandStepCount;
    if (lsdj_instrument_get_table_mode(&song, instrument) == LSDJ_INSTRUMENT_TABLE_STEP) {
        program.motionLoop = false;
    }

    auto runningFrame = std::clamp(program.selectedFrame - 1, 0, hyperframe::dsp::kWaveFrameCount - 1);
    for (int row = 0; row < hyperframe::dsp::kCommandStepCount; ++row) {
        const auto step = static_cast<std::size_t>(row);
        program.motionFrames[step] = runningFrame;
        program.motionPitch[step] = static_cast<float>(std::clamp(lsdjSignedByte(lsdj_table_get_transposition(&song, table, static_cast<std::uint8_t>(row))), -24, 24));

        const auto envelope = lsdj_table_get_envelope(&song, table, static_cast<std::uint8_t>(row));
        if (envelope != 0x00) {
            program.motionLevel[step] = lsdjWaveLevelFromEnvelope(envelope);
        }

        applyLsdjTableCommand(program, lsdj_table_get_command1(&song, table, static_cast<std::uint8_t>(row)), lsdj_table_get_command1_value(&song, table, static_cast<std::uint8_t>(row)), row, runningFrame);
        applyLsdjTableCommand(program, lsdj_table_get_command2(&song, table, static_cast<std::uint8_t>(row)), lsdj_table_get_command2_value(&song, table, static_cast<std::uint8_t>(row)), row, runningFrame);
    }
}

void configureLsdjProgramFromInstrument(LsdjProgram& program, const lsdj_song_t& song, std::uint8_t instrument, int synth) {
    const auto lengthFrames = std::clamp(static_cast<int>(lsdj_instrument_wave_get_length(&song, instrument)) + 1, 1, hyperframe::dsp::kWaveFrameCount);
    const auto playMode = lsdj_instrument_wave_get_play_mode(&song, instrument);
    const auto loopPos = std::clamp(static_cast<int>(lsdj_instrument_wave_get_loop_pos(&song, instrument)), 0, hyperframe::dsp::kWaveFrameCount - 1);

    program.frameCount = hyperframe::dsp::kWaveFrameCount;
    program.level = lsdjWaveVolumeParameterFromRaw(lsdj_instrument_wave_get_volume(&song, instrument));
    program.motionRate = lsdjWaveSpeedToMotionRate(lsdj_instrument_wave_get_speed(&song, instrument));
    program.motionLoop = playMode != LSDJ_INSTRUMENT_WAVE_PLAY_ONCE;
    program.motionEnabled = playMode != LSDJ_INSTRUMENT_WAVE_PLAY_MANUAL && lengthFrames > 1;
    program.motionLevel.fill(program.level);
    program.motionLoopStart = 0;

    if (playMode == LSDJ_INSTRUMENT_WAVE_PLAY_MANUAL) {
        const auto manualWave = std::clamp(static_cast<int>(lsdj_instrument_wave_get_wave(&song, instrument)), 0, 255);
        const auto firstWave = std::clamp(synth, 0, 15) * hyperframe::dsp::kWaveFrameCount;
        const auto localFrame = manualWave - firstWave;
        program.motionEnabled = true;
        program.motionLoop = false;
        if (localFrame >= 0 && localFrame < hyperframe::dsp::kWaveFrameCount) {
            program.selectedFrame = localFrame + 1;
            program.motionFrames[0] = localFrame;
        } else if (const auto* bytes = lsdj_wave_get_bytes_const(&song, static_cast<std::uint8_t>(manualWave))) {
            std::copy(bytes, bytes + 16, program.frames[0].begin());
            program.frameCount = 1;
            program.selectedFrame = 1;
            program.motionFrames[0] = 0;
        }
        program.motionSteps = 1;
        configureLsdjProgramFromTable(program, song, instrument);
        return;
    }

    if (playMode == LSDJ_INSTRUMENT_WAVE_PLAY_PING_PONG) {
        program.motionSteps = hyperframe::dsp::kCommandStepCount;
        const auto period = std::max(1, (lengthFrames * 2) - 2);
        for (int step = 0; step < program.motionSteps; ++step) {
            const auto wrapped = step % period;
            program.motionFrames[static_cast<std::size_t>(step)] = wrapped < lengthFrames ? wrapped : period - wrapped;
        }
        configureLsdjProgramFromTable(program, song, instrument);
        return;
    }

    program.motionLoopStart = playMode == LSDJ_INSTRUMENT_WAVE_PLAY_LOOP && loopPos < lengthFrames ? loopPos : 0;
    program.motionSteps = std::clamp(lengthFrames, 1, hyperframe::dsp::kCommandStepCount);
    for (int step = 0; step < program.motionSteps; ++step) {
        program.motionFrames[static_cast<std::size_t>(step)] = step;
    }
    configureLsdjProgramFromTable(program, song, instrument);
}

LsdjProgram makeLsdjProgramFromSynth(const lsdj_song_t& song, int synth, juce::String name) {
    LsdjProgram program;
    program.name = std::move(name);
    program.frameCount = hyperframe::dsp::kWaveFrameCount;
    program.motionSteps = hyperframe::dsp::kWaveFrameCount;
    program.motionLevel.fill(program.level);
    for (int frame = 0; frame < hyperframe::dsp::kCommandStepCount; ++frame) {
        program.motionFrames[static_cast<std::size_t>(frame)] = frame % hyperframe::dsp::kWaveFrameCount;
    }

    const auto firstWave = std::clamp(synth, 0, 15) * hyperframe::dsp::kWaveFrameCount;
    for (int frame = 0; frame < hyperframe::dsp::kWaveFrameCount; ++frame) {
        const auto waveIndex = static_cast<std::uint8_t>(firstWave + frame);
        const auto* bytes = lsdj_wave_get_bytes_const(&song, waveIndex);
        if (bytes == nullptr) {
            continue;
        }

        std::copy(bytes, bytes + 16, program.frames[static_cast<std::size_t>(frame)].begin());
    }

    return program;
}

void appendLsdjProgram(std::vector<LsdjProgram>& programs, std::set<std::string>& seen, LsdjProgram program) {
    if (!lsdjProgramHasSound(program)) {
        return;
    }

    const auto key = lsdjFramesKey(program);
    if (seen.insert(key).second) {
        programs.push_back(std::move(program));
    }
}

void appendLsdjSongPrograms(std::vector<LsdjProgram>& programs, std::set<std::string>& seen, const lsdj_song_t& song, const juce::String& songName) {
    std::array<bool, 16> importedSynths {};

    for (int instrument = 0; instrument < LSDJ_INSTRUMENT_COUNT; ++instrument) {
        const auto instrumentIndex = static_cast<std::uint8_t>(instrument);
        if (!lsdj_instrument_is_allocated(&song, instrumentIndex)
            || lsdj_instrument_get_type(&song, instrumentIndex) != LSDJ_INSTRUMENT_TYPE_WAVE) {
            continue;
        }

        const auto synth = std::clamp(static_cast<int>(lsdj_instrument_wave_get_synth(&song, instrumentIndex)), 0, 15);
        if (!lsdj_synth_is_wave_overwritten(&song, static_cast<std::uint8_t>(synth))) {
            continue;
        }

        importedSynths[static_cast<std::size_t>(synth)] = true;
        const auto instrumentName = sanitizedFixedText(
            lsdj_instrument_get_name(&song, instrumentIndex),
            LSDJ_INSTRUMENT_NAME_LENGTH,
            "Inst " + juce::String::toHexString(instrument).paddedLeft('0', 2).toUpperCase());
        auto program = makeLsdjProgramFromSynth(song, synth, songName + " / " + instrumentName);
        configureLsdjProgramFromInstrument(program, song, instrumentIndex, synth);
        appendLsdjProgram(programs, seen, std::move(program));
    }

    for (int synth = 0; synth < 16; ++synth) {
        if (importedSynths[static_cast<std::size_t>(synth)]) {
            continue;
        }

        if (!lsdj_synth_is_wave_overwritten(&song, static_cast<std::uint8_t>(synth))) {
            continue;
        }

        auto program = makeLsdjProgramFromSynth(song, synth, songName + " / Synth " + juce::String::toHexString(synth).toUpperCase());
        if (lsdjProgramHasSound(program)) {
            appendLsdjProgram(programs, seen, std::move(program));
        }
    }
}

void appendLsdjSntPrograms(std::vector<LsdjProgram>& programs, std::set<std::string>& seen, const juce::File& file, const juce::MemoryBlock& block) {
    const auto bytes = static_cast<const std::uint8_t*>(block.getData());
    const auto frameCount = static_cast<int>(block.getSize() / 16);
    if (frameCount <= 0 || block.getSize() % 16 != 0) {
        return;
    }

    const auto programCount = (frameCount + hyperframe::dsp::kWaveFrameCount - 1) / hyperframe::dsp::kWaveFrameCount;
    for (int programIndex = 0; programIndex < programCount; ++programIndex) {
        LsdjProgram program;
        program.name = file.getFileNameWithoutExtension();
        if (programCount > 1) {
            program.name += " / " + juce::String(programIndex + 1);
        }

        program.frameCount = std::min(hyperframe::dsp::kWaveFrameCount, frameCount - (programIndex * hyperframe::dsp::kWaveFrameCount));
        program.motionSteps = program.frameCount;
        program.motionLevel.fill(program.level);
        for (int frame = 0; frame < program.frameCount; ++frame) {
            const auto source = bytes + ((programIndex * hyperframe::dsp::kWaveFrameCount + frame) * 16);
            std::copy(source, source + 16, program.frames[static_cast<std::size_t>(frame)].begin());
            program.motionFrames[static_cast<std::size_t>(frame)] = frame;
        }

        appendLsdjProgram(programs, seen, std::move(program));
    }
}

void appendLsdjProjectPrograms(std::vector<LsdjProgram>& programs, std::set<std::string>& seen, const lsdj_project_t& project, juce::String fallbackName) {
    const auto name = sanitizedFixedText(lsdj_project_get_name(&project), lsdj_project_get_name_length(&project), std::move(fallbackName));
    if (const auto* song = lsdj_project_get_song_const(&project)) {
        appendLsdjSongPrograms(programs, seen, *song, name);
    }
}

std::vector<LsdjProgram> parseLsdjPrograms(const juce::File& file) {
    juce::MemoryBlock block;
    if (!file.loadFileAsData(block) || block.isEmpty()) {
        return {};
    }

    std::vector<LsdjProgram> programs;
    std::set<std::string> seen;
    const auto extension = file.getFileExtension().toLowerCase();
    if (extension == ".snt") {
        appendLsdjSntPrograms(programs, seen, file, block);
        return programs;
    }

    const auto* data = static_cast<const std::uint8_t*>(block.getData());
    if (extension == ".lsdsng") {
        lsdj_project_t* project = nullptr;
        if (lsdj_project_read_lsdsng_from_memory(data, block.getSize(), &project, nullptr) == LSDJ_SUCCESS && project != nullptr) {
            appendLsdjProjectPrograms(programs, seen, *project, file.getFileNameWithoutExtension());
        }
        lsdj_project_free(project);
        return programs;
    }

    if (extension == ".sav") {
        lsdj_sav_t* sav = nullptr;
        if (lsdj_sav_read_from_memory(data, block.getSize(), &sav, nullptr) == LSDJ_SUCCESS && sav != nullptr) {
            if (const auto* song = lsdj_sav_get_working_memory_song_const(sav)) {
                appendLsdjSongPrograms(programs, seen, *song, file.getFileNameWithoutExtension() + " / WM");
            }

            for (int projectIndex = 0; projectIndex < LSDJ_SAV_PROJECT_COUNT; ++projectIndex) {
                if (const auto* project = lsdj_sav_get_project_const(sav, static_cast<std::uint8_t>(projectIndex))) {
                    appendLsdjProjectPrograms(programs, seen, *project, "Slot " + juce::String(projectIndex + 1));
                }
            }
        }
        lsdj_sav_free(sav);
    }

    return programs;
}

float sampleLinear(const std::vector<float>& samples, float position) {
    if (samples.empty()) {
        return 0.0f;
    }

    const auto clampedPosition = std::clamp(position, 0.0f, static_cast<float>(samples.size() - 1));
    const auto index0 = static_cast<std::size_t>(std::floor(clampedPosition));
    const auto index1 = std::min(index0 + 1, samples.size() - 1);
    const auto fraction = clampedPosition - std::floor(clampedPosition);
    return samples[index0] + ((samples[index1] - samples[index0]) * fraction);
}

std::vector<float> buildSingleCycleFromSamples(std::vector<float> samples, SingleCycleBuildOptions options) {
    if (samples.size() < 8) {
        return {};
    }

    const auto mean = std::accumulate(samples.begin(), samples.end(), 0.0f) / static_cast<float>(samples.size());
    auto peak = 0.0f;
    for (auto& sample : samples) {
        sample -= mean;
        peak = std::max(peak, std::abs(sample));
    }

    if (peak <= 1.0e-5f) {
        return {};
    }

    const auto threshold = std::max(peak * 0.02f, 1.0e-4f);
    auto firstAudible = std::size_t { 0 };
    while (firstAudible + 1 < samples.size() && std::abs(samples[firstAudible]) < threshold) {
        ++firstAudible;
    }

    auto cycleStart = static_cast<float>(firstAudible);
    auto cycleLength = std::min<float>(static_cast<float>(samples.size() - firstAudible), 512.0f);

    if (options.zeroCrossingSnap) {
        std::vector<float> crossings;
        crossings.reserve(64);
        for (std::size_t index = firstAudible > 0 ? firstAudible - 1 : 0; index + 1 < samples.size(); ++index) {
            const auto current = samples[index];
            const auto next = samples[index + 1];
            if (current <= 0.0f && next > 0.0f) {
                const auto denominator = std::abs(current) + std::abs(next);
                crossings.push_back(static_cast<float>(index) + (denominator > 0.0f ? std::abs(current) / denominator : 0.0f));
            }
        }

        for (std::size_t crossing = 0; crossing + 1 < crossings.size(); ++crossing) {
            const auto candidateLength = crossings[crossing + 1] - crossings[crossing];
            if (candidateLength >= 8.0f && crossings[crossing] + candidateLength < static_cast<float>(samples.size())) {
                cycleStart = crossings[crossing];
                cycleLength = candidateLength;
                break;
            }
        }
    }

    if (cycleLength < 8.0f) {
        return {};
    }

    std::vector<float> cycle(hyperframe::dsp::WaveTable::kMaxPoints, 0.0f);
    auto cyclePeak = 0.0f;
    for (std::size_t point = 0; point < cycle.size(); ++point) {
        const auto position = cycleStart + (cycleLength * static_cast<float>(point) / static_cast<float>(cycle.size()));
        auto value = sampleLinear(samples, position);
        if (point == 0 && options.zeroCrossingSnap) {
            value = 0.0f;
        }

        cycle[point] = value;
        cyclePeak = std::max(cyclePeak, std::abs(value));
    }

    if (cyclePeak <= 1.0e-5f) {
        return {};
    }

    for (auto& sample : cycle) {
        sample = options.normalize ? std::clamp(sample / cyclePeak, -1.0f, 1.0f) : std::clamp(sample, -1.0f, 1.0f);
    }
    if (options.zeroCrossingSnap) {
        cycle.front() = 0.0f;
    }

    return cycle;
}

void normalizeSamples(std::vector<float>& samples) {
    auto peak = 0.0f;
    for (auto sample : samples) {
        peak = std::max(peak, std::abs(sample));
    }

    if (peak <= 1.0e-5f) {
        return;
    }

    for (auto& sample : samples) {
        sample = std::clamp(sample / peak, -1.0f, 1.0f);
    }
}

double bytebeatFrameScore(const std::vector<float>& samples, std::size_t start) {
    if (start + 32 > samples.size()) {
        return 0.0;
    }

    auto mean = 0.0;
    for (std::size_t i = 0; i < 32; ++i) {
        mean += samples[start + i];
    }
    mean /= 32.0;

    auto energy = 0.0;
    auto movement = 0.0;
    auto peak = 0.0;
    auto zeroCrossings = 0;
    auto previous = samples[start] - static_cast<float>(mean);
    for (std::size_t i = 0; i < 32; ++i) {
        const auto value = samples[start + i] - static_cast<float>(mean);
        energy += static_cast<double>(value * value);
        peak = std::max(peak, std::abs(static_cast<double>(value)));
        if (i > 0) {
            movement += std::abs(static_cast<double>(value - previous));
            if ((previous < 0.0f && value >= 0.0f) || (previous >= 0.0f && value < 0.0f)) {
                ++zeroCrossings;
            }
        }
        previous = value;
    }

    const auto rms = std::sqrt(energy / 32.0);
    const auto motion = movement / 31.0;
    const auto crossings = static_cast<double>(zeroCrossings) / 31.0;
    return (rms * 0.45) + (motion * 0.45) + (crossings * 0.10) + (peak * 0.15);
}

double bytebeatFrameDistance(const std::vector<float>& samples, std::size_t leftStart, std::size_t rightStart) {
    auto distance = 0.0;
    for (std::size_t i = 0; i < 32; ++i) {
        distance += std::abs(static_cast<double>(samples[leftStart + i] - samples[rightStart + i]));
    }
    return distance / 32.0;
}

std::array<std::size_t, hyperframe::dsp::kWaveFrameCount> selectBytebeatFrameStarts(const std::vector<float>& samples,
                                                                                   double sampleRate,
                                                                                   double frameStrideMs) {
    std::array<std::size_t, hyperframe::dsp::kWaveFrameCount> starts {};
    if (samples.size() <= 32) {
        return starts;
    }

    struct Candidate {
        std::size_t start = 0;
        double score = 0.0;
    };

    const auto maxStart = samples.size() - 32;
    const auto hop = std::max<std::size_t>(1, static_cast<std::size_t>(std::round(sampleRate / 1000.0)));
    auto candidates = std::vector<Candidate> {};
    candidates.reserve((maxStart / hop) + 1);
    for (std::size_t start = 0; start <= maxStart; start += hop) {
        const auto score = bytebeatFrameScore(samples, start);
        if (score > 0.02) {
            candidates.push_back({ start, score });
        }
    }

    std::sort(candidates.begin(), candidates.end(), [](const Candidate& left, const Candidate& right) {
        return left.score > right.score;
    });

    auto selected = std::vector<std::size_t> {};
    selected.reserve(hyperframe::dsp::kWaveFrameCount);
    auto minSpacing = frameStrideMs > 0.0
        ? static_cast<std::size_t>(std::round(frameStrideMs * sampleRate / 1000.0))
        : maxStart / hyperframe::dsp::kWaveFrameCount;
    minSpacing = std::max<std::size_t>(32, minSpacing);

    while (selected.size() < hyperframe::dsp::kWaveFrameCount && minSpacing >= 1) {
        for (const auto& candidate : candidates) {
            const auto farEnough = std::all_of(selected.begin(), selected.end(), [candidate, minSpacing](std::size_t selectedStart) {
                const auto distance = candidate.start > selectedStart
                    ? candidate.start - selectedStart
                    : selectedStart - candidate.start;
                return distance >= minSpacing;
            });
            const auto differentEnough = std::all_of(selected.begin(), selected.end(), [&samples, candidate](std::size_t selectedStart) {
                return bytebeatFrameDistance(samples, candidate.start, selectedStart) >= 0.05;
            });
            if (farEnough && differentEnough) {
                selected.push_back(candidate.start);
                if (selected.size() == hyperframe::dsp::kWaveFrameCount) {
                    break;
                }
            }
        }
        if (selected.size() == hyperframe::dsp::kWaveFrameCount) {
            break;
        }
        minSpacing /= 2;
    }

    for (std::size_t i = selected.size(); i < hyperframe::dsp::kWaveFrameCount; ++i) {
        selected.push_back(static_cast<std::size_t>(std::round(static_cast<double>(maxStart) * static_cast<double>(i) / static_cast<double>(hyperframe::dsp::kWaveFrameCount - 1))));
    }

    std::sort(selected.begin(), selected.end());
    for (std::size_t i = 0; i < starts.size(); ++i) {
        starts[i] = std::min(selected[i], maxStart);
    }
    return starts;
}

float bytebeatFramePoint(const std::vector<float>& samples, std::size_t start, std::size_t point, bool normalizeFrame) {
    if (!normalizeFrame) {
        return samples[std::min(start + point, samples.size() - 1)];
    }

    auto mean = 0.0f;
    for (std::size_t i = 0; i < 32; ++i) {
        mean += samples[std::min(start + i, samples.size() - 1)];
    }
    mean /= 32.0f;

    auto peak = 0.0f;
    for (std::size_t i = 0; i < 32; ++i) {
        peak = std::max(peak, std::abs(samples[std::min(start + i, samples.size() - 1)] - mean));
    }

    const auto value = samples[std::min(start + point, samples.size() - 1)] - mean;
    return peak > 1.0e-5f ? std::clamp(value / peak, -1.0f, 1.0f) : 0.0f;
}

bool fourccEquals(const std::uint8_t* data, const char* id) {
    return std::memcmp(data, id, 4) == 0;
}

std::uint16_t readU16(const std::uint8_t* data) {
    return static_cast<std::uint16_t>(data[0] | (data[1] << 8));
}

std::int16_t readS16(const std::uint8_t* data) {
    return static_cast<std::int16_t>(readU16(data));
}

std::uint32_t readU32(const std::uint8_t* data) {
    return static_cast<std::uint32_t>(data[0]
        | (data[1] << 8)
        | (data[2] << 16)
        | (data[3] << 24));
}

hyperframe::dsp::RawStreamLoop readWavSmplLoop(const juce::File& file, juce::int64 regionStart, juce::int64 regionLength) {
    juce::MemoryBlock block;
    if (!file.loadFileAsData(block) || block.getSize() < 12) {
        return {};
    }

    const auto* data = static_cast<const std::uint8_t*>(block.getData());
    if (!fourccEquals(data, "RIFF") || !fourccEquals(data + 8, "WAVE")) {
        return {};
    }

    const auto size = block.getSize();
    auto offset = std::size_t { 12 };
    while (offset + 8 <= size) {
        const auto chunkSize = static_cast<std::size_t>(readU32(data + offset + 4));
        const auto payload = offset + 8;
        if (payload + chunkSize > size) {
            break;
        }

        if (fourccEquals(data + offset, "smpl") && chunkSize >= 60) {
            const auto loopCount = readU32(data + payload + 28);
            for (std::uint32_t loopIndex = 0; loopIndex < loopCount; ++loopIndex) {
                const auto loopOffset = payload + 36 + (static_cast<std::size_t>(loopIndex) * 24);
                if (loopOffset + 24 > payload + chunkSize) {
                    break;
                }

                const auto loopType = readU32(data + loopOffset + 4);
                if (loopType != 0) {
                    continue;
                }

                const auto absoluteStart = static_cast<juce::int64>(readU32(data + loopOffset + 8));
                const auto absoluteEndInclusive = static_cast<juce::int64>(readU32(data + loopOffset + 12));
                const auto relativeStart = absoluteStart - regionStart;
                const auto relativeEnd = absoluteEndInclusive - regionStart + 1;
                if (relativeStart >= 0 && relativeEnd <= regionLength && relativeEnd > relativeStart + 1) {
                    return { true,
                        static_cast<std::size_t>(relativeStart),
                        static_cast<std::size_t>(relativeEnd) };
                }
            }
        }

        offset = payload + chunkSize + (chunkSize & 1u);
    }

    return {};
}

AudioSourceRegion readAudioSourceRegion(const juce::File& file, const HyperFrameAudioProcessor::AudioSourceImportOptions& options) {
    juce::AudioFormatManager formatManager;
    formatManager.registerBasicFormats();

    std::unique_ptr<juce::AudioFormatReader> reader(formatManager.createReaderFor(file));
    if (reader == nullptr || reader->lengthInSamples < 8 || reader->numChannels == 0) {
        return {};
    }

    const auto sourceLength = reader->lengthInSamples;
    const auto startSample = std::clamp(
        static_cast<juce::int64>(std::round(std::max(0.0, options.startMs) * reader->sampleRate / 1000.0)),
        juce::int64 { 0 },
        sourceLength - 1);
    const auto requestedLength = options.lengthMs > 0.0
        ? static_cast<juce::int64>(std::round(options.lengthMs * reader->sampleRate / 1000.0))
        : sourceLength - startSample;
    const auto samplesToRead = std::clamp(requestedLength, juce::int64 { 0 }, sourceLength - startSample);
    if (samplesToRead < 8) {
        return {};
    }

    AudioSourceRegion region;
    region.sampleRate = reader->sampleRate;
    region.bitsPerSample = std::clamp(static_cast<int>(reader->bitsPerSample),
        hyperframe::dsp::WaveTable::kMinBitDepth,
        hyperframe::dsp::WaveTable::kMaxBitDepth);
    region.loop = readWavSmplLoop(file, startSample, samplesToRead);
    region.samples.assign(static_cast<std::size_t>(samplesToRead), 0.0f);

    juce::AudioBuffer<float> buffer(static_cast<int>(reader->numChannels),
        static_cast<int>(std::min<juce::int64>(samplesToRead, kAudioReadChunkSize)));
    const auto channelScale = 1.0f / static_cast<float>(reader->numChannels);
    auto samplesRead = juce::int64 { 0 };
    while (samplesRead < samplesToRead) {
        const auto chunkSamples = static_cast<int>(std::min<juce::int64>(kAudioReadChunkSize, samplesToRead - samplesRead));
        if (buffer.getNumSamples() != chunkSamples) {
            buffer.setSize(static_cast<int>(reader->numChannels), chunkSamples, false, false, true);
        }

        if (!reader->read(&buffer, 0, chunkSamples, startSample + samplesRead, true, true)) {
            return {};
        }

        for (int channel = 0; channel < static_cast<int>(reader->numChannels); ++channel) {
            const auto* source = buffer.getReadPointer(channel);
            for (int sample = 0; sample < chunkSamples; ++sample) {
                region.samples[static_cast<std::size_t>(samplesRead + sample)] += source[sample] * channelScale;
            }
        }

        samplesRead += chunkSamples;
    }

    return region;
}

juce::String readSf2Name(const std::uint8_t* data, std::size_t length) {
    std::string text;
    text.reserve(length);
    for (std::size_t i = 0; i < length && data[i] != 0; ++i) {
        text.push_back(static_cast<char>(data[i]));
    }
    return juce::String::fromUTF8(text.c_str()).trim();
}

std::vector<Sf2Chunk> listRiffChunks(const std::uint8_t* data, std::size_t start, std::size_t end, const char* chunkId) {
    std::vector<Sf2Chunk> chunks;
    auto offset = start;
    while (offset + 8 <= end) {
        const auto size = static_cast<std::size_t>(readU32(data + offset + 4));
        const auto payload = offset + 8;
        const auto next = payload + size + (size & 1u);
        if (payload + size > end) {
            break;
        }

        if (fourccEquals(data + offset, chunkId)) {
            chunks.push_back({ payload, size });
        }
        offset = next;
    }
    return chunks;
}

bool findSoundFontChunks(const juce::MemoryBlock& block, Sf2Chunk& smpl, Sf2Chunk& phdr, Sf2Chunk& pbag, Sf2Chunk& pgen, Sf2Chunk& inst, Sf2Chunk& ibag, Sf2Chunk& igen, Sf2Chunk& shdr) {
    if (block.getSize() < 16) {
        return false;
    }

    const auto* data = static_cast<const std::uint8_t*>(block.getData());
    if (!fourccEquals(data, "RIFF") || !fourccEquals(data + 8, "sfbk")) {
        return false;
    }

    auto offset = std::size_t { 12 };
    const auto riffEnd = std::min<std::size_t>(block.getSize(), 8u + static_cast<std::size_t>(readU32(data + 4)));
    while (offset + 12 <= riffEnd) {
        const auto size = static_cast<std::size_t>(readU32(data + offset + 4));
        const auto payload = offset + 8;
        const auto next = payload + size + (size & 1u);
        if (payload + size > block.getSize() || !fourccEquals(data + offset, "LIST") || size < 4) {
            offset = next;
            continue;
        }

        const auto listData = payload + 4;
        const auto listEnd = payload + size;
        if (fourccEquals(data + payload, "sdta")) {
            for (const auto& chunk : listRiffChunks(data, listData, listEnd, "smpl")) {
                smpl = chunk;
            }
        } else if (fourccEquals(data + payload, "pdta")) {
            for (const auto& chunk : listRiffChunks(data, listData, listEnd, "phdr")) phdr = chunk;
            for (const auto& chunk : listRiffChunks(data, listData, listEnd, "pbag")) pbag = chunk;
            for (const auto& chunk : listRiffChunks(data, listData, listEnd, "pgen")) pgen = chunk;
            for (const auto& chunk : listRiffChunks(data, listData, listEnd, "inst")) inst = chunk;
            for (const auto& chunk : listRiffChunks(data, listData, listEnd, "ibag")) ibag = chunk;
            for (const auto& chunk : listRiffChunks(data, listData, listEnd, "igen")) igen = chunk;
            for (const auto& chunk : listRiffChunks(data, listData, listEnd, "shdr")) shdr = chunk;
        }

        offset = next;
    }

    return smpl.size > 0 && phdr.size >= 76 && pbag.size >= 8 && pgen.size >= 4
        && inst.size >= 44 && ibag.size >= 8 && igen.size >= 4 && shdr.size >= 92;
}

std::vector<Sf2PresetHeader> parsePresetHeaders(const std::uint8_t* data, Sf2Chunk chunk) {
    std::vector<Sf2PresetHeader> result;
    for (std::size_t offset = chunk.offset; offset + 38 <= chunk.offset + chunk.size; offset += 38) {
        result.push_back({ readSf2Name(data + offset, 20), static_cast<int>(readU16(data + offset + 20)),
            static_cast<int>(readU16(data + offset + 22)), static_cast<int>(readU16(data + offset + 24)) });
    }
    return result;
}

std::vector<Sf2Bag> parseBags(const std::uint8_t* data, Sf2Chunk chunk) {
    std::vector<Sf2Bag> result;
    for (std::size_t offset = chunk.offset; offset + 4 <= chunk.offset + chunk.size; offset += 4) {
        result.push_back({ static_cast<int>(readU16(data + offset)) });
    }
    return result;
}

std::vector<Sf2Generator> parseGenerators(const std::uint8_t* data, Sf2Chunk chunk) {
    std::vector<Sf2Generator> result;
    for (std::size_t offset = chunk.offset; offset + 4 <= chunk.offset + chunk.size; offset += 4) {
        result.push_back({ static_cast<int>(readU16(data + offset)), static_cast<int>(readS16(data + offset + 2)) });
    }
    return result;
}

std::vector<Sf2Instrument> parseInstruments(const std::uint8_t* data, Sf2Chunk chunk) {
    std::vector<Sf2Instrument> result;
    for (std::size_t offset = chunk.offset; offset + 22 <= chunk.offset + chunk.size; offset += 22) {
        result.push_back({ readSf2Name(data + offset, 20), static_cast<int>(readU16(data + offset + 20)) });
    }
    return result;
}

std::vector<Sf2SampleHeader> parseSampleHeaders(const std::uint8_t* data, Sf2Chunk chunk) {
    std::vector<Sf2SampleHeader> result;
    for (std::size_t offset = chunk.offset; offset + 46 <= chunk.offset + chunk.size; offset += 46) {
        result.push_back({ readSf2Name(data + offset, 20), readU32(data + offset + 20), readU32(data + offset + 24),
            readU32(data + offset + 28), readU32(data + offset + 32), readU32(data + offset + 36),
            static_cast<int>(data[offset + 40]), static_cast<int>(static_cast<std::int8_t>(data[offset + 41])),
            static_cast<int>(readU16(data + offset + 44)) });
    }
    return result;
}

void applySoundFontEnvelopeGenerator(Sf2Envelope& envelope, const Sf2Generator& generator);

struct Sf2InstrumentChoice {
    int instrumentIndex = -1;
    int keyCenter = 60;
    juce::String name;
    Sf2Envelope envelope {};
};

bool generatorHasInstrument(const std::vector<Sf2Generator>& generators, int start, int end) {
    for (int gen = start; gen < end; ++gen) {
        if (generators[static_cast<std::size_t>(gen)].operatorId == 41) {
            return true;
        }
    }

    return false;
}

int sf2KeyRangeCenter(int amount) {
    const auto range = static_cast<std::uint16_t>(amount);
    const auto low = std::clamp(static_cast<int>(range & 0xff), 0, 127);
    const auto high = std::clamp(static_cast<int>((range >> 8) & 0xff), low, 127);
    return (low + high) / 2;
}

std::vector<Sf2InstrumentChoice> findInstrumentChoicesForPreset(const Sf2PresetHeader& preset,
                                                                const Sf2PresetHeader& nextPreset,
                                                                const std::vector<Sf2Bag>& presetBags,
                                                                const std::vector<Sf2Generator>& presetGenerators,
                                                                const std::vector<Sf2Instrument>& instruments) {
    const auto startBag = std::clamp(preset.bagIndex, 0, static_cast<int>(presetBags.size()) - 1);
    const auto endBag = std::clamp(nextPreset.bagIndex, startBag, static_cast<int>(presetBags.size()) - 1);
    auto globalEnvelope = Sf2Envelope {};
    auto globalKeyCenter = 60;
    std::vector<Sf2InstrumentChoice> choices;
    for (int bag = startBag; bag < endBag; ++bag) {
        const auto genStart = std::clamp(presetBags[static_cast<std::size_t>(bag)].generatorIndex, 0, static_cast<int>(presetGenerators.size()));
        const auto genEnd = std::clamp(presetBags[static_cast<std::size_t>(bag + 1)].generatorIndex, genStart, static_cast<int>(presetGenerators.size()));
        if (!generatorHasInstrument(presetGenerators, genStart, genEnd)) {
            for (int gen = genStart; gen < genEnd; ++gen) {
                const auto& generator = presetGenerators[static_cast<std::size_t>(gen)];
                if (generator.operatorId == 43) {
                    globalKeyCenter = sf2KeyRangeCenter(generator.amount);
                } else {
                    applySoundFontEnvelopeGenerator(globalEnvelope, generator);
                }
            }
            continue;
        }

        auto envelope = globalEnvelope;
        auto keyCenter = globalKeyCenter;
        auto instrumentIndex = -1;
        for (int gen = genStart; gen < genEnd; ++gen) {
            const auto& generator = presetGenerators[static_cast<std::size_t>(gen)];
            if (generator.operatorId == 41) {
                instrumentIndex = generator.amount;
            } else if (generator.operatorId == 43) {
                keyCenter = sf2KeyRangeCenter(generator.amount);
            } else {
                applySoundFontEnvelopeGenerator(envelope, generator);
            }
        }

        if (instrumentIndex >= 0 && instrumentIndex + 1 < static_cast<int>(instruments.size())) {
            choices.push_back({ instrumentIndex,
                keyCenter,
                instruments[static_cast<std::size_t>(instrumentIndex)].name,
                envelope });
        }
    }
    return choices;
}

float soundFontTimecentsToSeconds(int timecents) {
    if (timecents <= -32768) {
        return 0.0f;
    }

    return std::clamp(std::pow(2.0f, static_cast<float>(timecents) / 1200.0f), 0.0f, 10.0f);
}

float soundFontSustainToLevel(int centibels) {
    if (centibels <= 0) {
        return 1.0f;
    }

    return std::clamp(std::pow(10.0f, -static_cast<float>(centibels) / 200.0f), 0.0f, 1.0f);
}

void applySoundFontEnvelopeGenerator(Sf2Envelope& envelope, const Sf2Generator& generator) {
    switch (generator.operatorId) {
    case 34:
        envelope.attack = soundFontTimecentsToSeconds(generator.amount);
        break;
    case 36:
        envelope.decay = soundFontTimecentsToSeconds(generator.amount);
        break;
    case 37:
        envelope.sustain = soundFontSustainToLevel(generator.amount);
        break;
    case 38:
        envelope.release = soundFontTimecentsToSeconds(generator.amount);
        break;
    default:
        break;
    }
}

juce::String simplifiedSoundFontName(juce::String name) {
    name = name.trim()
               .replaceCharacter('_', ' ')
               .replaceCharacter('-', ' ')
               .replaceCharacter('.', ' ');
    while (name.contains("  ")) {
        name = name.replace("  ", " ");
    }
    return name.trim();
}

bool isSoundFontPitchToken(const std::string& token) {
    if (token.size() < 2 || token.size() > 4) {
        return false;
    }

    const auto letter = static_cast<char>(std::tolower(static_cast<unsigned char>(token[0])));
    if (letter < 'a' || letter > 'g') {
        return false;
    }

    auto index = std::size_t { 1 };
    if (index < token.size() && (token[index] == '#' || token[index] == 'b')) {
        ++index;
    }
    if (index >= token.size()) {
        return false;
    }
    while (index < token.size()) {
        if (!std::isdigit(static_cast<unsigned char>(token[index]))) {
            return false;
        }
        ++index;
    }
    return true;
}

bool isNumericSoundFontToken(const std::string& token) {
    return !token.empty()
        && std::all_of(token.begin(), token.end(), [](char ch) {
               return std::isdigit(static_cast<unsigned char>(ch));
           });
}

bool isTechnicalSoundFontName(const juce::String& name) {
    const auto simplified = simplifiedSoundFontName(name).toStdString();
    std::vector<std::string> tokens;
    auto token = std::string {};
    for (const auto ch : simplified) {
        if (std::isspace(static_cast<unsigned char>(ch))) {
            if (!token.empty()) {
                tokens.push_back(std::move(token));
                token.clear();
            }
            continue;
        }
        token.push_back(ch);
    }
    if (!token.empty()) {
        tokens.push_back(std::move(token));
    }
    if (tokens.empty()) {
        return true;
    }

    const auto lower = juce::String(simplified).toLowerCase();
    if (lower.contains("vel") || lower.contains("velocity")) {
        return true;
    }
    if (isSoundFontPitchToken(tokens.front())) {
        return tokens.size() == 1
            || std::all_of(tokens.begin() + 1, tokens.end(), isNumericSoundFontToken);
    }

    return false;
}

int soundFontNameCost(const juce::String& name) {
    const auto simplified = simplifiedSoundFontName(name);
    if (simplified.isEmpty()) {
        return 10000;
    }

    auto cost = simplified.length() * 4;
    const auto lower = simplified.toLowerCase();
    auto letterCount = 0;
    auto digitCount = 0;
    if (lower == "sample" || lower == "loop" || lower == "instrument" || lower == "untitled"
        || lower.startsWith("sample ") || lower.startsWith("zone ")) {
        cost += 4000;
    }
    if (isTechnicalSoundFontName(simplified)) {
        cost += 3000;
    }
    if (lower.contains("loop")) {
        cost += 20;
    }
    if (lower.contains("sample") || lower.contains("samp")) {
        cost += 30;
    }
    if (lower.contains("zone") || lower.contains("layer")) {
        cost += 40;
    }

    for (const auto ch : simplified.toStdString()) {
        const auto value = static_cast<unsigned char>(ch);
        if (std::isalpha(value)) {
            ++letterCount;
        }
        if (std::isdigit(value)) {
            ++digitCount;
            cost += 2;
        } else if (!std::isalnum(value) && !std::isspace(value)) {
            cost += 4;
        }
    }
    if (letterCount == 0) {
        cost += 3000;
    }
    if (digitCount > 0 && letterCount <= 2 && simplified.length() <= 4) {
        cost += 1500;
    }

    return cost;
}

juce::String bestSoundFontName(const juce::String& presetName, const juce::String& instrumentName, const juce::String& sampleName) {
    auto bestName = simplifiedSoundFontName(presetName);
    auto bestCost = soundFontNameCost(bestName);
    for (const auto& name : { instrumentName, sampleName }) {
        const auto simplified = simplifiedSoundFontName(name);
        const auto cost = soundFontNameCost(simplified);
        if (cost < bestCost) {
            bestName = simplified;
            bestCost = cost;
        }
    }
    return bestName.isNotEmpty() ? bestName : presetName;
}

bool generatorHasSampleId(const std::vector<Sf2Generator>& generators, int start, int end) {
    for (int gen = start; gen < end; ++gen) {
        if (generators[static_cast<std::size_t>(gen)].operatorId == 53) {
            return true;
        }
    }

    return false;
}

struct Sf2SampleChoice {
    int sampleIndex = -1;
    int rootNote = 60;
    int keyCenter = 60;
    Sf2Envelope envelope {};
};

std::vector<Sf2SampleChoice> findSampleChoicesForInstrument(const Sf2InstrumentChoice& instrumentChoice,
                                                            const std::vector<Sf2Instrument>& instruments,
                                                            const std::vector<Sf2Bag>& instrumentBags,
                                                            const std::vector<Sf2Generator>& instrumentGenerators,
                                                            const std::vector<Sf2SampleHeader>& sampleHeaders) {
    const auto instrumentIndex = instrumentChoice.instrumentIndex;
    if (instrumentIndex < 0 || instrumentIndex + 1 >= static_cast<int>(instruments.size())) {
        return {};
    }

    std::vector<Sf2SampleChoice> choices;
    const auto startBag = std::clamp(instruments[static_cast<std::size_t>(instrumentIndex)].bagIndex, 0, static_cast<int>(instrumentBags.size()) - 1);
    const auto endBag = std::clamp(instruments[static_cast<std::size_t>(instrumentIndex + 1)].bagIndex, startBag, static_cast<int>(instrumentBags.size()) - 1);
    auto globalEnvelope = instrumentChoice.envelope;
    auto globalKeyCenter = instrumentChoice.keyCenter;
    for (int bag = startBag; bag < endBag; ++bag) {
        const auto genStart = std::clamp(instrumentBags[static_cast<std::size_t>(bag)].generatorIndex, 0, static_cast<int>(instrumentGenerators.size()));
        const auto genEnd = std::clamp(instrumentBags[static_cast<std::size_t>(bag + 1)].generatorIndex, genStart, static_cast<int>(instrumentGenerators.size()));
        if (!generatorHasSampleId(instrumentGenerators, genStart, genEnd)) {
            for (int gen = genStart; gen < genEnd; ++gen) {
                const auto& generator = instrumentGenerators[static_cast<std::size_t>(gen)];
                if (generator.operatorId == 43) {
                    globalKeyCenter = sf2KeyRangeCenter(generator.amount);
                } else {
                    applySoundFontEnvelopeGenerator(globalEnvelope, generator);
                }
            }
            continue;
        }

        auto envelope = globalEnvelope;
        auto sampleIndex = -1;
        auto overridingRootKey = -1;
        auto keyCenter = globalKeyCenter;
        for (int gen = genStart; gen < genEnd; ++gen) {
            const auto& generator = instrumentGenerators[static_cast<std::size_t>(gen)];
            if (generator.operatorId == 53) {
                sampleIndex = generator.amount;
            } else if (generator.operatorId == 58) {
                overridingRootKey = generator.amount;
            } else if (generator.operatorId == 43) {
                keyCenter = sf2KeyRangeCenter(generator.amount);
            } else {
                applySoundFontEnvelopeGenerator(envelope, generator);
            }
        }

        if (sampleIndex < 0 || sampleIndex + 1 >= static_cast<int>(sampleHeaders.size())) {
            continue;
        }

        const auto& sample = sampleHeaders[static_cast<std::size_t>(sampleIndex)];
        const auto root = std::clamp(overridingRootKey >= 0 ? overridingRootKey : sample.originalPitch, 0, 127);
        choices.push_back({ sampleIndex, root, keyCenter, envelope });
    }

    return choices;
}

std::vector<SoundFontProgram> parseSoundFontPrograms(const juce::File& file) {
    juce::MemoryBlock block;
    if (!file.loadFileAsData(block)) {
        return {};
    }

    Sf2Chunk smpl, phdr, pbag, pgen, inst, ibag, igen, shdr;
    if (!findSoundFontChunks(block, smpl, phdr, pbag, pgen, inst, ibag, igen, shdr)) {
        return {};
    }

    const auto* data = static_cast<const std::uint8_t*>(block.getData());
    const auto presetHeaders = parsePresetHeaders(data, phdr);
    const auto presetBags = parseBags(data, pbag);
    const auto presetGenerators = parseGenerators(data, pgen);
    const auto instruments = parseInstruments(data, inst);
    const auto instrumentBags = parseBags(data, ibag);
    const auto instrumentGenerators = parseGenerators(data, igen);
    const auto sampleHeaders = parseSampleHeaders(data, shdr);
    if (presetHeaders.size() < 2 || instruments.size() < 2 || sampleHeaders.size() < 2) {
        return {};
    }

    const auto sampleDataCount = smpl.size / sizeof(std::int16_t);
    std::map<std::vector<std::int16_t>, std::size_t> convertedSampleKeys;
    std::vector<SoundFontProgram> programs;
    std::vector<int> programScores;
    for (std::size_t presetIndex = 0; presetIndex + 1 < presetHeaders.size(); ++presetIndex) {
        const auto& preset = presetHeaders[presetIndex];
        if (preset.name.isEmpty() || preset.name.equalsIgnoreCase("EOP")) {
            continue;
        }

        auto bestProgram = SoundFontProgram {};
        auto bestSampleKey = std::vector<std::int16_t> {};
        auto bestScore = -1000000000;
        const auto instrumentChoices = findInstrumentChoicesForPreset(preset, presetHeaders[presetIndex + 1], presetBags, presetGenerators, instruments);
        for (const auto& instrumentChoice : instrumentChoices) {
            const auto sampleChoices = findSampleChoicesForInstrument(instrumentChoice,
                instruments,
                instrumentBags,
                instrumentGenerators,
                sampleHeaders);
            for (const auto& choice : sampleChoices) {
                const auto& sample = sampleHeaders[static_cast<std::size_t>(choice.sampleIndex)];
                if (sample.end <= sample.start + 8 || sample.end > sampleDataCount || sample.sampleRate == 0) {
                    continue;
                }

                const auto sampleLength = static_cast<std::size_t>(sample.end - sample.start);
                const auto hasConvertibleWholeSample = sampleLength >= hyperframe::dsp::WaveTable::kMinActiveLength
                    && sampleLength <= hyperframe::dsp::WaveTable::kMaxPoints;
                const auto hasConvertibleLoop = sample.startLoop >= sample.start
                    && sample.endLoop >= sample.startLoop + hyperframe::dsp::WaveTable::kMinActiveLength
                    && sample.endLoop <= sample.end
                    && (sample.endLoop - sample.startLoop) <= hyperframe::dsp::WaveTable::kMaxPoints;
                if (!hasConvertibleWholeSample && !hasConvertibleLoop) {
                    continue;
                }

                const auto usesLoop = hasConvertibleLoop;
                const auto sourceStart = usesLoop ? sample.startLoop : sample.start;
                const auto cycleLength = usesLoop
                    ? static_cast<std::size_t>(sample.endLoop - sample.startLoop)
                    : sampleLength;
                if (sourceStart + cycleLength > sampleDataCount) {
                    continue;
                }

                const auto displayName = bestSoundFontName(preset.name, instrumentChoice.name, sample.name);
                const auto score = (usesLoop ? 100000 : 50000)
                    - (std::abs(choice.keyCenter - 60) * 24)
                    - (std::abs(choice.rootNote - 60) * 12)
                    - std::abs(static_cast<int>(cycleLength) - 32)
                    - soundFontNameCost(displayName);
                if (score <= bestScore) {
                    continue;
                }

                auto sampleKey = std::vector<std::int16_t>(cycleLength);
                for (std::size_t index = 0; index < sampleKey.size(); ++index) {
                    sampleKey[index] = readS16(data + smpl.offset + ((static_cast<std::size_t>(sourceStart) + index) * sizeof(std::int16_t)));
                }

                SoundFontProgram program;
                program.name = displayName;
                program.cycle.resize(cycleLength);
                for (std::size_t index = 0; index < cycleLength; ++index) {
                    program.cycle[index] = std::clamp(static_cast<float>(sampleKey[index]) / 32768.0f, -1.0f, 1.0f);
                }
                program.sampleRate = static_cast<double>(sample.sampleRate);
                program.attack = choice.envelope.attack;
                program.decay = choice.envelope.decay;
                program.sustain = choice.envelope.sustain;
                program.release = choice.envelope.release;
                if (usesLoop) {
                    program.loop.enabled = true;
                    program.loop.start = 0;
                    program.loop.end = cycleLength;
                }

                bestProgram = std::move(program);
                bestSampleKey = std::move(sampleKey);
                bestScore = score;
            }
        }

        if (bestScore <= -1000000000 || bestSampleKey.empty()) {
            continue;
        }

        const auto existing = convertedSampleKeys.find(bestSampleKey);
        if (existing != convertedSampleKeys.end()) {
            const auto programIndex = existing->second;
            if (bestScore > programScores[programIndex]) {
                programs[programIndex] = std::move(bestProgram);
                programScores[programIndex] = bestScore;
            }
            continue;
        }

        const auto programIndex = programs.size();
        convertedSampleKeys.emplace(std::move(bestSampleKey), programIndex);
        programScores.push_back(bestScore);
        programs.push_back(std::move(bestProgram));
    }

    return programs;
}

} // namespace

struct HyperFrameAudioProcessor::Program {
    const char* name = "";
    hyperframe::dsp::WaveTable::Shape shape = hyperframe::dsp::WaveTable::Shape::Square;
    int length = 32;
    int bits = 4;
    float lsdjPhase = 0.0f;
    hyperframe::dsp::LsdjPhaseMode lsdjPhaseMode = hyperframe::dsp::LsdjPhaseMode::Normal;
    float attack = 0.002f;
    float decay = 0.120f;
    float sustain = 0.85f;
    float release = 0.080f;
    float gain = 0.5f;
    bool interpolation = false;
    const char* waveRamHex = nullptr;
    MotionStyle motionStyle = MotionStyle::Off;
    hyperframe::dsp::CommandSettings::EngineMode engineMode = hyperframe::dsp::CommandSettings::EngineMode::Draw;
    bool motionTableEnabled = false;
    bool motionLoop = false;
    float motionRate = 12.0f;
    int motionSteps = 8;
    bool motionClockBpm = false;
    float slideTime = 0.0f;
};

HyperFrameAudioProcessor::HyperFrameAudioProcessor()
    : AudioProcessor(BusesProperties().withOutput("Output", juce::AudioChannelSet::stereo(), true)),
      parameters_(*this, nullptr, "PARAMETERS", createParameterLayout()) {
    for (int frame = 0; frame < hyperframe::dsp::kWaveFrameCount; ++frame) {
        auto& waveFrame = authoringWaveFrame(frame);
        waveFrame.setActiveLength(32);
        waveFrame.setBitDepth(4);
        waveFrame.generate(static_cast<hyperframe::dsp::WaveTable::Shape>(frame % 5), static_cast<unsigned int>(frame + 1));
    }

    for (int step = 0; step < hyperframe::dsp::kCommandStepCount; ++step) {
        motionTable_[static_cast<std::size_t>(step)].frame = -1;
        motionTable_[static_cast<std::size_t>(step)].level = 1.0f;
        engine_.setCommandStep(step, motionTable_[static_cast<std::size_t>(step)]);
    }

    engine_.setSelectedWaveFrame(0);

    suppressHostNotification_ = true;
    setCurrentProgram(0);
    suppressHostNotification_ = false;
    addStrictParameterListeners();
}

HyperFrameAudioProcessor::~HyperFrameAudioProcessor() {
    cancelPendingUpdate();
    removeStrictParameterListeners();
}

void HyperFrameAudioProcessor::prepareToPlay(double sampleRate, int samplesPerBlock) {
    juce::ignoreUnused(samplesPerBlock);
    engine_.setSampleRate(sampleRate);
    engine_.reset();
}

void HyperFrameAudioProcessor::releaseResources() {}

bool HyperFrameAudioProcessor::isBusesLayoutSupported(const BusesLayout& layouts) const {
    const auto& output = layouts.getMainOutputChannelSet();
    return output == juce::AudioChannelSet::mono() || output == juce::AudioChannelSet::stereo();
}

void HyperFrameAudioProcessor::processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midiMessages) {
    juce::ScopedNoDenormals noDenormals;
    applyPendingSourceSnapshot();
    if (getParamValue(parameters_, kParamMotionClockMode) > 0.5f) {
        updateHostTempo();
    }
    updateEngineParameters();
    keyboardState_.processNextMidiBuffer(midiMessages, 0, buffer.getNumSamples(), true);
    buffer.clear();

    auto midiIterator = midiMessages.cbegin();
    const auto midiEnd = midiMessages.cend();
    auto hasMidi = midiIterator != midiEnd;
    auto metadata = hasMidi ? *midiIterator : juce::MidiMessageMetadata {};
    auto nextMidiSample = hasMidi ? metadata.samplePosition : buffer.getNumSamples();

    for (int sample = 0; sample < buffer.getNumSamples(); ++sample) {
        while (hasMidi && nextMidiSample == sample) {
            const auto message = metadata.getMessage();
            if (message.isNoteOn()) {
                engine_.noteOn(message.getNoteNumber(), message.getFloatVelocity());
            } else if (message.isNoteOff()) {
                engine_.noteOff(message.getNoteNumber());
            } else if (message.isAllNotesOff() || message.isAllSoundOff()) {
                engine_.reset();
            } else if (message.isPitchWheel()) {
                const float bend = (static_cast<float>(message.getPitchWheelValue()) - 8192.0f) / 8192.0f * 2.0f;
                engine_.setPitchBend(bend);
            }

            ++midiIterator;
            hasMidi = midiIterator != midiEnd;
            metadata = hasMidi ? *midiIterator : juce::MidiMessageMetadata {};
            nextMidiSample = hasMidi ? metadata.samplePosition : buffer.getNumSamples();
        }

        const auto value = engine_.renderSample();
        for (int channel = 0; channel < buffer.getNumChannels(); ++channel) {
            buffer.setSample(channel, sample, value);
        }
    }
}

juce::AudioProcessorEditor* HyperFrameAudioProcessor::createEditor() {
    return new HyperFrameAudioProcessorEditor(*this);
}

bool HyperFrameAudioProcessor::hasEditor() const {
    return true;
}

const juce::String HyperFrameAudioProcessor::getName() const {
    return "HyperFrame";
}

bool HyperFrameAudioProcessor::acceptsMidi() const {
    return true;
}

bool HyperFrameAudioProcessor::producesMidi() const {
    return false;
}

bool HyperFrameAudioProcessor::isMidiEffect() const {
    return false;
}

double HyperFrameAudioProcessor::getTailLengthSeconds() const {
    return 0.0;
}

int HyperFrameAudioProcessor::getNumPrograms() {
    return static_cast<int>(programs().size() + importedPrograms_.size());
}

int HyperFrameAudioProcessor::getCurrentProgram() {
    return std::max(0, currentProgram_);
}

void HyperFrameAudioProcessor::setCurrentProgram(int index) {
    const auto clampedIndex = std::clamp(index, 0, getNumPrograms() - 1);
    if (clampedIndex >= static_cast<int>(programs().size())) {
        currentProgram_ = clampedIndex;
        setImportedProgram(importedPrograms_[static_cast<std::size_t>(clampedIndex - static_cast<int>(programs().size()))]);
        return;
    }

    const auto& program = programs()[static_cast<std::size_t>(clampedIndex)];
    currentProgram_ = clampedIndex;

    {
        const juce::ScopedLock lock(waveTableLock_);
        clearRawSourceLocked();
    }

    setParameterValue(kParamWaveLength, static_cast<float>(program.length));
    setParameterValue(kParamWaveBits, static_cast<float>(program.bits));
    setParameterValue(kParamSelectedFrame, 1.0f);
    setParameterValue(kParamInterpolation, program.interpolation ? 1.0f : 0.0f);
    setParameterValue(kParamAttack, program.attack);
    setParameterValue(kParamDecay, program.decay);
    setParameterValue(kParamSustain, program.sustain);
    setParameterValue(kParamRelease, program.release);
    setParameterValue(kParamLsdjPhase, program.lsdjPhase);
    setParameterValue(kParamLsdjPhaseMode, static_cast<float>(lsdjPhaseModeIndex(program.lsdjPhaseMode)));
    setParameterValue(kParamSlideTime, program.slideTime);
    setParameterValue(kParamMotionTable, program.motionTableEnabled ? 1.0f : 0.0f);
    setParameterValue(kParamMotionLoop, program.motionLoop ? 1.0f : 0.0f);
    setParameterValue(kParamMotionClockMode, program.motionClockBpm ? 1.0f : 0.0f);
    setParameterValue(kParamMotionRate, program.motionRate);
    setParameterValue(kParamMotionSteps, static_cast<float>(program.motionSteps));
    setParameterValue(kParamMotionLoopStart, 0.0f);
    setParameterValue(kParamEngineMode, static_cast<float>(engineModeIndex(program.engineMode)));
    setParameterValue(kParamMonoMode, hyperframe::dsp::isHardwareWaveEngineMode(program.engineMode) ? 1.0f : 0.0f);
    setParameterValue(kParamGain, program.gain);

    for (int stepIndex = 0; stepIndex < hyperframe::dsp::kCommandStepCount; ++stepIndex) {
        const auto step = presetMotionStep(program.motionStyle, stepIndex);
        const auto frameCommand = step.frame + 1;
        const auto stepLevel = hyperframe::dsp::isGameBoyWaveEngineMode(program.engineMode)
            ? lsdjWaveVolumeParameter(step.level)
            : step.level;
        setMotionStepValues(stepIndex, static_cast<float>(frameCommand), step.pitchSemitones, 0.0f, step.phaseAmount, 0.0f, stepLevel);
    }

    const juce::ScopedLock lock(waveTableLock_);
    for (int frame = 0; frame < hyperframe::dsp::kWaveFrameCount; ++frame) {
        auto& waveFrame = authoringWaveFrame(frame);
        waveFrame.setActiveLength(static_cast<std::size_t>(program.length));
        waveFrame.setSampleFormat(sampleFormatForEngineMode(program.engineMode));
        waveFrame.setBitDepth(program.bits);
        if (hyperframe::dsp::isGameBoyWaveEngineMode(program.engineMode)) {
            writeGbWavePulseDefaultFrame(waveFrame, frame);
            continue;
        }

        waveFrame.generate(shapeForFrame(program.shape, frame, program.motionStyle), waveSeed_++ + static_cast<unsigned int>(frame * 17));
        if (frame % 3 == 1) {
            waveFrame.smooth();
        } else if (frame % 5 == 2) {
            waveFrame.normalize();
        }
    }

    if (program.waveRamHex != nullptr) {
        writeGbWaveRamHexToFrame(authoringWaveFrame(0), program.waveRamHex);
    }
    publishSourceSnapshotLocked();
}

const juce::String HyperFrameAudioProcessor::getProgramName(int index) {
    if (index < 0 || index >= getNumPrograms()) {
        return {};
    }

    const auto factoryCount = static_cast<int>(programs().size());
    if (index >= factoryCount) {
        return importedPrograms_[static_cast<std::size_t>(index - factoryCount)].name;
    }

    return programs()[static_cast<std::size_t>(index)].name;
}

void HyperFrameAudioProcessor::changeProgramName(int index, const juce::String& newName) {
    const auto factoryCount = static_cast<int>(programs().size());
    if (index < factoryCount || newName.isEmpty()) return;
    importedPrograms_[static_cast<std::size_t>(index - factoryCount)].name = newName;
}

void HyperFrameAudioProcessor::getStateInformation(juce::MemoryBlock& destData) {
    auto state = parameters_.copyState();
    const auto factoryCount = static_cast<int>(programs().size());
    state.setProperty(kCurrentProgramProperty, currentProgram_ < factoryCount ? currentProgram_ : -1, nullptr);
    state.setProperty(kSelectedMotionStepProperty, selectedMotionStep_, nullptr);
    for (int child = state.getNumChildren() - 1; child >= 0; --child) {
        if (state.getChild(child).hasType(kWaveTableStateType)
            || state.getChild(child).hasType(kWaveFramesStateType)
            || state.getChild(child).hasType(kRawStreamStateType)) {
            state.removeChild(child, nullptr);
        }
    }

    state.addChild(createWaveTableState(), -1, nullptr);
    state.addChild(createRawStreamState(), -1, nullptr);

    juce::MemoryOutputStream stream(destData, true);
    state.writeToStream(stream);
}

void HyperFrameAudioProcessor::setStateInformation(const void* data, int sizeInBytes) {
    if (auto tree = juce::ValueTree::readFromData(data, static_cast<size_t>(sizeInBytes)); tree.isValid()) {
        migrateLegacyMotionState(tree);
        currentProgram_ = static_cast<int>(tree.getProperty(kCurrentProgramProperty, currentProgram_));
        currentProgram_ = currentProgram_ < 0 ? -1 : std::clamp(currentProgram_, 0, getNumPrograms() - 1);
        selectedMotionStep_ = std::clamp(
            static_cast<int>(tree.getProperty(kSelectedMotionStepProperty, selectedMotionStep_)),
            0,
            hyperframe::dsp::kCommandStepCount - 1);
        parameters_.replaceState(tree);
        restoreWaveTableState(tree);
        restoreRawStreamState(tree);
        enforceWaveStrictParameters();
        updateEngineParameters();
    }
}

bool HyperFrameAudioProcessor::exportPresetFile(const juce::File& file) {
    juce::MemoryBlock presetData;
    getStateInformation(presetData);
    return file.replaceWithData(presetData.getData(), presetData.getSize());
}

bool HyperFrameAudioProcessor::importPresetFile(const juce::File& file) {
    juce::MemoryBlock presetData;
    if (!file.loadFileAsData(presetData) || presetData.isEmpty()) {
        return false;
    }

    if (auto tree = juce::ValueTree::readFromData(presetData.getData(), presetData.getSize()); !tree.isValid()) {
        return false;
    }

    setStateInformation(presetData.getData(), static_cast<int>(presetData.getSize()));
    return true;
}

bool HyperFrameAudioProcessor::importSoundFontBank(const juce::File& file) {
    const auto programsFromFile = parseSoundFontPrograms(file);
    if (programsFromFile.empty()) {
        return false;
    }

    importedPrograms_.clear();
    importedPrograms_.reserve(programsFromFile.size());
    for (auto program : programsFromFile) {
        ImportedProgram imported;
        imported.name = file.getFileNameWithoutExtension() + " / " + program.name;
        imported.cycle = std::move(program.cycle);
        imported.sampleRate = program.sampleRate;
        imported.attack = program.attack;
        imported.decay = program.decay;
        imported.sustain = program.sustain;
        imported.release = program.release;
        imported.loop = program.loop;
        importedPrograms_.push_back(std::move(imported));
    }

    setCurrentProgram(static_cast<int>(programs().size()));
    return true;
}

bool HyperFrameAudioProcessor::importLsdjBank(const juce::File& file) {
    const auto programsFromFile = parseLsdjPrograms(file);
    if (programsFromFile.empty()) {
        return false;
    }

    importedPrograms_.clear();
    importedPrograms_.reserve(programsFromFile.size());
    for (const auto& program : programsFromFile) {
        ImportedProgram imported;
        imported.kind = ImportedProgramKind::LsdjWaveFrames;
        imported.name = "LSDJ / " + program.name;
        imported.waveRamFrames = program.frames;
        imported.waveFrameCount = program.frameCount;
        imported.waveMotionFrames = program.motionFrames;
        imported.waveMotionPitch = program.motionPitch;
        imported.waveMotionPitchBend = program.motionPitchBend;
        imported.waveMotionPhase = program.motionPhase;
        imported.waveMotionVibratoRate = program.motionVibratoRate;
        imported.waveMotionLevel = program.motionLevel;
        imported.waveMotionSteps = program.motionSteps;
        imported.waveMotionLoopStart = program.motionLoopStart;
        imported.waveMotionEnabled = program.motionEnabled;
        imported.waveMotionLoop = program.motionLoop;
        imported.waveMotionRate = program.motionRate;
        imported.waveLevel = program.level;
        imported.waveSelectedFrame = program.selectedFrame;
        importedPrograms_.push_back(std::move(imported));
    }

    setCurrentProgram(static_cast<int>(programs().size()));
    return true;
}

void HyperFrameAudioProcessor::setImportedProgram(const ImportedProgram& program) {
    if (program.kind == ImportedProgramKind::LsdjWaveFrames) {
        const auto frameCount = std::clamp(program.waveFrameCount, 1, hyperframe::dsp::kWaveFrameCount);
        const auto motionSteps = std::clamp(program.waveMotionSteps > 0 ? program.waveMotionSteps : frameCount, 1, hyperframe::dsp::kCommandStepCount);
        {
            const juce::ScopedLock lock(waveTableLock_);
            clearRawSourceLocked();
            publishSourceSnapshotLocked();
        }

        setParameterValue(kParamEngineMode, static_cast<float>(engineModeIndex(hyperframe::dsp::CommandSettings::EngineMode::Wave)));
        setParameterValue(kParamWaveLength, 32.0f);
        setParameterValue(kParamWaveBits, 4.0f);
        setParameterValue(kParamInterpolation, 0.0f);
        setParameterValue(kParamAttack, 0.0f);
        setParameterValue(kParamDecay, 0.0f);
        setParameterValue(kParamSustain, 1.0f);
        setParameterValue(kParamRelease, 0.0f);
        setParameterValue(kParamGain, 0.50f);
        setParameterValue(kParamSelectedFrame, static_cast<float>(std::clamp(program.waveSelectedFrame, 1, hyperframe::dsp::kWaveFrameCount)));
        setParameterValue(kParamLsdjPhase, 0.0f);
        setParameterValue(kParamLsdjPhaseMode, static_cast<float>(lsdjPhaseModeIndex(hyperframe::dsp::LsdjPhaseMode::Normal)));
        setParameterValue(kParamSlideTime, 0.0f);
        setParameterValue(kParamMotionTable, program.waveMotionEnabled ? 1.0f : 0.0f);
        setParameterValue(kParamMotionLoop, program.waveMotionLoop ? 1.0f : 0.0f);
        setParameterValue(kParamMotionClockMode, 0.0f);
        setParameterValue(kParamMotionRate, std::clamp(program.waveMotionRate, 1.0f, 64.0f));
        setParameterValue(kParamMotionSteps, static_cast<float>(motionSteps));
        setParameterValue(kParamMotionLoopStart, static_cast<float>(std::clamp(program.waveMotionLoopStart, 0, motionSteps - 1)));
        setParameterValue(kParamMonoMode, 1.0f);

        {
            const juce::ScopedLock lock(waveTableLock_);
            for (int frame = 0; frame < hyperframe::dsp::kWaveFrameCount; ++frame) {
                writeGbWaveRamBytesToFrame(authoringWaveFrame(frame), program.waveRamFrames[static_cast<std::size_t>(frame % frameCount)]);
            }
            publishSourceSnapshotLocked();
        }

        for (int stepIndex = 0; stepIndex < hyperframe::dsp::kCommandStepCount; ++stepIndex) {
            const auto frame = stepIndex < motionSteps
                ? std::clamp(program.waveMotionFrames[static_cast<std::size_t>(stepIndex)], 0, hyperframe::dsp::kWaveFrameCount - 1)
                : -1;
            setMotionStepValues(stepIndex,
                static_cast<float>(frame + 1),
                std::clamp(program.waveMotionPitch[static_cast<std::size_t>(stepIndex)], -24.0f, 24.0f),
                std::clamp(program.waveMotionPitchBend[static_cast<std::size_t>(stepIndex)], -96.0f, 96.0f),
                std::clamp(program.waveMotionPhase[static_cast<std::size_t>(stepIndex)], -31.0f, 31.0f),
                std::clamp(program.waveMotionVibratoRate[static_cast<std::size_t>(stepIndex)], 0.0f, 64.0f),
                lsdjWaveVolumeParameter(program.waveMotionLevel[static_cast<std::size_t>(stepIndex)]));
        }
        return;
    }

    if (program.cycle.size() < hyperframe::dsp::WaveTable::kMinActiveLength || program.cycle.size() > hyperframe::dsp::WaveTable::kMaxPoints) {
        return;
    }

    {
        const juce::ScopedLock lock(waveTableLock_);
        clearRawSourceLocked();
        publishSourceSnapshotLocked();
    }

    setParameterValue(kParamEngineMode, static_cast<float>(engineModeIndex(hyperframe::dsp::CommandSettings::EngineMode::Draw)));
    setParameterValue(kParamWaveLength, static_cast<float>(program.cycle.size()));
    setParameterValue(kParamWaveBits, static_cast<float>(hyperframe::dsp::WaveTable::kMaxBitDepth));
    setParameterValue(kParamInterpolation, 0.0f);
    setParameterValue(kParamAttack, program.attack);
    setParameterValue(kParamDecay, program.decay);
    setParameterValue(kParamSustain, program.sustain);
    setParameterValue(kParamRelease, program.release);
    setParameterValue(kParamGain, 0.50f);
    setParameterValue(kParamSelectedFrame, 1.0f);
    setParameterValue(kParamSlideTime, 0.0f);
    setParameterValue(kParamMotionTable, 0.0f);
    setParameterValue(kParamMotionLoop, 0.0f);
    setParameterValue(kParamMotionSteps, 1.0f);
    setParameterValue(kParamMotionLoopStart, 0.0f);
    setParameterValue(kParamMonoMode, 0.0f);
    for (int stepIndex = 0; stepIndex < hyperframe::dsp::kCommandStepCount; ++stepIndex) {
        setMotionStepValues(stepIndex, 1.0f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f);
    }

    const juce::ScopedLock lock(waveTableLock_);
    clearRawSourceLocked();
    auto& waveFrame = authoringWaveFrame(0);
    waveFrame.setActiveLength(program.cycle.size());
    waveFrame.setSampleFormat(hyperframe::dsp::WaveTable::SampleFormat::Signed);
    waveFrame.setBitDepth(hyperframe::dsp::WaveTable::kMaxBitDepth);
    waveFrame.setLoop(program.loop.enabled, program.loop.start, program.loop.end);
    for (std::size_t point = 0; point < program.cycle.size(); ++point) {
        waveFrame.setPoint(sourceIndexForActivePoint(point, program.cycle.size()), program.cycle[point]);
    }
    publishSourceSnapshotLocked();
}

juce::AudioProcessorValueTreeState& HyperFrameAudioProcessor::parameters() {
    return parameters_;
}

const hyperframe::dsp::WaveTable& HyperFrameAudioProcessor::waveTable() const {
    return authoringWaveFrame(editableWaveFrameIndex());
}

float HyperFrameAudioProcessor::waveDisplayPoint(std::size_t index, std::size_t activeLength, int bitDepth) const {
    const juce::ScopedLock lock(waveTableLock_);
    return authoringWaveFrame(editableWaveFrameIndex()).displayPoint(index, activeLength, bitDepth);
}

bool HyperFrameAudioProcessor::hasRawStream() const {
    const juce::ScopedLock lock(waveTableLock_);
    return !rawStream_.empty();
}

bool HyperFrameAudioProcessor::rawStreamDisplayRanges(std::size_t bucketCount, std::vector<float>& minimums, std::vector<float>& maximums) const {
    minimums.assign(bucketCount, 0.0f);
    maximums.assign(bucketCount, 0.0f);
    if (bucketCount == 0) {
        return false;
    }

    const juce::ScopedLock lock(waveTableLock_);
    if (rawStream_.empty()) {
        return false;
    }

    const auto sampleCount = rawStream_.size();
    for (std::size_t bucket = 0; bucket < bucketCount; ++bucket) {
        const auto start = std::min((bucket * sampleCount) / bucketCount, sampleCount - 1);
        const auto end = std::max(start + 1, std::min(((bucket + 1) * sampleCount) / bucketCount, sampleCount));

        auto minimum = 1.0f;
        auto maximum = -1.0f;
        for (auto index = start; index < end; ++index) {
            const auto value = hyperframe::dsp::WaveTable::quantizeValue(
                rawStream_[index],
                static_cast<int>(getParamValue(parameters_, kParamWaveBits)),
                hyperframe::dsp::WaveTable::SampleFormat::Signed);
            minimum = std::min(minimum, value);
            maximum = std::max(maximum, value);
        }

        minimums[bucket] = minimum;
        maximums[bucket] = maximum;
    }

    return true;
}

void HyperFrameAudioProcessor::setWaveDisplayPoint(std::size_t index, std::size_t activeLength, float value) {
    const juce::ScopedLock lock(waveTableLock_);
    auto& waveFrame = authoringWaveFrame(editableWaveFrameIndex());
    syncAuthoringWaveFrameProfileLocked(waveFrame);
    waveFrame.setPoint(sourceIndexForActivePoint(index, activeLength), value);
    publishSourceSnapshotLocked();
}

void HyperFrameAudioProcessor::beginWaveEdit() {
    const juce::ScopedLock lock(waveTableLock_);
    waveEditSnapshot_ = waveFrames_;
    waveEditPending_ = true;
}

void HyperFrameAudioProcessor::commitWaveEdit() {
    if (!waveEditPending_) return;
    waveEditPending_ = false;
    waveUndoStack_.push_back(waveEditSnapshot_);
    if (static_cast<int>(waveUndoStack_.size()) > kMaxWaveUndoDepth)
        waveUndoStack_.pop_front();
    waveRedoStack_.clear();
}

bool HyperFrameAudioProcessor::canUndoWaveEdit() const {
    return !waveUndoStack_.empty();
}

bool HyperFrameAudioProcessor::canRedoWaveEdit() const {
    return !waveRedoStack_.empty();
}

void HyperFrameAudioProcessor::undoWaveEdit() {
    if (waveUndoStack_.empty()) return;
    const juce::ScopedLock lock(waveTableLock_);
    waveRedoStack_.push_back(waveFrames_);
    waveFrames_ = waveUndoStack_.back();
    waveUndoStack_.pop_back();
    publishSourceSnapshotLocked();
}

void HyperFrameAudioProcessor::redoWaveEdit() {
    if (waveRedoStack_.empty()) return;
    const juce::ScopedLock lock(waveTableLock_);
    waveUndoStack_.push_back(waveFrames_);
    waveFrames_ = waveRedoStack_.back();
    waveRedoStack_.pop_back();
    publishSourceSnapshotLocked();
}

void HyperFrameAudioProcessor::copyCurrentFrame() {
    const juce::ScopedLock lock(waveTableLock_);
    frameClipboard_ = authoringWaveFrame(editableWaveFrameIndex());
}

void HyperFrameAudioProcessor::pasteToCurrentFrame() {
    if (!frameClipboard_) return;
    beginWaveEdit();
    {
        const juce::ScopedLock lock(waveTableLock_);
        authoringWaveFrame(editableWaveFrameIndex()) = *frameClipboard_;
        publishSourceSnapshotLocked();
    }
    commitWaveEdit();
}

bool HyperFrameAudioProcessor::hasFrameClipboard() const {
    return frameClipboard_.has_value();
}

int HyperFrameAudioProcessor::currentMotionStep() const {
    return engine_.currentMotionStep();
}

void HyperFrameAudioProcessor::applyWaveShape(hyperframe::dsp::WaveTable::Shape shape) {
    beginWaveEdit();
    {
        const juce::ScopedLock lock(waveTableLock_);
        auto& waveFrame = authoringWaveFrame(editableWaveFrameIndex());
        waveFrame.setActiveLength(static_cast<std::size_t>(getParamValue(parameters_, kParamWaveLength)));
        waveFrame.setSampleFormat(sampleFormatForEngineMode(engineModeFromIndex(static_cast<int>(getParamValue(parameters_, kParamEngineMode)))));
        waveFrame.setBitDepth(static_cast<int>(getParamValue(parameters_, kParamWaveBits)));
        const auto seed = (shape == hyperframe::dsp::WaveTable::Shape::Noise) ? waveSeed_++ : waveSeed_;
        waveFrame.generate(shape, seed);
        publishSourceSnapshotLocked();
    }
    commitWaveEdit();
}

void HyperFrameAudioProcessor::normalizeWaveTable() {
    beginWaveEdit();
    {
        const juce::ScopedLock lock(waveTableLock_);
        auto& waveFrame = authoringWaveFrame(editableWaveFrameIndex());
        syncAuthoringWaveFrameProfileLocked(waveFrame);
        waveFrame.normalize();
        publishSourceSnapshotLocked();
    }
    commitWaveEdit();
}

void HyperFrameAudioProcessor::smoothWaveTable() {
    beginWaveEdit();
    {
        const juce::ScopedLock lock(waveTableLock_);
        auto& waveFrame = authoringWaveFrame(editableWaveFrameIndex());
        syncAuthoringWaveFrameProfileLocked(waveFrame);
        waveFrame.smooth();
        publishSourceSnapshotLocked();
    }
    commitWaveEdit();
}

bool HyperFrameAudioProcessor::convertRawStreamToDrawExact() {
    std::vector<float> cycle;
    {
        const juce::ScopedLock lock(waveTableLock_);
        if (rawStream_.empty()) {
            return false;
        }

        auto start = std::size_t { 0 };
        auto end = rawStream_.size();
        if (rawStreamLoop_.enabled && rawStreamLoop_.end > rawStreamLoop_.start) {
            start = std::min(rawStreamLoop_.start, rawStream_.size());
            end = std::min(rawStreamLoop_.end, rawStream_.size());
        }

        const auto length = end > start ? end - start : std::size_t { 0 };
        if (length < hyperframe::dsp::WaveTable::kMinActiveLength || length > hyperframe::dsp::WaveTable::kMaxPoints) {
            return false;
        }

        cycle.assign(rawStream_.begin() + static_cast<std::ptrdiff_t>(start),
            rawStream_.begin() + static_cast<std::ptrdiff_t>(end));
    }

    setParameterValue(kParamEngineMode, static_cast<float>(engineModeIndex(hyperframe::dsp::CommandSettings::EngineMode::Draw)));
    setParameterValue(kParamWaveLength, static_cast<float>(cycle.size()));
    setParameterValue(kParamWaveBits, static_cast<float>(hyperframe::dsp::WaveTable::kMaxBitDepth));
    setParameterValue(kParamInterpolation, 0.0f);
    setParameterValue(kParamSelectedFrame, 1.0f);
    setParameterValue(kParamMotionTable, 0.0f);
    setParameterValue(kParamMotionLoop, 0.0f);
    setParameterValue(kParamMotionLoopStart, 0.0f);

    const juce::ScopedLock lock(waveTableLock_);
    clearRawSourceLocked();
    auto& waveFrame = authoringWaveFrame(0);
    waveFrame.setActiveLength(cycle.size());
    waveFrame.setSampleFormat(hyperframe::dsp::WaveTable::SampleFormat::Signed);
    waveFrame.setBitDepth(hyperframe::dsp::WaveTable::kMaxBitDepth);
    waveFrame.clearLoop();
    for (std::size_t point = 0; point < cycle.size(); ++point) {
        waveFrame.setPoint(sourceIndexForActivePoint(point, cycle.size()), cycle[point]);
    }
    publishSourceSnapshotLocked();

    return true;
}

bool HyperFrameAudioProcessor::importAudioSourceFile(const juce::File& file, const AudioSourceImportOptions& options) {
    auto region = readAudioSourceRegion(file, options);
    if (region.samples.empty()) {
        return false;
    }

    if (options.target == AudioSourceImportTarget::RawStream) {
        {
            const juce::ScopedLock lock(waveTableLock_);
            rawStream_ = region.samples;
            rawStreamSnapshotDirty_ = true;
            rawStreamHoldInterpolation_ = false;
            rawStreamRate_ = region.sampleRate;
            rawStreamSourceRootNote_ = kPitchReferenceMidiNote;
            rawStreamRootNote_ = kPitchReferenceMidiNote;
            rawStreamLoop_ = region.loop;
            publishSourceSnapshotLocked();
        }
        setRawStreamPlaybackDefaults();
        setParameterValue(kParamWaveBits, static_cast<float>(region.bitsPerSample));
        return true;
    }

    const auto writeWaveFrames = [this, &region, &options]() {
        auto samples = region.samples;
        if (options.normalize) {
            normalizeSamples(samples);
        }

        setParameterValue(kParamEngineMode, static_cast<float>(engineModeIndex(hyperframe::dsp::CommandSettings::EngineMode::Wave)));
        setParameterValue(kParamWaveLength, 32.0f);
        setParameterValue(kParamWaveBits, 4.0f);
        setParameterValue(kParamInterpolation, 0.0f);
        setParameterValue(kParamMonoMode, 1.0f);
        setParameterValue(kParamSelectedFrame, 1.0f);
        setParameterValue(kParamMotionTable, 1.0f);
        setParameterValue(kParamMotionLoop, 1.0f);
        setParameterValue(kParamMotionSteps, static_cast<float>(hyperframe::dsp::kWaveFrameCount));
        setParameterValue(kParamMotionLoopStart, 0.0f);
        setParameterValue(kParamMotionRate, 12.0f);

        setSequentialMotionStepDefaults();

        const juce::ScopedLock lock(waveTableLock_);
        clearRawSourceLocked();
        const auto sourceSpan = samples.size() > 1 ? static_cast<float>(samples.size() - 1) : 0.0f;
        for (int frame = 0; frame < hyperframe::dsp::kWaveFrameCount; ++frame) {
            auto& waveFrame = authoringWaveFrame(frame);
            waveFrame.setActiveLength(32);
            waveFrame.setSampleFormat(hyperframe::dsp::WaveTable::SampleFormat::Unsigned);
            waveFrame.setBitDepth(4);
            waveFrame.clearLoop();

            for (std::size_t point = 0; point < 32; ++point) {
                const auto packedIndex = (static_cast<float>(frame) * 32.0f) + static_cast<float>(point);
                const auto position = sourceSpan * packedIndex / static_cast<float>((hyperframe::dsp::kWaveFrameCount * 32) - 1);
                const auto value = sampleLinear(samples, position);
                waveFrame.setPoint(sourceIndexForActivePoint(point, 32), value);
            }
        }
        publishSourceSnapshotLocked();
    };

    if (options.target == AudioSourceImportTarget::WaveFrames) {
        writeWaveFrames();
        enforceWaveStrictParameters();
        return true;
    }

    const auto cycle = buildSingleCycleFromSamples(std::move(region.samples), { options.normalize, options.zeroCrossingSnap });
    if (cycle.empty()) {
        return false;
    }

    setParameterValue(kParamEngineMode, static_cast<float>(engineModeIndex(hyperframe::dsp::CommandSettings::EngineMode::Draw)));
    setParameterValue(kParamWaveLength, static_cast<float>(hyperframe::dsp::WaveTable::kMaxPoints));
    setParameterValue(kParamWaveBits, static_cast<float>(region.bitsPerSample));
    setParameterValue(kParamInterpolation, 1.0f);
    setParameterValue(kParamSelectedFrame, 1.0f);
    setParameterValue(kParamMotionTable, 0.0f);
    setParameterValue(kParamMotionLoop, 0.0f);
    setParameterValue(kParamMotionLoopStart, 0.0f);

    const juce::ScopedLock lock(waveTableLock_);
    clearRawSourceLocked();
    auto& waveFrame = authoringWaveFrame(0);
    waveFrame.setActiveLength(hyperframe::dsp::WaveTable::kMaxPoints);
    waveFrame.setSampleFormat(hyperframe::dsp::WaveTable::SampleFormat::Unsigned);
    waveFrame.setBitDepth(region.bitsPerSample);
    waveFrame.clearLoop();
    for (std::size_t point = 0; point < cycle.size(); ++point) {
        waveFrame.setPoint(point, cycle[point]);
    }
    publishSourceSnapshotLocked();

    return true;
}

bool HyperFrameAudioProcessor::importBytebeat(const BytebeatImportOptions& options) {
    const auto expression = options.expression.trim().toStdString();
    const auto loopTicks = std::clamp<std::uint32_t>(options.loopTicks, 8, 1048576);
    const auto startTick = options.startTick;
    const auto clockHz = std::clamp(options.sampleRate, 1.0, 2000000.0);

    std::vector<float> samples;
    if (!hyperframe::dsp::renderBytebeatTicks(expression, startTick, loopTicks, samples)) {
        return false;
    }

    if (options.target == BytebeatImportTarget::RawStream) {
        const auto sourceFrequency = clockHz / static_cast<double>(loopTicks);
        const auto sourceRootNote = midiNoteForFrequency(sourceFrequency);
        const auto playbackRootKey = playableRootKeyForSourceNote(sourceRootNote);

        {
            const juce::ScopedLock lock(waveTableLock_);
            rawStream_ = samples;
            rawStreamSnapshotDirty_ = true;
            rawStreamHoldInterpolation_ = true;
            rawStreamRate_ = clockHz;
            rawStreamSourceRootNote_ = sourceRootNote;
            rawStreamRootNote_ = static_cast<double>(playbackRootKey);
            rawStreamLoop_.enabled = true;
            rawStreamLoop_.start = 0;
            rawStreamLoop_.end = samples.size();
            publishSourceSnapshotLocked();
        }
        setRawStreamPlaybackDefaults();
        setParameterValue(kParamWaveBits, 8.0f);
        return true;
    }

    const auto frameBitDepth = 8;
    const auto frameFormat = sampleFormatForEngineMode(hyperframe::dsp::CommandSettings::EngineMode::Draw);
    const auto selectedStarts = selectBytebeatFrameStarts(samples, clockHz, options.frameStrideMs);

    setParameterValue(kParamEngineMode, static_cast<float>(engineModeIndex(hyperframe::dsp::CommandSettings::EngineMode::Draw)));
    setParameterValue(kParamWaveLength, 32.0f);
    setParameterValue(kParamWaveBits, static_cast<float>(frameBitDepth));
    setParameterValue(kParamInterpolation, 0.0f);
    setParameterValue(kParamMonoMode, 0.0f);
    setParameterValue(kParamSelectedFrame, 1.0f);
    setParameterValue(kParamMotionTable, 1.0f);
    setParameterValue(kParamMotionLoop, 1.0f);
    setParameterValue(kParamMotionSteps, static_cast<float>(hyperframe::dsp::kWaveFrameCount));
    setParameterValue(kParamMotionLoopStart, 0.0f);
    setParameterValue(kParamMotionRate, 12.0f);

    setSequentialMotionStepDefaults();

    {
        const juce::ScopedLock lock(waveTableLock_);
        clearRawSourceLocked();
        for (int frame = 0; frame < hyperframe::dsp::kWaveFrameCount; ++frame) {
            auto& waveFrame = authoringWaveFrame(frame);
            waveFrame.setActiveLength(32);
            waveFrame.setSampleFormat(frameFormat);
            waveFrame.setBitDepth(frameBitDepth);
            waveFrame.clearLoop();

            const auto frameStart = selectedStarts[static_cast<std::size_t>(frame)];
            for (std::size_t point = 0; point < 32; ++point) {
                waveFrame.setPoint(sourceIndexForActivePoint(point, 32), bytebeatFramePoint(samples, frameStart, point, options.normalizeFrames));
            }
        }
        publishSourceSnapshotLocked();
    }

    return true;
}

HyperFrameAudioProcessor::LoopInfo HyperFrameAudioProcessor::rawStreamLoopInfo() const {
    const juce::ScopedLock lock(waveTableLock_);
    LoopInfo info;
    info.hasContent = !rawStream_.empty();
    info.sampleCount = rawStream_.size();
    info.enabled = rawStreamLoop_.enabled;
    info.start = std::min(rawStreamLoop_.start, rawStream_.size());
    info.end = std::min(rawStreamLoop_.end, rawStream_.size());
    info.playStart = std::min(rawStreamLoop_.playStart, rawStream_.size());
    return info;
}

HyperFrameAudioProcessor::LoopInfo HyperFrameAudioProcessor::drawLoopInfo() const {
    const juce::ScopedLock lock(waveTableLock_);
    const auto& waveFrame = authoringWaveFrame(editableWaveFrameIndex());
    const auto engineMode = engineModeFromIndex(static_cast<int>(getParamValue(parameters_, kParamEngineMode)));
    LoopInfo info;
    info.sampleCount = hyperframe::dsp::isHardwareWaveEngineMode(engineMode)
        ? static_cast<std::size_t>(hyperframe::dsp::hardwareWaveLength(engineMode))
        : static_cast<std::size_t>(getParamValue(parameters_, kParamWaveLength));
    info.hasContent = info.sampleCount > 0;
    info.enabled = waveFrame.loopEnabled() && waveFrame.loopEnd() <= info.sampleCount;
    info.start = std::min(waveFrame.loopStart(), info.sampleCount);
    info.end = std::min(waveFrame.loopEnd(), info.sampleCount);
    return info;
}

void HyperFrameAudioProcessor::setRawStreamLoop(bool enabled, std::size_t start, std::size_t end, std::size_t playStart) {
    const juce::ScopedLock lock(waveTableLock_);
    if (rawStream_.empty()) {
        rawStreamLoop_ = {};
        publishSourceSnapshotLocked();
        return;
    }

    start = std::min(start, rawStream_.size() - 1);
    end = std::min(end, rawStream_.size());
    playStart = std::min(playStart, rawStream_.size() - 1);
    if (end <= start + 1) {
        end = std::min(rawStream_.size(), start + 2);
    }

    rawStreamLoop_.enabled = enabled && end > start + 1;
    rawStreamLoop_.start = start;
    rawStreamLoop_.end = end;
    rawStreamLoop_.playStart = playStart;
    publishSourceSnapshotLocked();
}

juce::String HyperFrameAudioProcessor::exportGameBoyWaveRamHex() const {
    juce::String hex;
    const juce::ScopedLock lock(waveTableLock_);

    for (std::size_t byteIndex = 0; byteIndex < 16; ++byteIndex) {
        const auto& waveFrame = authoringWaveFrame(editableWaveFrameIndex());
        const auto high = gbWaveRamNibbleFromValue(waveFrame.displayPoint(byteIndex * 2, 32, 4));
        const auto low = gbWaveRamNibbleFromValue(waveFrame.displayPoint((byteIndex * 2) + 1, 32, 4));
        hex += juce::String::toHexString((high << 4) | low).paddedLeft('0', 2).toUpperCase();
    }

    return hex;
}

void HyperFrameAudioProcessor::setEngineModeWithProfile(int modeIndex) {
    const auto mode = engineModeFromIndex(modeIndex);
    setParameterValue(kParamEngineMode, static_cast<float>(engineModeIndex(mode)));
    applyEngineModeProfile(mode);
}

const std::vector<HyperFrameAudioProcessor::Program>& HyperFrameAudioProcessor::programs() {
    using Mode = hyperframe::dsp::CommandSettings::EngineMode;
    using Phase = hyperframe::dsp::LsdjPhaseMode;

    // Preset convention: hardware wave modes keep instant gate; Draw defaults leave a tiny release for click control.
    static const std::vector<Program> presetPrograms {
        { "Init", hyperframe::dsp::WaveTable::Shape::Sine, 32, 4, 0.0f, Phase::Normal, 0.002f, 0.120f, 0.85f, 0.080f, 0.50f, false, nullptr, MotionStyle::Off, Mode::Draw, false, false, 12.0f, 8 },
        { "Draw Noise Tick", hyperframe::dsp::WaveTable::Shape::Noise, 16, 3, 0.0f, Phase::Normal, 0.000f, 0.060f, 0.00f, 0.020f, 0.34f, false, nullptr, MotionStyle::NoiseBurst, Mode::Draw, true, false, 52.0f, 5 },
        { "Wave GB Pulse", hyperframe::dsp::WaveTable::Shape::Square, 32, 4, 0.0f, Phase::Normal, 0.000f, 0.000f, 1.000f, 0.000f, 0.50f, false, nullptr, MotionStyle::Off, Mode::Wave, false, false, 12.0f, 8 },
        { "Wave Duty Cycle", hyperframe::dsp::WaveTable::Shape::Square, 32, 4, 0.0f, Phase::Normal, 0.000f, 0.000f, 1.000f, 0.000f, 0.50f, false, nullptr, MotionStyle::GbPulseDuty, Mode::Wave, true, true, 32.0f, 2 },
        { "Wave Organ", hyperframe::dsp::WaveTable::Shape::Sine, 32, 4, 0.0f, Phase::Normal, 0.000f, 0.000f, 1.000f, 0.000f, 0.50f, false, "89ACEECA86421123589ACEECA8642112", MotionStyle::Off, Mode::Wave, false, false, 12.0f, 8 },
        { "Wave Hollow", hyperframe::dsp::WaveTable::Shape::Sine, 32, 4, 0.0f, Phase::Normal, 0.000f, 0.000f, 1.000f, 0.000f, 0.50f, false, "8BDFFFFDB8520002358BDFFFFDB85200", MotionStyle::Off, Mode::Wave, false, false, 12.0f, 8 },
        { "WS Glass Sweep", hyperframe::dsp::WaveTable::Shape::Sine, 32, 4, 5.0f, Phase::Resync, 0.000f, 0.000f, 1.000f, 0.000f, 0.45f, false, nullptr, MotionStyle::WsSweep, Mode::WonderSwan, true, true, 18.0f, 8 },
        { "WS Step Keys", hyperframe::dsp::WaveTable::Shape::Square, 32, 4, 2.0f, Phase::Normal, 0.000f, 0.000f, 1.000f, 0.000f, 0.42f, false, nullptr, MotionStyle::WsStepArp, Mode::WonderSwan, true, true, 14.0f, 8 },
        { "WS Thin Organ", hyperframe::dsp::WaveTable::Shape::Triangle, 32, 4, 1.0f, Phase::Resync2, 0.000f, 0.000f, 1.000f, 0.000f, 0.40f, false, nullptr, MotionStyle::PadShimmer, Mode::WonderSwan, true, true, 7.0f, 8 },
        { "PCE Bell Sweep", hyperframe::dsp::WaveTable::Shape::Triangle, 32, 5, 3.0f, Phase::Resync, 0.000f, 0.000f, 1.000f, 0.000f, 0.48f, false, nullptr, MotionStyle::PceBellLfo, Mode::PcEngine, true, true, 16.0f, 8 },
        { "PCE DDA Bass", hyperframe::dsp::WaveTable::Shape::Saw, 32, 5, 1.0f, Phase::Normal, 0.000f, 0.000f, 1.000f, 0.000f, 0.52f, false, nullptr, MotionStyle::PceDdaPluck, Mode::PcEngine, true, false, 28.0f, 8 },
        { "PCE Hollow Keys", hyperframe::dsp::WaveTable::Shape::Sine, 32, 5, 4.0f, Phase::Resync2, 0.000f, 0.000f, 1.000f, 0.000f, 0.44f, false, nullptr, MotionStyle::PingPong, Mode::PcEngine, true, true, 12.0f, 8 },
        { "SCC Lead Stack", hyperframe::dsp::WaveTable::Shape::Saw, 32, 8, 2.0f, Phase::Resync2, 0.000f, 0.000f, 1.000f, 0.000f, 0.42f, false, nullptr, MotionStyle::SccEchoLead, Mode::Scc, true, true, 12.0f, 16 },
        { "SCC Oct Echo", hyperframe::dsp::WaveTable::Shape::Square, 32, 8, 1.0f, Phase::Resync, 0.000f, 0.000f, 1.000f, 0.000f, 0.38f, false, nullptr, MotionStyle::SccEchoLead, Mode::Scc, true, true, 9.0f, 16 },
        { "SCC Buzz Bass", hyperframe::dsp::WaveTable::Shape::Saw, 32, 8, 0.0f, Phase::Normal, 0.000f, 0.000f, 1.000f, 0.000f, 0.46f, false, nullptr, MotionStyle::SccBuzzStack, Mode::Scc, true, true, 18.0f, 8 },
    };

    return presetPrograms;
}

juce::AudioProcessorValueTreeState::ParameterLayout HyperFrameAudioProcessor::createParameterLayout() {
    using FloatAttributes = juce::AudioParameterFloatAttributes;

    const auto envelopeAttributes = FloatAttributes()
        .withStringFromValueFunction(envelopeTimeText)
        .withValueFromStringFunction(envelopeTimeValue);
    const auto percentAttributes = FloatAttributes()
        .withStringFromValueFunction(percentText)
        .withValueFromStringFunction(percentValue);
    const auto amplitudeAttributes = FloatAttributes()
        .withStringFromValueFunction(amplitudeStepText)
        .withValueFromStringFunction(amplitudeStepValue);
    const auto lsdjPhaseAttributes = FloatAttributes()
        .withStringFromValueFunction(lsdjPhaseText)
        .withValueFromStringFunction(lsdjPhaseValue);
    const auto signedIntAttributes = FloatAttributes()
        .withStringFromValueFunction(signedIntText)
        .withValueFromStringFunction(signedIntValue);
    const auto semitoneAttributes = FloatAttributes()
        .withStringFromValueFunction(semitoneText)
        .withValueFromStringFunction(semitoneValue);
    const auto hzAttributes = FloatAttributes()
        .withStringFromValueFunction(hzText)
        .withValueFromStringFunction(hzValue);
    const auto clockStepAttributes = FloatAttributes()
        .withStringFromValueFunction(integerText)
        .withValueFromStringFunction(integerValue);

    juce::AudioProcessorValueTreeState::ParameterLayout layout;

    auto waveGroup = std::make_unique<juce::AudioProcessorParameterGroup>("group_wave", "Wave", "|");
    waveGroup->addChild(std::make_unique<juce::AudioParameterChoice>(
        juce::ParameterID { kParamEngineMode, 1 }, "Engine Mode", juce::StringArray { "Raw", "Draw", "Wave", "WonderSwan", "PC Engine", "SCC" }, 1));
    waveGroup->addChild(std::make_unique<juce::AudioParameterInt>(
        juce::ParameterID { kParamWaveLength, 1 }, "Wave Length", 8, static_cast<int>(hyperframe::dsp::WaveTable::kMaxPoints), 32));
    waveGroup->addChild(std::make_unique<juce::AudioParameterInt>(
        juce::ParameterID { kParamWaveBits, 1 }, "Wave Bits", hyperframe::dsp::WaveTable::kMinBitDepth, hyperframe::dsp::WaveTable::kMaxBitDepth, hyperframe::dsp::WaveTable::kDefaultBitDepth));
    waveGroup->addChild(std::make_unique<juce::AudioParameterBool>(
        juce::ParameterID { kParamRawPlayFull, 1 }, "Raw Full Playback", false));
    waveGroup->addChild(std::make_unique<juce::AudioParameterBool>(
        juce::ParameterID { kParamInterpolation, 1 }, "Interpolation", false));
    waveGroup->addChild(std::make_unique<juce::AudioParameterInt>(
        juce::ParameterID { kParamSelectedFrame, 1 }, "Frame", 1, hyperframe::dsp::kWaveFrameCount, 1));
    layout.add(std::move(waveGroup));

    auto envGroup = std::make_unique<juce::AudioProcessorParameterGroup>("group_envelope", "Envelope", "|");
    envGroup->addChild(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID { kParamAttack, 1 }, "Attack", juce::NormalisableRange<float>(0.0f, 5.0f, kEnvelopeTimeStepSeconds), 0.002f, envelopeAttributes));
    envGroup->addChild(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID { kParamDecay, 1 }, "Decay", juce::NormalisableRange<float>(0.0f, 5.0f, kEnvelopeTimeStepSeconds), 0.120f, envelopeAttributes));
    envGroup->addChild(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID { kParamSustain, 1 }, "Sustain", juce::NormalisableRange<float>(0.0f, 1.0f, 0.01f), 0.85f, percentAttributes));
    envGroup->addChild(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID { kParamRelease, 1 }, "Release", juce::NormalisableRange<float>(0.0f, 10.0f, kEnvelopeTimeStepSeconds), 0.080f, envelopeAttributes));
    layout.add(std::move(envGroup));

    auto voiceGroup = std::make_unique<juce::AudioProcessorParameterGroup>("group_voice", "Voice", "|");
    voiceGroup->addChild(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID { kParamLsdjPhase, 1 }, "LSDJ Phase", juce::NormalisableRange<float>(0.0f, 31.0f, 1.0f), 0.0f, lsdjPhaseAttributes));
    voiceGroup->addChild(std::make_unique<juce::AudioParameterChoice>(
        juce::ParameterID { kParamLsdjPhaseMode, 1 }, "LSDJ Phase Mode", juce::StringArray { "Normal", "Resync", "Resyn2" }, 0));
    voiceGroup->addChild(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID { kParamSlideTime, 1 }, "Slide", juce::NormalisableRange<float>(0.0f, 2.0f, kEnvelopeTimeStepSeconds), 0.0f, envelopeAttributes));
    voiceGroup->addChild(std::make_unique<juce::AudioParameterBool>(
        juce::ParameterID { kParamMonoMode, 1 }, "Mono", false));
    voiceGroup->addChild(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID { kParamGain, 1 }, "Gain", juce::NormalisableRange<float>(0.0f, 1.5f, 0.001f), 0.5f));
    layout.add(std::move(voiceGroup));

    auto motionGroup = std::make_unique<juce::AudioProcessorParameterGroup>("group_motion", "Motion", "|");
    motionGroup->addChild(std::make_unique<juce::AudioParameterBool>(
        juce::ParameterID { kParamMotionTable, 1 }, "Motion Table", false));
    motionGroup->addChild(std::make_unique<juce::AudioParameterBool>(
        juce::ParameterID { kParamMotionLoop, 1 }, "Motion Loop", false));
    motionGroup->addChild(std::make_unique<juce::AudioParameterChoice>(
        juce::ParameterID { kParamMotionClockMode, 1 }, "Motion Clock", juce::StringArray { "Hz", "BPM" }, 0));
    motionGroup->addChild(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID { kParamMotionRate, 1 }, "Motion Rate", juce::NormalisableRange<float>(1.0f, 64.0f, 1.0f), 12.0f, clockStepAttributes));
    motionGroup->addChild(std::make_unique<juce::AudioParameterInt>(
        juce::ParameterID { kParamMotionSteps, 1 }, "Motion Steps", 1, hyperframe::dsp::kCommandStepCount, 8));
    motionGroup->addChild(std::make_unique<juce::AudioParameterInt>(
        juce::ParameterID { kParamMotionLoopStart, 1 }, "Motion Loop Start", 0, hyperframe::dsp::kCommandStepCount - 1, 0));
    layout.add(std::move(motionGroup));

    auto stepsGroup = std::make_unique<juce::AudioProcessorParameterGroup>("group_steps", "Motion Steps", "|");
    for (int step = 0; step < hyperframe::dsp::kCommandStepCount; ++step) {
        const auto stepLabel = "Step " + juce::String(step + 1) + " ";
        const auto stepId = "group_step_" + juce::String(step + 1);
        auto stepGroup = std::make_unique<juce::AudioProcessorParameterGroup>(stepId, "Step " + juce::String(step + 1), "|");
        stepGroup->addChild(std::make_unique<juce::AudioParameterInt>(
            juce::ParameterID { motionParamId(step, "frame"), 1 }, stepLabel + "Frame", 0, hyperframe::dsp::kWaveFrameCount, 0));
        stepGroup->addChild(std::make_unique<juce::AudioParameterFloat>(
            juce::ParameterID { motionParamId(step, "pitch"), 1 }, stepLabel + "Pitch", juce::NormalisableRange<float>(-24.0f, 24.0f, 1.0f), 0.0f, semitoneAttributes));
        stepGroup->addChild(std::make_unique<juce::AudioParameterFloat>(
            juce::ParameterID { motionParamId(step, "bend"), 1 }, stepLabel + "Bend", juce::NormalisableRange<float>(-96.0f, 96.0f, 1.0f), 0.0f, semitoneAttributes));
        stepGroup->addChild(std::make_unique<juce::AudioParameterFloat>(
            juce::ParameterID { motionParamId(step, "phase"), 1 }, stepLabel + "Phase", juce::NormalisableRange<float>(-31.0f, 31.0f, 1.0f), 0.0f, signedIntAttributes));
        stepGroup->addChild(std::make_unique<juce::AudioParameterFloat>(
            juce::ParameterID { motionParamId(step, "vib_rate"), 1 }, stepLabel + "Vibrato Rate", juce::NormalisableRange<float>(0.0f, 64.0f, 1.0f), 0.0f, hzAttributes));
        stepGroup->addChild(std::make_unique<juce::AudioParameterFloat>(
            juce::ParameterID { motionParamId(step, "level"), 1 }, stepLabel + "Level", juce::NormalisableRange<float>(0.0f, 1.0f, kGbAmplitudeStep), 1.0f, amplitudeAttributes));
        stepsGroup->addChild(std::move(stepGroup));
    }
    layout.add(std::move(stepsGroup));

    return layout;
}

void HyperFrameAudioProcessor::updateEngineParameters() {
    const auto engineMode = engineModeFromIndex(static_cast<int>(getParamValue(parameters_, kParamEngineMode)));
    const auto isHardwareWaveMode = hyperframe::dsp::isHardwareWaveEngineMode(engineMode);
    const auto selectedFrame = static_cast<int>(getParamValue(parameters_, kParamSelectedFrame)) - 1;
    engine_.setSelectedWaveFrame(selectedFrame);

    if (isHardwareWaveMode) {
        const auto hardwareLength = hyperframe::dsp::hardwareWaveLength(engineMode);
        const auto hardwareBits = hyperframe::dsp::hardwareWaveBitDepth(engineMode);
        for (int frame = 0; frame < hyperframe::dsp::kWaveFrameCount; ++frame) {
            auto& waveFrame = engine_.waveFrame(frame);
            waveFrame.setActiveLength(static_cast<std::size_t>(hardwareLength));
            waveFrame.setSampleFormat(sampleFormatForEngineMode(engineMode));
            waveFrame.setBitDepth(hardwareBits);
        }
    } else if (engineMode == hyperframe::dsp::CommandSettings::EngineMode::Draw) {
        for (int frame = 0; frame < hyperframe::dsp::kWaveFrameCount; ++frame) {
            auto& waveFrame = engine_.waveFrame(frame);
            waveFrame.setActiveLength(static_cast<std::size_t>(getParamValue(parameters_, kParamWaveLength)));
            waveFrame.setBitDepth(static_cast<int>(getParamValue(parameters_, kParamWaveBits)));
        }
    }

    engine_.setOutputGain(getParamValue(parameters_, kParamGain));

    const auto interpolation = (!isHardwareWaveMode && getParamValue(parameters_, kParamInterpolation) > 0.5f)
        ? hyperframe::dsp::WaveTableOscillator::Interpolation::Linear
        : hyperframe::dsp::WaveTableOscillator::Interpolation::Nearest;
    engine_.setInterpolation(interpolation);
    updateMotionTableFromParameters();

    hyperframe::dsp::CommandSettings commandSettings;
    commandSettings.enabled = getParamValue(parameters_, kParamMotionTable) > 0.5f;
    commandSettings.loop = getParamValue(parameters_, kParamMotionLoop) > 0.5f;
    commandSettings.mono = getParamValue(parameters_, kParamMonoMode) > 0.5f;
    const auto motionRateValue = getParamValue(parameters_, kParamMotionRate);
    commandSettings.rateHz = getParamValue(parameters_, kParamMotionClockMode) > 0.5f
        ? bpmSyncedMotionRateHz(motionRateValue, hostBpm_)
        : motionRateValue;
    commandSettings.stepCount = static_cast<int>(getParamValue(parameters_, kParamMotionSteps));
    commandSettings.loopStartStep = std::clamp(static_cast<int>(getParamValue(parameters_, kParamMotionLoopStart)), 0, commandSettings.stepCount - 1);
    const auto slideTimeValue = slideSyncParameterValue(getParamValue(parameters_, kParamSlideTime));
    commandSettings.slideTimeSeconds = getParamValue(parameters_, kParamMotionClockMode) > 0.5f
        ? bpmSyncedSlideTimeSeconds(slideTimeValue, hostBpm_)
        : slideTimeValue;
    commandSettings.engineMode = engineMode;
    commandSettings.basePhaseAmount = getParamValue(parameters_, kParamLsdjPhase);
    commandSettings.rawBitDepth = static_cast<int>(getParamValue(parameters_, kParamWaveBits));
    commandSettings.rawPlayFull = getParamValue(parameters_, kParamRawPlayFull) > 0.5f;
    commandSettings.phaseMode = lsdjPhaseModeFromIndex(static_cast<int>(getParamValue(parameters_, kParamLsdjPhaseMode)));
    engine_.setCommandSettings(commandSettings);

    hyperframe::dsp::AdsrEnvelope::Settings envelope;
    if (isHardwareWaveMode) {
        envelope.attackSeconds = 0.0f;
        envelope.decaySeconds = 0.0f;
        envelope.sustainLevel = 1.0f;
        envelope.releaseSeconds = 0.0f;
    } else {
        envelope.attackSeconds = getParamValue(parameters_, kParamAttack);
        envelope.decaySeconds = getParamValue(parameters_, kParamDecay);
        envelope.sustainLevel = getParamValue(parameters_, kParamSustain);
        envelope.releaseSeconds = getParamValue(parameters_, kParamRelease);
    }
    engine_.setEnvelopeSettings(envelope);
}

void HyperFrameAudioProcessor::selectMotionStep(int index) {
    selectedMotionStep_ = std::clamp(index, 0, hyperframe::dsp::kCommandStepCount - 1);
}

int HyperFrameAudioProcessor::selectedMotionStep() const {
    return selectedMotionStep_;
}

int HyperFrameAudioProcessor::currentProgramSelection() const {
    return currentProgram_;
}

double HyperFrameAudioProcessor::hostBpm() const {
    return hostBpm_;
}

juce::MidiKeyboardState& HyperFrameAudioProcessor::keyboardState() {
    return keyboardState_;
}

void HyperFrameAudioProcessor::updateMotionTableFromParameters() {
    const auto engineMode = engineModeFromIndex(static_cast<int>(getParamValue(parameters_, kParamEngineMode)));
    for (int stepIndex = 0; stepIndex < hyperframe::dsp::kCommandStepCount; ++stepIndex) {
        hyperframe::dsp::CommandStep step;
        step.frame = static_cast<int>(getParamValue(parameters_, motionParamId(stepIndex, "frame"))) - 1;
        step.pitchSemitones = getParamValue(parameters_, motionParamId(stepIndex, "pitch"));
        step.pitchBendSemitonesPerSecond = getParamValue(parameters_, motionParamId(stepIndex, "bend"));
        step.phaseAmount = getParamValue(parameters_, motionParamId(stepIndex, "phase"));
        step.vibratoRateHz = getParamValue(parameters_, motionParamId(stepIndex, "vib_rate"));
        step.level = getParamValue(parameters_, motionParamId(stepIndex, "level"));
        if (hyperframe::dsp::isGameBoyWaveEngineMode(engineMode)) {
            step.level = lsdjWaveVolumeAmplitude(step.level);
        }
        motionTable_[static_cast<std::size_t>(stepIndex)] = step;
        engine_.setCommandStep(stepIndex, step);
    }
}

void HyperFrameAudioProcessor::applyEngineModeProfile(hyperframe::dsp::CommandSettings::EngineMode engineMode) {
    setParameterValue(kParamLsdjPhase, getParamValue(parameters_, kParamLsdjPhase));
    setParameterValue(kParamDecay, getParamValue(parameters_, kParamDecay));
    setParameterValue(kParamSustain, getParamValue(parameters_, kParamSustain));
    setParameterValue(kParamRelease, getParamValue(parameters_, kParamRelease));
    setParameterValue(kParamSelectedFrame, 1.0f);

    switch (engineMode) {
    case hyperframe::dsp::CommandSettings::EngineMode::Draw:
        for (int stepIndex = 0; stepIndex < hyperframe::dsp::kCommandStepCount; ++stepIndex) {
            const auto pitchId = motionParamId(stepIndex, "pitch");
            const auto phaseId = motionParamId(stepIndex, "phase");
            const auto levelId = motionParamId(stepIndex, "level");
            setParameterValue(pitchId.toRawUTF8(), getParamValue(parameters_, pitchId));
            setParameterValue(phaseId.toRawUTF8(), std::clamp(getParamValue(parameters_, phaseId), -31.0f, 31.0f));
            setParameterValue(levelId.toRawUTF8(), getParamValue(parameters_, levelId));
        }
        break;

    case hyperframe::dsp::CommandSettings::EngineMode::Wave:
        setParameterValue(kParamWaveLength, static_cast<float>(hyperframe::dsp::hardwareWaveLength(engineMode)));
        setParameterValue(kParamWaveBits, static_cast<float>(hyperframe::dsp::hardwareWaveBitDepth(engineMode)));
        setParameterValue(kParamInterpolation, 0.0f);
        setParameterValue(kParamMonoMode, 1.0f);
        for (int stepIndex = 0; stepIndex < hyperframe::dsp::kCommandStepCount; ++stepIndex) {
            const auto pitchId = motionParamId(stepIndex, "pitch");
            const auto phaseId = motionParamId(stepIndex, "phase");
            const auto levelId = motionParamId(stepIndex, "level");
            setParameterValue(pitchId.toRawUTF8(), getParamValue(parameters_, pitchId));
            setParameterValue(phaseId.toRawUTF8(), std::clamp(getParamValue(parameters_, phaseId), -31.0f, 31.0f));
            setParameterValue(levelId.toRawUTF8(), lsdjWaveVolumeParameter(getParamValue(parameters_, levelId)));
        }
        break;

    case hyperframe::dsp::CommandSettings::EngineMode::WonderSwan:
    case hyperframe::dsp::CommandSettings::EngineMode::PcEngine:
    case hyperframe::dsp::CommandSettings::EngineMode::Scc:
        setParameterValue(kParamWaveLength, static_cast<float>(hyperframe::dsp::hardwareWaveLength(engineMode)));
        setParameterValue(kParamWaveBits, static_cast<float>(hyperframe::dsp::hardwareWaveBitDepth(engineMode)));
        setParameterValue(kParamInterpolation, 0.0f);
        setParameterValue(kParamMonoMode, 1.0f);
        for (int stepIndex = 0; stepIndex < hyperframe::dsp::kCommandStepCount; ++stepIndex) {
            const auto pitchId = motionParamId(stepIndex, "pitch");
            const auto phaseId = motionParamId(stepIndex, "phase");
            const auto levelId = motionParamId(stepIndex, "level");
            setParameterValue(pitchId.toRawUTF8(), getParamValue(parameters_, pitchId));
            setParameterValue(phaseId.toRawUTF8(), std::clamp(getParamValue(parameters_, phaseId), -31.0f, 31.0f));
            setParameterValue(levelId.toRawUTF8(), getParamValue(parameters_, levelId));
        }
        break;

    case hyperframe::dsp::CommandSettings::EngineMode::Raw:
        setParameterValue(kParamSelectedFrame, 1.0f);
        for (int stepIndex = 0; stepIndex < hyperframe::dsp::kCommandStepCount; ++stepIndex) {
            const auto pitchId = motionParamId(stepIndex, "pitch");
            const auto phaseId = motionParamId(stepIndex, "phase");
            const auto levelId = motionParamId(stepIndex, "level");
            setParameterValue(pitchId.toRawUTF8(), getParamValue(parameters_, pitchId));
            setParameterValue(phaseId.toRawUTF8(), 0.0f);
            setParameterValue(levelId.toRawUTF8(), getParamValue(parameters_, levelId));
        }
        break;
    }

    updateMotionTableFromParameters();
}

void HyperFrameAudioProcessor::setParameterValue(const char* id, float value) {
    if (auto* parameter = parameters_.getParameter(id)) {
        if (auto* ranged = dynamic_cast<juce::RangedAudioParameter*>(parameter)) {
            const auto snappedValue = ranged->convertFrom0to1(ranged->convertTo0to1(value));
            const auto normalisedValue = ranged->convertTo0to1(snappedValue);
            if (std::abs(parameter->getValue() - normalisedValue) <= 1.0e-6f) {
                return;
            }

            if (suppressHostNotification_) {
                parameter->setValue(normalisedValue);
            } else {
                parameter->beginChangeGesture();
                parameter->setValueNotifyingHost(normalisedValue);
                parameter->endChangeGesture();
            }
        }
    }
}

void HyperFrameAudioProcessor::setRawStreamPlaybackDefaults() {
    setParameterValue(kParamEngineMode, static_cast<float>(engineModeIndex(hyperframe::dsp::CommandSettings::EngineMode::Raw)));
    setParameterValue(kParamRawPlayFull, 0.0f);
    setParameterValue(kParamAttack, 0.0f);
    setParameterValue(kParamDecay, 0.0f);
    setParameterValue(kParamSustain, 1.0f);
    setParameterValue(kParamRelease, 0.0f);
    setParameterValue(kParamSelectedFrame, 1.0f);
    setParameterValue(kParamMotionTable, 0.0f);
    setParameterValue(kParamMotionLoop, 0.0f);
    setParameterValue(kParamMotionLoopStart, 0.0f);
}

void HyperFrameAudioProcessor::setMotionStepValues(int stepIndex, float frame, float pitch, float bend, float phase, float vibratoRate, float level) {
    setParameterValue(motionParamId(stepIndex, "frame").toRawUTF8(), frame);
    setParameterValue(motionParamId(stepIndex, "pitch").toRawUTF8(), pitch);
    setParameterValue(motionParamId(stepIndex, "bend").toRawUTF8(), bend);
    setParameterValue(motionParamId(stepIndex, "phase").toRawUTF8(), phase);
    setParameterValue(motionParamId(stepIndex, "vib_rate").toRawUTF8(), vibratoRate);
    setParameterValue(motionParamId(stepIndex, "level").toRawUTF8(), level);
}

void HyperFrameAudioProcessor::setSequentialMotionStepDefaults() {
    for (int stepIndex = 0; stepIndex < hyperframe::dsp::kCommandStepCount; ++stepIndex) {
        setMotionStepValues(stepIndex, static_cast<float>(stepIndex + 1), 0.0f, 0.0f, 0.0f, 0.0f, 1.0f);
    }
}

void HyperFrameAudioProcessor::addStrictParameterListeners() {
    for (const auto* id : { kParamEngineMode, kParamWaveLength, kParamWaveBits, kParamInterpolation,
             kParamMotionClockMode,
             kParamMotionRate, kParamSlideTime }) {
        parameters_.addParameterListener(id, this);
    }

    for (int stepIndex = 0; stepIndex < hyperframe::dsp::kCommandStepCount; ++stepIndex) {
        parameters_.addParameterListener(motionParamId(stepIndex, "level"), this);
        parameters_.addParameterListener(motionParamId(stepIndex, "phase"), this);
    }
}

void HyperFrameAudioProcessor::removeStrictParameterListeners() {
    for (const auto* id : { kParamEngineMode, kParamWaveLength, kParamWaveBits, kParamInterpolation,
             kParamMotionClockMode,
             kParamMotionRate, kParamSlideTime }) {
        parameters_.removeParameterListener(id, this);
    }

    for (int stepIndex = 0; stepIndex < hyperframe::dsp::kCommandStepCount; ++stepIndex) {
        parameters_.removeParameterListener(motionParamId(stepIndex, "level"), this);
        parameters_.removeParameterListener(motionParamId(stepIndex, "phase"), this);
    }
}

void HyperFrameAudioProcessor::enforceWaveStrictParameters() {
    const auto engineMode = engineModeFromIndex(static_cast<int>(getParamValue(parameters_, kParamEngineMode)));
    if (!hyperframe::dsp::isHardwareWaveEngineMode(engineMode)) {
        return;
    }

    setParameterValue(kParamWaveLength, static_cast<float>(hyperframe::dsp::hardwareWaveLength(engineMode)));
    setParameterValue(kParamWaveBits, static_cast<float>(hyperframe::dsp::hardwareWaveBitDepth(engineMode)));
    setParameterValue(kParamInterpolation, 0.0f);
    setParameterValue(kParamMotionRate, getParamValue(parameters_, kParamMotionRate));

    for (int stepIndex = 0; stepIndex < hyperframe::dsp::kCommandStepCount; ++stepIndex) {
        const auto phaseId = motionParamId(stepIndex, "phase");
        const auto levelId = motionParamId(stepIndex, "level");
        setParameterValue(phaseId.toRawUTF8(), std::clamp(getParamValue(parameters_, phaseId), -31.0f, 31.0f));
        if (hyperframe::dsp::isGameBoyWaveEngineMode(engineMode)) {
            setParameterValue(levelId.toRawUTF8(), lsdjWaveVolumeParameter(getParamValue(parameters_, levelId)));
        } else {
            setParameterValue(levelId.toRawUTF8(), getParamValue(parameters_, levelId));
        }
    }
}

void HyperFrameAudioProcessor::enforceMotionClockParameters() {
    setParameterValue(kParamSlideTime, slideSyncParameterValue(getParamValue(parameters_, kParamSlideTime)));

    if (getParamValue(parameters_, kParamMotionClockMode) <= 0.5f) {
        return;
    }

    setParameterValue(kParamMotionRate, motionSyncDivision(getParamValue(parameters_, kParamMotionRate)));
}

void HyperFrameAudioProcessor::updateHostTempo() {
    auto bpm = 120.0;
    if (auto* hostPlayHead = getPlayHead()) {
        if (const auto position = hostPlayHead->getPosition()) {
            if (const auto hostBpm = position->getBpm()) {
                bpm = *hostBpm;
            }
        }
    }

    hostBpm_ = std::clamp(bpm, 20.0, 400.0);
}

void HyperFrameAudioProcessor::parameterChanged(const juce::String& parameterID, float newValue) {
    juce::ignoreUnused(parameterID, newValue);
    if (!suppressHostNotification_) {
        triggerAsyncUpdate();
    }
}

void HyperFrameAudioProcessor::handleAsyncUpdate() {
    enforceWaveStrictParameters();
    enforceMotionClockParameters();
}

int HyperFrameAudioProcessor::editableWaveFrameIndex() const {
    return static_cast<int>(getParamValue(parameters_, kParamSelectedFrame)) - 1;
}

hyperframe::dsp::WaveTable& HyperFrameAudioProcessor::authoringWaveFrame(int index) {
    return waveFrames_[static_cast<std::size_t>(std::clamp(index, 0, hyperframe::dsp::kWaveFrameCount - 1))];
}

const hyperframe::dsp::WaveTable& HyperFrameAudioProcessor::authoringWaveFrame(int index) const {
    return waveFrames_[static_cast<std::size_t>(std::clamp(index, 0, hyperframe::dsp::kWaveFrameCount - 1))];
}

void HyperFrameAudioProcessor::syncAuthoringWaveFrameProfileLocked(hyperframe::dsp::WaveTable& waveFrame) {
    const auto engineMode = engineModeFromIndex(static_cast<int>(getParamValue(parameters_, kParamEngineMode)));
    if (hyperframe::dsp::isHardwareWaveEngineMode(engineMode)) {
        waveFrame.setActiveLength(static_cast<std::size_t>(hyperframe::dsp::hardwareWaveLength(engineMode)));
        waveFrame.setSampleFormat(sampleFormatForEngineMode(engineMode));
        waveFrame.setBitDepth(hyperframe::dsp::hardwareWaveBitDepth(engineMode));
        return;
    }

    if (engineMode == hyperframe::dsp::CommandSettings::EngineMode::Draw) {
        waveFrame.setActiveLength(static_cast<std::size_t>(getParamValue(parameters_, kParamWaveLength)));
        waveFrame.setBitDepth(static_cast<int>(getParamValue(parameters_, kParamWaveBits)));
    }
}

void HyperFrameAudioProcessor::clearRawSourceLocked() {
    rawStream_.clear();
    rawStreamSnapshotDirty_ = true;
    rawStreamHoldInterpolation_ = false;
    rawStreamRate_ = 44100.0;
    rawStreamSourceRootNote_ = kPitchReferenceMidiNote;
    rawStreamRootNote_ = kPitchReferenceMidiNote;
    rawStreamLoop_ = {};
}

void HyperFrameAudioProcessor::publishSourceSnapshotLocked() {
    if (rawStreamSnapshotDirty_ || rawStreamSnapshot_ == nullptr) {
        rawStreamSnapshot_ = std::make_shared<const std::vector<float>>(rawStream_);
        rawStreamSnapshotDirty_ = false;
    }

    auto snapshot = std::make_shared<hyperframe::dsp::SourceSnapshot>();
    snapshot->waveFrames = waveFrames_;
    snapshot->rawStream = rawStreamSnapshot_;
    snapshot->rawStreamRate = rawStreamRate_;
    snapshot->rawStreamRootNote = rawStreamRootNote_;
    snapshot->rawStreamLoop = rawStreamLoop_;
    snapshot->rawStreamHoldInterpolation = rawStreamHoldInterpolation_;
    std::atomic_store_explicit(&sourceSnapshot_, std::shared_ptr<const hyperframe::dsp::SourceSnapshot>(std::move(snapshot)), std::memory_order_release);
}

void HyperFrameAudioProcessor::applyPendingSourceSnapshot() {
    auto snapshot = std::atomic_load_explicit(&sourceSnapshot_, std::memory_order_acquire);
    if (snapshot != nullptr && snapshot != activeSourceSnapshot_) {
        engine_.setSourceSnapshot(*snapshot);
        activeSourceSnapshot_ = std::move(snapshot);
    }
}

void HyperFrameAudioProcessor::restoreWaveTableState(const juce::ValueTree& state) {
    const juce::ScopedLock lock(waveTableLock_);
    const auto framesState = state.getChildWithName(kWaveFramesStateType);
    if (framesState.isValid()) {
        for (int frame = 0; frame < hyperframe::dsp::kWaveFrameCount; ++frame) {
            const auto frameState = framesState.getChildWithName(frameChildType(frame));
            if (!frameState.isValid()) {
                continue;
            }

            auto& waveFrame = authoringWaveFrame(frame);
            waveFrame.setActiveLength(static_cast<std::size_t>(getParamValue(parameters_, kParamWaveLength)));
            waveFrame.setBitDepth(static_cast<int>(getParamValue(parameters_, kParamWaveBits)));
            waveFrame.setSampleFormat(sampleFormatFromIndex(static_cast<int>(frameState.getProperty(kWaveFrameFormatProperty, sampleFormatIndex(waveFrame.sampleFormat())))));
            waveFrame.setLoop(
                static_cast<bool>(frameState.getProperty(kWaveFrameLoopEnabledProperty, false)),
                static_cast<std::size_t>(static_cast<int>(frameState.getProperty(kWaveFrameLoopStartProperty, 0))),
                static_cast<std::size_t>(static_cast<int>(frameState.getProperty(kWaveFrameLoopEndProperty, static_cast<int>(waveFrame.activeLength())))));
            for (std::size_t i = 0; i < hyperframe::dsp::WaveTable::kMaxPoints; ++i) {
                const auto property = wavePointProperty(i);
                if (frameState.hasProperty(property)) {
                    waveFrame.setPoint(i, static_cast<float>(frameState.getProperty(property)));
                }
            }
        }

        publishSourceSnapshotLocked();
        return;
    }

    const auto waveState = state.getChildWithName(kWaveTableStateType);
    if (waveState.isValid()) {
        auto& waveFrame = authoringWaveFrame(editableWaveFrameIndex());
        waveFrame.setActiveLength(static_cast<std::size_t>(getParamValue(parameters_, kParamWaveLength)));
        waveFrame.setSampleFormat(sampleFormatFromIndex(static_cast<int>(waveState.getProperty(kWaveFrameFormatProperty, sampleFormatIndex(waveFrame.sampleFormat())))));
        waveFrame.setBitDepth(static_cast<int>(getParamValue(parameters_, kParamWaveBits)));
        waveFrame.setLoop(
            static_cast<bool>(waveState.getProperty(kWaveFrameLoopEnabledProperty, false)),
            static_cast<std::size_t>(static_cast<int>(waveState.getProperty(kWaveFrameLoopStartProperty, 0))),
            static_cast<std::size_t>(static_cast<int>(waveState.getProperty(kWaveFrameLoopEndProperty, static_cast<int>(waveFrame.activeLength())))));

        for (std::size_t i = 0; i < hyperframe::dsp::WaveTable::kMaxPoints; ++i) {
            const auto property = wavePointProperty(i);
            if (waveState.hasProperty(property)) {
                waveFrame.setPoint(i, static_cast<float>(waveState.getProperty(property)));
            }
        }
        publishSourceSnapshotLocked();
    }
}

juce::ValueTree HyperFrameAudioProcessor::createWaveTableState() const {
    juce::ValueTree framesState(kWaveFramesStateType);
    const juce::ScopedLock lock(waveTableLock_);

    for (int frame = 0; frame < hyperframe::dsp::kWaveFrameCount; ++frame) {
        juce::ValueTree frameState(frameChildType(frame));
        const auto& waveFrame = authoringWaveFrame(frame);
        frameState.setProperty(kWaveFrameFormatProperty, sampleFormatIndex(waveFrame.sampleFormat()), nullptr);
        frameState.setProperty(kWaveFrameLoopEnabledProperty, waveFrame.loopEnabled(), nullptr);
        frameState.setProperty(kWaveFrameLoopStartProperty, static_cast<int>(waveFrame.loopStart()), nullptr);
        frameState.setProperty(kWaveFrameLoopEndProperty, static_cast<int>(waveFrame.loopEnd()), nullptr);
        for (std::size_t i = 0; i < hyperframe::dsp::WaveTable::kMaxPoints; ++i) {
            frameState.setProperty(wavePointProperty(i), waveFrame.sourcePoint(i), nullptr);
        }

        framesState.addChild(frameState, -1, nullptr);
    }

    return framesState;
}

void HyperFrameAudioProcessor::restoreRawStreamState(const juce::ValueTree& state) {
    const auto rawState = state.getChildWithName(kRawStreamStateType);
    if (!rawState.isValid()) {
        const juce::ScopedLock lock(waveTableLock_);
        clearRawSourceLocked();
        publishSourceSnapshotLocked();
        return;
    }

    auto restoredSampleRate = static_cast<double>(rawState.getProperty(kRawStreamRateProperty, rawStreamRate_));
    auto restoredSourceRootNote = static_cast<double>(rawState.getProperty(kRawStreamSourceRootNoteProperty,
        rawState.getProperty(kRawStreamRootNoteProperty, rawStreamSourceRootNote_)));
    auto restoredRootNote = static_cast<double>(rawState.getProperty(kRawStreamRootNoteProperty, rawStreamRootNote_));
    auto restoredHoldInterpolation = static_cast<bool>(rawState.getProperty(kRawStreamHoldInterpolationProperty, false));
    auto restoredLoop = hyperframe::dsp::RawStreamLoop {};
    restoredLoop.enabled = static_cast<bool>(rawState.getProperty(kRawStreamLoopEnabledProperty, false));
    restoredLoop.start = static_cast<std::size_t>(static_cast<int>(rawState.getProperty(kRawStreamLoopStartProperty, 0)));
    restoredLoop.end = static_cast<std::size_t>(static_cast<int>(rawState.getProperty(kRawStreamLoopEndProperty, 0)));
    restoredLoop.playStart = static_cast<std::size_t>(static_cast<int>(rawState.getProperty(kRawStreamPlayStartProperty, 0)));

    juce::MemoryBlock block;
    if (!block.fromBase64Encoding(rawState.getProperty(kRawStreamDataProperty).toString())) {
        const juce::ScopedLock lock(waveTableLock_);
        clearRawSourceLocked();
        publishSourceSnapshotLocked();
        return;
    }

    const auto sampleCount = block.getSize() / sizeof(float);
    auto restoredSamples = std::vector<float>(sampleCount);
    if (sampleCount > 0) {
        std::memcpy(restoredSamples.data(), block.getData(), sampleCount * sizeof(float));
    }

    const juce::ScopedLock lock(waveTableLock_);
    rawStream_ = std::move(restoredSamples);
    rawStreamSnapshotDirty_ = true;
    rawStreamHoldInterpolation_ = restoredHoldInterpolation;
    rawStreamRate_ = restoredSampleRate;
    rawStreamSourceRootNote_ = restoredSourceRootNote;
    rawStreamRootNote_ = restoredRootNote;
    rawStreamLoop_ = restoredLoop;
    publishSourceSnapshotLocked();
}

juce::ValueTree HyperFrameAudioProcessor::createRawStreamState() const {
    juce::ValueTree rawState(kRawStreamStateType);
    const juce::ScopedLock lock(waveTableLock_);
    rawState.setProperty(kRawStreamRateProperty, rawStreamRate_, nullptr);
    rawState.setProperty(kRawStreamSourceRootNoteProperty, rawStreamSourceRootNote_, nullptr);
    rawState.setProperty(kRawStreamRootNoteProperty, rawStreamRootNote_, nullptr);
    rawState.setProperty(kRawStreamHoldInterpolationProperty, rawStreamHoldInterpolation_, nullptr);
    rawState.setProperty(kRawStreamLoopEnabledProperty, rawStreamLoop_.enabled, nullptr);
    rawState.setProperty(kRawStreamLoopStartProperty, static_cast<int>(rawStreamLoop_.start), nullptr);
    rawState.setProperty(kRawStreamLoopEndProperty, static_cast<int>(rawStreamLoop_.end), nullptr);
    rawState.setProperty(kRawStreamPlayStartProperty, static_cast<int>(rawStreamLoop_.playStart), nullptr);

    if (!rawStream_.empty()) {
        juce::MemoryBlock block(rawStream_.data(), rawStream_.size() * sizeof(float));
        rawState.setProperty(kRawStreamDataProperty, block.toBase64Encoding(), nullptr);
    }

    return rawState;
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() {
    return new HyperFrameAudioProcessor();
}
