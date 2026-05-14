#include "dsp/SynthEngine.h"

#include <algorithm>
#include <cmath>

namespace hyperframe::dsp {

void SynthEngine::setSampleRate(double sampleRate) {
    sampleRate_ = sampleRate > 1.0 ? sampleRate : 44100.0;
    for (auto& voice : voices_) {
        voice.setSampleRate(sampleRate_);
    }
}

void SynthEngine::setEnvelopeSettings(const AdsrEnvelope::Settings& settings) {
    for (auto& voice : voices_) {
        voice.setEnvelopeSettings(settings);
    }
}

void SynthEngine::setInterpolation(WaveTableOscillator::Interpolation interpolation) {
    for (auto& voice : voices_) {
        voice.setInterpolation(interpolation);
    }
}

void SynthEngine::setCommandSettings(const CommandSettings& settings) {
    commandSettings_ = settings;
    const auto clampedStepCount = std::clamp(commandSettings_.stepCount, 1, kCommandStepCount);
    commandSettings_.loopStartStep = std::clamp(commandSettings_.loopStartStep, 0, clampedStepCount - 1);
    sharedCommandStepIndex_ = std::clamp(sharedCommandStepIndex_, 0, clampedStepCount - 1);
}

void SynthEngine::setCommandStep(int index, const CommandStep& step) {
    if (index < 0 || index >= kCommandStepCount) {
        return;
    }

    commandTable_[static_cast<std::size_t>(index)] = step;
}

void SynthEngine::setRawStream(const std::vector<float>& samples, double sampleRate, double rootNote, RawStreamLoop loop, bool holdInterpolation) {
    rawStream_ = std::make_shared<const std::vector<float>>(samples);
    rawStreamRate_ = sampleRate > 1.0 ? sampleRate : 44100.0;
    rawStreamRootNote_ = rootNote;
    rawStreamLoop_ = loop;
    rawStreamHoldInterpolation_ = holdInterpolation;
    rawStreamLoop_.start = std::min(rawStreamLoop_.start, rawStream_->size());
    rawStreamLoop_.end = std::min(rawStreamLoop_.end, rawStream_->size());
    rawStreamLoop_.playStart = std::min(rawStreamLoop_.playStart, rawStream_->size());
    rawStreamLoop_.enabled = rawStreamLoop_.enabled && rawStreamLoop_.end > rawStreamLoop_.start + 1;
}

void SynthEngine::setSourceSnapshot(const SourceSnapshot& snapshot) {
    waveFrames_ = snapshot.waveFrames;
    rawStream_ = snapshot.rawStream;
    rawStreamRate_ = snapshot.rawStreamRate > 1.0 ? snapshot.rawStreamRate : 44100.0;
    rawStreamRootNote_ = snapshot.rawStreamRootNote;
    rawStreamLoop_ = snapshot.rawStreamLoop;
    rawStreamHoldInterpolation_ = snapshot.rawStreamHoldInterpolation;

    const auto rawStreamSize = rawStream_ != nullptr ? rawStream_->size() : std::size_t { 0 };
    rawStreamLoop_.start = std::min(rawStreamLoop_.start, rawStreamSize);
    rawStreamLoop_.end = std::min(rawStreamLoop_.end, rawStreamSize);
    rawStreamLoop_.playStart = std::min(rawStreamLoop_.playStart, rawStreamSize);
    rawStreamLoop_.enabled = rawStreamLoop_.enabled && rawStreamLoop_.end > rawStreamLoop_.start + 1;
}

void SynthEngine::setOutputGain(float gain) {
    outputGain_ = std::max(0.0f, gain);
}

void SynthEngine::setPitchBend(float semitones) {
    pitchBend_ = semitones;
}

int SynthEngine::currentMotionStep() const {
    if (usesSharedCommandLoop())
        return sharedCommandStepIndex_;
    for (const auto& v : voices_)
        if (v.isActive()) return v.commandStepIndex();
    return 0;
}

void SynthEngine::noteOn(int noteNumber, float velocity) {
    const auto noteVelocity = isHardwareWaveEngineMode(commandSettings_.engineMode)
        ? 1.0f
        : velocity;
    const auto hasActiveVoice = std::any_of(voices_.begin(), voices_.end(), [](const Voice& voice) {
        return voice.isActive();
    });
    const auto shouldRestartSharedMotion = usesSharedCommandLoop()
        && (!hasActiveVoice || commandSettings_.mono);
    if (shouldRestartSharedMotion) {
        sharedCommandStepIndex_ = 0;
        sharedCommandStepTimeSeconds_ = 0.0f;
    }

    const auto glideEnabled = commandSettings_.slideTimeSeconds > 0.0f;
    if (glideEnabled) {
        for (std::size_t index = 1; index < voices_.size(); ++index) {
            voices_[index].reset();
        }

        if (voices_.front().isActive()) {
            voices_.front().slideTo(noteNumber, noteVelocity);
            return;
        }

        voices_.front().noteOn(noteNumber, noteVelocity);
        return;
    }

    if (commandSettings_.mono) {
        for (auto& voice : voices_) {
            voice.reset();
        }

        voices_.front().noteOn(noteNumber, noteVelocity);
        return;
    }

    findVoiceForNoteOn().noteOn(noteNumber, noteVelocity);
}

void SynthEngine::noteOff(int noteNumber) {
    const auto releaseEnvelope = commandSettings_.engineMode != CommandSettings::EngineMode::Raw
        || !commandSettings_.rawPlayFull;
    for (auto& voice : voices_) {
        voice.noteOff(noteNumber, releaseEnvelope);
    }
}

void SynthEngine::reset() {
    for (auto& voice : voices_) {
        voice.reset();
    }
    sharedCommandStepIndex_ = 0;
    sharedCommandStepTimeSeconds_ = 0.0f;
}

float SynthEngine::renderSample() {
    float mixed = 0.0f;
    int activeVoiceCount = 0;
    const auto sharedCommandStepIndex = usesSharedCommandLoop() ? sharedCommandStepIndex_ : -1;
    static const std::vector<float> emptyRawStream;
    const auto& rawStream = rawStream_ != nullptr ? *rawStream_ : emptyRawStream;

    for (auto& voice : voices_) {
        if (voice.isActive()) {
            ++activeVoiceCount;
        }
        mixed += voice.render(waveFrames_,
                               commandTable_,
                               commandSettings_,
                               rawStream,
                               rawStreamRate_,
                               rawStreamRootNote_,
                               rawStreamLoop_,
                               rawStreamHoldInterpolation_,
                               sharedCommandStepIndex,
                               pitchBend_);
    }

    if (sharedCommandStepIndex >= 0 && activeVoiceCount > 0) {
        advanceSharedCommandLoop();
    }

    if (isHardwareWaveEngineMode(commandSettings_.engineMode)
        && !commandSettings_.mono
        && activeVoiceCount > 1) {
        mixed /= std::sqrt(static_cast<float>(activeVoiceCount));
    }

    return mixed * outputGain_;
}

WaveTable& SynthEngine::waveTable() {
    return waveFrame(selectedWaveFrame_);
}

const WaveTable& SynthEngine::waveTable() const {
    return waveFrame(selectedWaveFrame_);
}

WaveTable& SynthEngine::waveFrame(int index) {
    return waveFrames_[static_cast<std::size_t>(std::clamp(index, 0, kWaveFrameCount - 1))];
}

const WaveTable& SynthEngine::waveFrame(int index) const {
    return waveFrames_[static_cast<std::size_t>(std::clamp(index, 0, kWaveFrameCount - 1))];
}

void SynthEngine::setSelectedWaveFrame(int index) {
    selectedWaveFrame_ = std::clamp(index, 0, kWaveFrameCount - 1);
}

int SynthEngine::selectedWaveFrame() const {
    return selectedWaveFrame_;
}

Voice& SynthEngine::findVoiceForNoteOn() {
    for (auto& voice : voices_) {
        if (!voice.isActive()) {
            return voice;
        }
    }

    return voices_.front();
}

bool SynthEngine::usesSharedCommandLoop() const {
    return commandSettings_.enabled
        && commandSettings_.loop
        && isWaveformEngineMode(commandSettings_.engineMode)
        && (!isHardwareWaveEngineMode(commandSettings_.engineMode)
            || hardwareWaveUsesSharedMotionLoop(commandSettings_.engineMode));
}

void SynthEngine::advanceSharedCommandLoop() {
    const auto clampedStepCount = std::clamp(commandSettings_.stepCount, 1, kCommandStepCount);
    const auto stepDuration = 1.0f / std::max(0.01f, commandSettings_.rateHz);
    sharedCommandStepTimeSeconds_ += 1.0f / static_cast<float>(sampleRate_);

    while (sharedCommandStepTimeSeconds_ >= stepDuration) {
        sharedCommandStepTimeSeconds_ -= stepDuration;
        if (sharedCommandStepIndex_ + 1 < clampedStepCount) {
            ++sharedCommandStepIndex_;
        } else {
            sharedCommandStepIndex_ = std::clamp(commandSettings_.loopStartStep, 0, clampedStepCount - 1);
        }
    }
}

} // namespace hyperframe::dsp
