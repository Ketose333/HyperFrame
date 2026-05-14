#include "dsp/Voice.h"

#include <algorithm>
#include <cmath>

namespace hyperframe::dsp {

namespace {
constexpr float kTwoPi = 6.28318530717958647692f;

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
} // namespace

void Voice::setSampleRate(double sampleRate) {
    sampleRate_ = sampleRate > 1.0 ? sampleRate : 44100.0;
    oscillator_.setSampleRate(sampleRate);
    envelope_.setSampleRate(sampleRate);
}

void Voice::setEnvelopeSettings(const AdsrEnvelope::Settings& settings) {
    envelope_.setSettings(settings);
}

void Voice::setInterpolation(WaveTableOscillator::Interpolation interpolation) {
    oscillator_.setInterpolation(interpolation);
}

void Voice::noteOn(int noteNumber, float velocity) {
    baseMidiNote_ = static_cast<float>(noteNumber);
    noteNumber_ = noteNumber;
    velocity_ = std::clamp(velocity, 0.0f, 1.0f);
    slideStartMidiNote_ = baseMidiNote_;
    targetMidiNote_ = static_cast<float>(noteNumber);
    slideElapsedSeconds_ = 0.0f;
    commandStepIndex_ = 0;
    commandFrameIndex_ = 0;
    commandStepTimeSeconds_ = 0.0f;
    rawStreamPosition_ = 0.0;
    wavePitchBendSemitones_ = 0.0f;
    waveVibratoPhase_ = 0.0f;
    hardwareJitterPhase_ = wrapPhase(static_cast<float>((noteNumber * 37) % 101) / 101.0f);
    triggerPending_ = true;
    oscillator_.setFrequency(midiNoteToFrequency(baseMidiNote_));
    oscillator_.resetPhase();
    envelope_.noteOn();
}

void Voice::slideTo(int noteNumber, float velocity) {
    if (!envelope_.isActive()) {
        noteOn(noteNumber, velocity);
        return;
    }

    noteNumber_ = noteNumber;
    velocity_ = std::clamp(velocity, 0.0f, 1.0f);
    slideStartMidiNote_ = baseMidiNote_;
    targetMidiNote_ = static_cast<float>(noteNumber);
    slideElapsedSeconds_ = 0.0f;
}

void Voice::noteOff(int noteNumber, bool releaseEnvelope) {
    if (noteNumber_ == noteNumber && releaseEnvelope) {
        envelope_.noteOff();
    }
}

void Voice::reset() {
    noteNumber_ = -1;
    velocity_ = 0.0f;
    commandStepIndex_ = 0;
    commandFrameIndex_ = 0;
    commandStepTimeSeconds_ = 0.0f;
    rawStreamPosition_ = 0.0;
    baseMidiNote_ = 69.0f;
    slideStartMidiNote_ = 69.0f;
    targetMidiNote_ = 69.0f;
    slideElapsedSeconds_ = 0.0f;
    wavePitchBendSemitones_ = 0.0f;
    waveVibratoPhase_ = 0.0f;
    hardwareJitterPhase_ = 0.0f;
    triggerPending_ = false;
    oscillator_.resetPhase();
    envelope_.reset();
}

float Voice::render(const std::array<WaveTable, kWaveFrameCount>& frames,
                    const CommandTable& commandTable,
                    const CommandSettings& commandSettings,
                    const std::vector<float>& rawStream,
                    double rawStreamRate,
                    double rawStreamRootNote,
                    const RawStreamLoop& rawStreamLoop,
                    bool rawStreamHoldInterpolation,
                    int sharedCommandStepIndex,
                    float externalPitchBendSemitones) {
    if (!envelope_.isActive()) {
        noteNumber_ = -1;
        return 0.0f;
    }

    const auto clampedStepCount = std::clamp(commandSettings.stepCount, 1, kCommandStepCount);
    const auto useSharedCommandStep = sharedCommandStepIndex >= 0;
    if (triggerPending_ && commandSettings.enabled && !useSharedCommandStep) {
        commandStepIndex_ = 0;
        commandStepTimeSeconds_ = 0.0f;
    }
    auto step = CommandStep {};
    if (commandSettings.enabled) {
        const auto commandStepIndex = useSharedCommandStep
            ? sharedCommandStepIndex
            : commandStepIndex_;
        step = commandTable[static_cast<std::size_t>(std::clamp(commandStepIndex, 0, clampedStepCount - 1))];
    }

    const auto isWaveformMode = isWaveformEngineMode(commandSettings.engineMode);
    const auto allowFrameChange = isWaveformMode;

    if (commandSettings.enabled && allowFrameChange && step.frame >= 0) {
        commandFrameIndex_ = std::clamp(step.frame, 0, kWaveFrameCount - 1);
    }

    const auto slideTime = std::max(0.0f, commandSettings.slideTimeSeconds);
    if (slideTime <= 0.0f) {
        baseMidiNote_ = targetMidiNote_;
    } else if (baseMidiNote_ != targetMidiNote_) {
        slideElapsedSeconds_ = std::min(slideElapsedSeconds_ + (1.0f / static_cast<float>(sampleRate_)), slideTime);
        const auto alpha = std::clamp(slideElapsedSeconds_ / slideTime, 0.0f, 1.0f);
        baseMidiNote_ = slideStartMidiNote_ + ((targetMidiNote_ - slideStartMidiNote_) * alpha);
    }

    const auto isGameBoyWaveMode = commandSettings.engineMode == CommandSettings::EngineMode::Wave;
    if (isGameBoyWaveMode && commandSettings.enabled) {
        const auto bendRate = std::clamp(step.pitchBendSemitonesPerSecond, -96.0f, 96.0f);
        wavePitchBendSemitones_ = std::clamp(wavePitchBendSemitones_ + (bendRate / static_cast<float>(sampleRate_)), -48.0f, 48.0f);
        const auto vibratoRate = std::clamp(step.vibratoRateHz, 0.0f, 64.0f);
        waveVibratoPhase_ = wrapPhase(waveVibratoPhase_ + (vibratoRate / static_cast<float>(sampleRate_)));
    }

    const auto waveVibratoDepth = isGameBoyWaveMode
        ? std::clamp(std::abs(step.phaseAmount) * 0.125f, 0.0f, 4.0f)
        : 0.0f;
    const auto waveVibrato = isGameBoyWaveMode && step.vibratoRateHz > 0.0f
        ? std::sin(waveVibratoPhase_ * kTwoPi) * waveVibratoDepth
        : 0.0f;
    const auto pitchSemitones = step.pitchSemitones
        + (isGameBoyWaveMode ? wavePitchBendSemitones_ + waveVibrato : 0.0f)
        + externalPitchBendSemitones;
    const auto rawFrequency = midiNoteToFrequency(static_cast<int>(std::round(baseMidiNote_))) * std::pow(2.0f, (baseMidiNote_ - std::round(baseMidiNote_) + pitchSemitones) / 12.0f);
    const auto* hardwareProfile = hardwareWaveProfile(commandSettings.engineMode);
    auto frequency = hardwareProfile != nullptr
        ? quantizeHardwareWaveFrequency(rawFrequency, commandSettings.engineMode)
        : rawFrequency;
    if (hardwareProfile != nullptr && hardwareProfile->clockJitterDepth > 0.0f && hardwareProfile->clockJitterRateHz > 0.0f) {
        hardwareJitterPhase_ = wrapPhase(hardwareJitterPhase_ + (hardwareProfile->clockJitterRateHz / static_cast<float>(sampleRate_)));
        frequency *= hardwareWaveClockJitterFactor(*hardwareProfile, hardwareJitterPhase_);
    }
    oscillator_.setFrequency(frequency);

    if (commandSettings.enabled && !useSharedCommandStep) {
        const auto stepDuration = 1.0f / std::max(0.01f, commandSettings.rateHz);
        commandStepTimeSeconds_ += 1.0f / static_cast<float>(sampleRate_);
        while (commandStepTimeSeconds_ >= stepDuration) {
            commandStepTimeSeconds_ -= stepDuration;
            if (commandStepIndex_ + 1 < clampedStepCount) {
                ++commandStepIndex_;
            } else if (commandSettings.loop) {
                commandStepIndex_ = std::clamp(commandSettings.loopStartStep, 0, clampedStepCount - 1);
            } else {
                commandStepIndex_ = clampedStepCount - 1;
                commandStepTimeSeconds_ = 0.0f;
                break;
            }
        }
    }

    if (commandSettings.engineMode == CommandSettings::EngineMode::Raw) {
        if (triggerPending_) {
            rawStreamPosition_ = static_cast<double>(std::min(rawStreamLoop.playStart, rawStream.size()));
            triggerPending_ = false;
        }
        return renderRawStream(rawStream,
                               rawStreamRate,
                               rawStreamRootNote,
                               pitchSemitones,
                               rawStreamLoop,
                               rawStreamHoldInterpolation,
                               commandSettings.rawBitDepth)
            * std::clamp(step.level, 0.0f, 1.5f)
            * envelope_.nextSample()
            * velocity_;
    }

    const auto frameIndex = allowFrameChange ? commandFrameIndex_ : 0;
    const auto& table = frames[static_cast<std::size_t>(frameIndex)];
    const auto allowLsdjPhase = isWaveformMode;
    const auto phaseAmount = allowLsdjPhase
        ? commandSettings.basePhaseAmount + ((commandSettings.enabled && allowLsdjPhase && !isGameBoyWaveMode) ? step.phaseAmount : 0.0f)
        : 0.0f;
    const auto level = std::clamp(step.level, 0.0f, 1.5f);
    const auto envelopeValue = envelope_.nextSample();

    if (triggerPending_) {
        if (isHardwareWaveEngineMode(commandSettings.engineMode)) {
            oscillator_.resetPhase(1.0f / static_cast<float>(table.activeLength()));
            triggerPending_ = false;
            return lastWaveOutput_ * level * envelopeValue * velocity_;
        }
        triggerPending_ = false;
    }

    auto waveOutput = oscillator_.nextSample(table, phaseAmount, commandSettings.phaseMode);
    if (hardwareProfile != nullptr && hardwareProfile->dacBias != 0.0f) {
        waveOutput = std::clamp(waveOutput + hardwareProfile->dacBias, -1.0f, 1.0f);
    }
    if (isWaveformMode) {
        lastWaveOutput_ = waveOutput;
    }

    return waveOutput * level * envelopeValue * velocity_;
}

bool Voice::isActive() const {
    return envelope_.isActive();
}

float Voice::midiNoteToFrequency(int noteNumber) {
    return 440.0f * std::pow(2.0f, (static_cast<float>(noteNumber) - 69.0f) / 12.0f);
}

float Voice::wrapPhase(float phase) {
    return phase - std::floor(phase);
}

float Voice::renderRawStream(const std::vector<float>& rawStream, double rawStreamRate, double rootNote, float pitchSemitones, const RawStreamLoop& loop, bool holdInterpolation, int bitDepth) {
    if (rawStream.empty()) {
        noteNumber_ = -1;
        envelope_.reset();
        return 0.0f;
    }

    const auto loopEnd = std::min(loop.end, rawStream.size());
    const auto loopEnabled = loop.enabled && loopEnd > loop.start + 1;
    if (loopEnabled && rawStreamPosition_ >= static_cast<double>(loopEnd)) {
        const auto loopStart = static_cast<double>(loop.start);
        const auto loopLength = static_cast<double>(loopEnd - loop.start);
        rawStreamPosition_ = loopStart + std::fmod(rawStreamPosition_ - loopStart, loopLength);
    }

    if (!loopEnabled && rawStreamPosition_ > static_cast<double>(rawStream.size() - 1)) {
        noteNumber_ = -1;
        envelope_.reset();
        return 0.0f;
    }

    const auto sample = holdInterpolation
        ? rawStream[std::min(static_cast<std::size_t>(std::floor(rawStreamPosition_)), rawStream.size() - 1)]
        : sampleLinear(rawStream, static_cast<float>(rawStreamPosition_));
    const auto rootOffset = (static_cast<double>(noteNumber_ - rootNote) + static_cast<double>(pitchSemitones)) / 12.0;
    const auto transpose = std::pow(2.0, rootOffset);
    const auto sourceRate = rawStreamRate > 1.0 ? rawStreamRate : sampleRate_;
    rawStreamPosition_ += (sourceRate / sampleRate_) * transpose;
    if (loopEnabled && rawStreamPosition_ >= static_cast<double>(loopEnd)) {
        const auto loopStart = static_cast<double>(loop.start);
        const auto loopLength = static_cast<double>(loopEnd - loop.start);
        rawStreamPosition_ = loopStart + std::fmod(rawStreamPosition_ - loopStart, loopLength);
    }
    return WaveTable::quantizeValue(sample, bitDepth, WaveTable::SampleFormat::Signed);
}

} // namespace hyperframe::dsp
