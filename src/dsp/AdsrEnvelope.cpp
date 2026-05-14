#include "dsp/AdsrEnvelope.h"

#include <algorithm>

namespace hyperframe::dsp {

void AdsrEnvelope::setSampleRate(double sampleRate) {
    sampleRate_ = sampleRate > 1.0 ? sampleRate : 44100.0;
}

void AdsrEnvelope::setSettings(const Settings& settings) {
    settings_ = settings;
    settings_.attackSeconds = std::max(0.0f, settings_.attackSeconds);
    settings_.decaySeconds = std::max(0.0f, settings_.decaySeconds);
    settings_.sustainLevel = std::clamp(settings_.sustainLevel, 0.0f, 1.0f);
    settings_.releaseSeconds = std::max(0.0f, settings_.releaseSeconds);
}

void AdsrEnvelope::noteOn() {
    stage_ = Stage::Attack;
}

void AdsrEnvelope::noteOff() {
    if (stage_ == Stage::Idle) {
        return;
    }

    stage_ = Stage::Release;
    releaseStep_ = secondsToStep(settings_.releaseSeconds, value_);
}

void AdsrEnvelope::reset() {
    stage_ = Stage::Idle;
    value_ = 0.0f;
    releaseStep_ = 0.0f;
}

float AdsrEnvelope::nextSample() {
    switch (stage_) {
    case Stage::Idle:
        value_ = 0.0f;
        break;
    case Stage::Attack:
        value_ += secondsToStep(settings_.attackSeconds, 1.0f);
        if (value_ >= 1.0f || settings_.attackSeconds == 0.0f) {
            value_ = 1.0f;
            stage_ = Stage::Decay;
        }
        break;
    case Stage::Decay:
        value_ -= secondsToStep(settings_.decaySeconds, 1.0f - settings_.sustainLevel);
        if (value_ <= settings_.sustainLevel || settings_.decaySeconds == 0.0f) {
            value_ = settings_.sustainLevel;
            stage_ = Stage::Sustain;
        }
        break;
    case Stage::Sustain:
        value_ = settings_.sustainLevel;
        break;
    case Stage::Release:
        value_ -= releaseStep_;
        if (value_ <= 0.0f || settings_.releaseSeconds == 0.0f) {
            value_ = 0.0f;
            stage_ = Stage::Idle;
        }
        break;
    }

    return value_;
}

bool AdsrEnvelope::isActive() const {
    return stage_ != Stage::Idle;
}

float AdsrEnvelope::secondsToStep(float seconds, float distance) const {
    if (seconds <= 0.0f) {
        return distance;
    }

    return distance / static_cast<float>(seconds * sampleRate_);
}

} // namespace hyperframe::dsp

