#include "dsp/WaveTableOscillator.h"

#include <algorithm>
#include <cmath>

namespace hyperframe::dsp {

void WaveTableOscillator::setSampleRate(double sampleRate) {
    sampleRate_ = sampleRate > 1.0 ? sampleRate : 44100.0;
}

void WaveTableOscillator::setFrequency(float frequencyHz) {
    frequencyHz_ = std::max(0.0f, frequencyHz);
}

void WaveTableOscillator::setInterpolation(Interpolation interpolation) {
    interpolation_ = interpolation;
}

void WaveTableOscillator::resetPhase(float phase) {
    phase_ = wrapPhase(phase);
    position_ = 0.0f;
}

float WaveTableOscillator::nextSample(const WaveTable& table, float phaseAmount, LsdjPhaseMode phaseMode) {
    const auto length = static_cast<float>(table.activeLength());
    const auto loopEnabled = table.loopEnabled();
    const auto loopStart = static_cast<float>(table.loopStart());
    const auto loopEnd = static_cast<float>(table.loopEnd());
    const auto loopLength = std::max(1.0f, loopEnd - loopStart);
    if (loopEnabled && position_ >= loopEnd) {
        position_ = loopStart + std::fmod(position_ - loopStart, loopLength);
    } else if (!loopEnabled && position_ >= length) {
        position_ = std::fmod(position_, length);
    }

    const auto readPosition = loopEnabled ? position_ : wrapPhase(phase_) * length;
    const auto clampedPhase = std::clamp(phaseAmount, 0.0f, 31.0f);
    const auto phaseSpan = std::max(1.0f, length - ((clampedPhase / 31.0f) * (length - 1.0f)));
    float sourcePosition = readPosition;

    if (clampedPhase > 0.0f) {
        switch (phaseMode) {
        case LsdjPhaseMode::Normal:
            if (readPosition >= phaseSpan) {
                sourcePosition = -1.0f;
            } else {
                sourcePosition = (readPosition / phaseSpan) * length;
            }
            break;
        case LsdjPhaseMode::Resync:
            sourcePosition = (std::fmod(readPosition, phaseSpan) / phaseSpan) * length;
            break;
        case LsdjPhaseMode::Resync2:
            sourcePosition = std::fmod(readPosition, phaseSpan);
            break;
        }
    }

    const auto output = sourcePosition < 0.0f ? 0.0f : sampleAt(table, sourcePosition);
    phase_ += frequencyHz_ / static_cast<float>(sampleRate_);
    phase_ = wrapPhase(phase_);
    const auto cycleLength = static_cast<float>(table.playbackCycleLength());
    position_ += (frequencyHz_ / static_cast<float>(sampleRate_)) * cycleLength;
    if (loopEnabled && position_ >= loopEnd) {
        position_ = loopStart + std::fmod(position_ - loopStart, loopLength);
    }
    return output;
}

float WaveTableOscillator::sampleAt(const WaveTable& table, float readPosition) const {
    if (interpolation_ == Interpolation::Nearest) {
        const auto index = static_cast<std::size_t>(std::floor(readPosition));
        return table.point(index);
    }

    const auto indexA = static_cast<std::size_t>(std::floor(readPosition));
    const auto indexB = indexA + 1;
    const auto fraction = readPosition - std::floor(readPosition);
    const auto a = table.point(indexA);
    const auto b = table.point(indexB);
    return a + ((b - a) * fraction);
}

float WaveTableOscillator::wrapPhase(float phase) {
    return phase - std::floor(phase);
}

} // namespace hyperframe::dsp
