#pragma once

#include "dsp/WaveTable.h"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cmath>

namespace hyperframe::dsp {

inline constexpr int kWaveFrameCount = 16;
inline constexpr int kCommandStepCount = 16;
inline constexpr std::size_t kReferenceCycleSize = 64;
inline constexpr float kHardwareWaveJitterTwoPi = 6.28318530717958647692f;

enum class LsdjPhaseMode {
    Normal,
    Resync,
    Resync2
};

struct CommandStep {
    int frame = -1;
    float pitchSemitones = 0.0f;
    float pitchBendSemitonesPerSecond = 0.0f;
    float phaseAmount = 0.0f;
    float vibratoRateHz = 0.0f;
    float level = 1.0f;
};

struct RawStreamLoop {
    bool enabled = false;
    std::size_t start = 0;
    std::size_t end = 0;
    std::size_t playStart = 0;
};

struct CommandSettings {
    enum class EngineMode {
        Raw = 0,
        Draw = 1,
        Wave = 2,
        WonderSwan = 3,
        PcEngine = 4,
        Scc = 5
    };

    bool enabled = false;
    bool loop = false;
    bool mono = false;
    EngineMode engineMode = EngineMode::Draw;
    int stepCount = kCommandStepCount;
    int loopStartStep = 0;
    float rateHz = 12.0f;
    float slideTimeSeconds = 0.0f;
    float basePhaseAmount = 0.0f;
    int rawBitDepth = WaveTable::kMaxBitDepth;
    bool rawPlayFull = false;
    LsdjPhaseMode phaseMode = LsdjPhaseMode::Normal;
};

using CommandTable = std::array<CommandStep, kCommandStepCount>;

struct HardwareWaveProfile {
    CommandSettings::EngineMode engineMode = CommandSettings::EngineMode::Wave;
    int activeLength = 32;
    int bitDepth = 4;
    WaveTable::SampleFormat sampleFormat = WaveTable::SampleFormat::Unsigned;
    float clockHz = 2097152.0f;
    int maximumDivider = 2048;
    float cycleSamples = 32.0f;
    bool gameBoyWaveVolume = false;
    bool sharedMotionLoop = true;
    float dacBias = 0.0f;
    float clockJitterDepth = 0.0f;
    float clockJitterRateHz = 0.0f;
};

inline constexpr std::array<HardwareWaveProfile, 4> kHardwareWaveProfiles {{
    { CommandSettings::EngineMode::Wave, 32, 4, WaveTable::SampleFormat::Unsigned, 2097152.0f, 2048, 32.0f, true, false, -0.020f, 0.00035f, 0.55f },
    { CommandSettings::EngineMode::WonderSwan, 32, 4, WaveTable::SampleFormat::Unsigned, 3072000.0f, 2048, 32.0f, false, true, -0.015f, 0.00030f, 0.47f },
    { CommandSettings::EngineMode::PcEngine, 32, 5, WaveTable::SampleFormat::Unsigned, 3579545.0f, 4096, 32.0f, false, true, -0.010f, 0.00025f, 0.41f },
    { CommandSettings::EngineMode::Scc, 32, 8, WaveTable::SampleFormat::Signed, 3579545.0f, 4096, 32.0f, false, true, 0.000f, 0.00020f, 0.37f },
}};

inline const HardwareWaveProfile* hardwareWaveProfile(CommandSettings::EngineMode engineMode) {
    for (const auto& profile : kHardwareWaveProfiles) {
        if (profile.engineMode == engineMode) {
            return &profile;
        }
    }

    return nullptr;
}

inline bool isWaveformEngineMode(CommandSettings::EngineMode engineMode) {
    return engineMode == CommandSettings::EngineMode::Draw
        || engineMode == CommandSettings::EngineMode::Wave
        || engineMode == CommandSettings::EngineMode::WonderSwan
        || engineMode == CommandSettings::EngineMode::PcEngine
        || engineMode == CommandSettings::EngineMode::Scc;
}

inline bool isHardwareWaveEngineMode(CommandSettings::EngineMode engineMode) {
    return hardwareWaveProfile(engineMode) != nullptr;
}

inline bool isGameBoyWaveEngineMode(CommandSettings::EngineMode engineMode) {
    const auto* profile = hardwareWaveProfile(engineMode);
    return profile != nullptr && profile->gameBoyWaveVolume;
}

inline int hardwareWaveLength(CommandSettings::EngineMode engineMode) {
    const auto* profile = hardwareWaveProfile(engineMode);
    return profile != nullptr ? profile->activeLength : 0;
}

inline int hardwareWaveBitDepth(CommandSettings::EngineMode engineMode) {
    const auto* profile = hardwareWaveProfile(engineMode);
    return profile != nullptr ? profile->bitDepth : 0;
}

inline WaveTable::SampleFormat hardwareWaveSampleFormat(CommandSettings::EngineMode engineMode) {
    const auto* profile = hardwareWaveProfile(engineMode);
    return profile != nullptr ? profile->sampleFormat : WaveTable::SampleFormat::Unsigned;
}

inline bool hardwareWaveUsesSharedMotionLoop(CommandSettings::EngineMode engineMode) {
    const auto* profile = hardwareWaveProfile(engineMode);
    return profile != nullptr && profile->sharedMotionLoop;
}

inline float quantizeHardwareWaveFrequency(float frequencyHz, CommandSettings::EngineMode engineMode) {
    const auto* profile = hardwareWaveProfile(engineMode);
    if (profile == nullptr || frequencyHz <= 0.0f) {
        return frequencyHz;
    }

    const auto divider = std::clamp(
        static_cast<int>(std::round((profile->clockHz / profile->cycleSamples) / frequencyHz)),
        1,
        profile->maximumDivider);
    return (profile->clockHz / profile->cycleSamples) / static_cast<float>(divider);
}

inline float hardwareWaveClockJitterFactor(const HardwareWaveProfile& profile, float phase) {
    if (profile.clockJitterDepth <= 0.0f || profile.clockJitterRateHz <= 0.0f) {
        return 1.0f;
    }

    const auto wrappedPhase = phase - std::floor(phase);
    return 1.0f + (std::sin(wrappedPhase * kHardwareWaveJitterTwoPi) * profile.clockJitterDepth);
}

} // namespace hyperframe::dsp
