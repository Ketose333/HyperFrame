#include "dsp/WaveTable.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>

namespace hyperframe::dsp {

namespace {
constexpr float kPi = 3.14159265358979323846f;

float clampAudio(float value) {
    return std::clamp(value, -1.0f, 1.0f);
}

std::size_t activeIndexForSource(std::size_t sourceIndex, std::size_t activeLength) {
    return (sourceIndex * activeLength) / WaveTable::kMaxPoints;
}

float lmmsTriangleSample(float phase) {
    if (phase <= 0.25f) {
        return phase * 4.0f;
    }

    if (phase <= 0.75f) {
        return 2.0f - (phase * 4.0f);
    }

    return (phase * 4.0f) - 4.0f;
}

std::uint16_t nextLfsrState(std::uint16_t state) {
    const auto feedback = static_cast<std::uint16_t>(((state >> 0) ^ (state >> 1)) & 0x0001u);
    return static_cast<std::uint16_t>((state >> 1) | (feedback << 14));
}

float lfsrNoiseValue(std::uint16_t& state) {
    state = nextLfsrState(state == 0 ? 0x4000u : state);
    const auto code = state & 0x000Fu;
    return (static_cast<float>(code) / 15.0f * 2.0f) - 1.0f;
}

float quantizedRampValue(std::size_t sourceIndex, std::size_t activeLength, int bitDepth) {
    const auto activeIndex = activeIndexForSource(sourceIndex, activeLength);
    const auto levels = static_cast<std::size_t>((1 << std::clamp(bitDepth, WaveTable::kMinBitDepth, WaveTable::kMaxBitDepth)) - 1);
    const auto codeCount = levels + 1;
    const auto code = std::min(levels, (activeIndex * codeCount) / activeLength);
    return (static_cast<float>(code) / static_cast<float>(levels) * 2.0f) - 1.0f;
}

float highFirstSquareValue(std::size_t sourceIndex, std::size_t activeLength) {
    const auto activeIndex = activeIndexForSource(sourceIndex, activeLength);
    return (activeIndex * 2) < activeLength ? 1.0f : -1.0f;
}
} // namespace

WaveTable::WaveTable() {
    generate(Shape::Square);
}

void WaveTable::setActiveLength(std::size_t length) {
    const auto newLength = std::clamp(length, kMinActiveLength, kMaxPoints);
    if (newLength == activeLength_) {
        return;
    }

    const auto oldLength = activeLength_;
    auto resampled = points_;

    for (std::size_t i = 0; i < newLength; ++i) {
        const auto srcFloat = static_cast<float>(i)
                            * static_cast<float>(oldLength)
                            / static_cast<float>(newLength);
        const auto srcFloor = static_cast<std::size_t>(srcFloat) % oldLength;
        const auto srcCeil  = (srcFloor + 1) % oldLength;
        const auto fraction = srcFloat - std::floor(srcFloat);
        const auto valA = points_[sourceIndexForActivePoint(srcFloor, oldLength)];
        const auto valB = points_[sourceIndexForActivePoint(srcCeil,  oldLength)];
        resampled[sourceIndexForActivePoint(i, newLength)] =
            valA + (valB - valA) * fraction;
    }

    points_       = resampled;
    activeLength_ = newLength;
}

void WaveTable::setBitDepth(int bits) {
    bitDepth_ = std::clamp(bits, kMinBitDepth, kMaxBitDepth);
}

void WaveTable::setSampleFormat(SampleFormat format) {
    sampleFormat_ = format;
}

void WaveTable::setLoop(bool enabled, std::size_t start, std::size_t end) {
    loopStart_ = std::min(start, activeLength_);
    loopEnd_ = std::min(end, activeLength_);
    loopEnabled_ = enabled && loopEnd_ > loopStart_ + 1;
}

void WaveTable::clearLoop() {
    loopEnabled_ = false;
    loopStart_ = 0;
    loopEnd_ = activeLength_;
}

void WaveTable::setPoint(std::size_t index, float value) {
    if (index >= points_.size()) {
        return;
    }

    points_[index] = std::clamp(value, -1.0f, 1.0f);
}

void WaveTable::generate(Shape shape, unsigned int seed) {
    clearLoop();

    auto noiseState = static_cast<std::uint16_t>((seed == 0 ? 1u : seed) & 0x7FFFu);
    auto activeNoise = std::array<float, kMaxPoints> {};
    if (shape == Shape::Noise) {
        for (std::size_t i = 0; i < activeLength_; ++i) {
            activeNoise[i] = lfsrNoiseValue(noiseState);
        }
    }

    for (std::size_t i = 0; i < points_.size(); ++i) {
        const auto activeIndex = activeIndexForSource(i, activeLength_);
        const auto phase = (static_cast<float>(activeIndex) + 0.5f) / static_cast<float>(activeLength_);
        switch (shape) {
        case Shape::Sine:
            points_[i] = std::sin(phase * 2.0f * kPi);
            break;
        case Shape::Triangle:
            points_[i] = lmmsTriangleSample(phase);
            break;
        case Shape::Saw:
            points_[i] = quantizedRampValue(i, activeLength_, bitDepth_);
            break;
        case Shape::SawDown:
            points_[i] = -quantizedRampValue(i, activeLength_, bitDepth_);
            break;
        case Shape::Square:
            points_[i] = highFirstSquareValue(i, activeLength_);
            break;
        case Shape::Square25:
            points_[i] = ((activeIndexForSource(i, activeLength_) * 4) < activeLength_) ? 1.0f : -1.0f;
            break;
        case Shape::Square125:
            points_[i] = ((activeIndexForSource(i, activeLength_) * 8) < activeLength_) ? 1.0f : -1.0f;
            break;
        case Shape::Noise:
            points_[i] = activeNoise[activeIndex];
            break;
        }
    }

}

void WaveTable::normalize() {
    const auto length = activeLength_;
    auto mean = 0.0f;
    for (std::size_t i = 0; i < length; ++i) {
        mean += points_[sourceIndexForActivePoint(i, length)];
    }
    mean /= static_cast<float>(length);

    float peak = 0.0f;
    for (std::size_t i = 0; i < length; ++i) {
        const auto sourceIndex = sourceIndexForActivePoint(i, length);
        points_[sourceIndex] -= mean;
        peak = std::max(peak, std::abs(points_[sourceIndex]));
    }

    if (peak > 0.0f) {
        for (std::size_t i = 0; i < length; ++i) {
            const auto sourceIndex = sourceIndexForActivePoint(i, length);
            points_[sourceIndex] = clampAudio(points_[sourceIndex] / peak);
        }
    }
}

void WaveTable::smooth() {
    auto smoothed = points_;

    for (std::size_t i = 0; i < activeLength_; ++i) {
        const auto prev = (i + activeLength_ - 1) % activeLength_;
        const auto next = (i + 1) % activeLength_;
        const auto prevSource = sourceIndexForActivePoint(prev, activeLength_);
        const auto source = sourceIndexForActivePoint(i, activeLength_);
        const auto nextSource = sourceIndexForActivePoint(next, activeLength_);
        smoothed[source] = (points_[prevSource] + (points_[source] * 2.0f) + points_[nextSource]) * 0.25f;
    }

    points_ = smoothed;
}

void WaveTable::quantize() {
    for (auto& point : points_) {
        point = quantizeValue(point, bitDepth_, sampleFormat_);
    }
}

float WaveTable::point(std::size_t index) const {
    return quantizeValue(
        points_[sourceIndexForActivePoint(index, activeLength_)],
        bitDepth_, sampleFormat_);
}

float WaveTable::sourcePoint(std::size_t index) const {
    if (index >= points_.size()) {
        return 0.0f;
    }

    return points_[index];
}

float WaveTable::displayPoint(std::size_t index, std::size_t activeLength, int bitDepth) const {
    const auto clampedLength = std::clamp(activeLength, kMinActiveLength, kMaxPoints);
    const auto clampedBits = std::clamp(bitDepth, kMinBitDepth, kMaxBitDepth);
    return quantizeValue(points_[sourceIndexForActivePoint(index, clampedLength)], clampedBits, sampleFormat_);
}

std::size_t WaveTable::activeLength() const {
    return activeLength_;
}

int WaveTable::bitDepth() const {
    return bitDepth_;
}

WaveTable::SampleFormat WaveTable::sampleFormat() const {
    return sampleFormat_;
}

bool WaveTable::loopEnabled() const {
    return loopEnabled_ && loopEnd_ <= activeLength_ && loopEnd_ > loopStart_ + 1;
}

std::size_t WaveTable::loopStart() const {
    return loopStart_;
}

std::size_t WaveTable::loopEnd() const {
    return loopEnd_;
}

std::size_t WaveTable::playbackCycleLength() const {
    return loopEnabled() ? loopEnd_ - loopStart_ : activeLength_;
}

std::size_t WaveTable::sourceIndexForActivePoint(std::size_t index, std::size_t activeLength) {
    const auto activeIndex = index % activeLength;
    return (activeIndex * kMaxPoints) / activeLength;
}

float WaveTable::quantizeValue(float value, int bitDepth, SampleFormat format) {
    const auto clamped = clampAudio(value);
    if (std::abs(clamped) < 1.0e-7f) {
        return 0.0f;
    }

    const auto clampedBits = std::clamp(bitDepth, kMinBitDepth, kMaxBitDepth);
    if (format == SampleFormat::Signed) {
        const auto magnitude = static_cast<float>(1 << (clampedBits - 1));
        const auto signedCode = std::clamp(std::round(clamped * magnitude), -magnitude, magnitude - 1.0f);
        return signedCode / magnitude;
    }

    const auto levels = (1 << clampedBits) - 1;
    const auto normalized = (clamped + 1.0f) * 0.5f;
    const auto quantized = std::round(normalized * static_cast<float>(levels))
        / static_cast<float>(levels);
    return (quantized * 2.0f) - 1.0f;
}

} // namespace hyperframe::dsp
