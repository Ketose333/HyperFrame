#pragma once

#include <array>
#include <cstddef>

namespace hyperframe::dsp {

class WaveTable {
public:
    static constexpr std::size_t kMaxPoints = 512;
    static constexpr std::size_t kMinActiveLength = 8;
    static constexpr std::size_t kDefaultActiveLength = 32;
    static constexpr int kDefaultBitDepth = 4;
    static constexpr int kMinBitDepth = 2;
    static constexpr int kMaxBitDepth = 16;

    enum class Shape {
        Sine,
        Triangle,
        Saw,
        SawDown,
        Square,
        Square25,
        Square125,
        Noise
    };

    enum class SampleFormat {
        Unsigned,
        Signed
    };

    WaveTable();

    void setActiveLength(std::size_t length);
    void setBitDepth(int bits);
    void setSampleFormat(SampleFormat format);
    void setLoop(bool enabled, std::size_t start, std::size_t end);
    void clearLoop();
    void setPoint(std::size_t index, float value);
    void generate(Shape shape, unsigned int seed = 1);
    void normalize();
    void smooth();
    void quantize();

    float point(std::size_t index) const;
    float sourcePoint(std::size_t index) const;
    float displayPoint(std::size_t index, std::size_t activeLength, int bitDepth) const;
    std::size_t activeLength() const;
    int bitDepth() const;
    SampleFormat sampleFormat() const;
    bool loopEnabled() const;
    std::size_t loopStart() const;
    std::size_t loopEnd() const;
    std::size_t playbackCycleLength() const;
    static float quantizeValue(float value, int bitDepth, SampleFormat format);

private:
    static std::size_t sourceIndexForActivePoint(std::size_t index, std::size_t activeLength);

    std::array<float, kMaxPoints> points_{};
    std::size_t activeLength_ = kDefaultActiveLength;
    int bitDepth_ = kDefaultBitDepth;
    SampleFormat sampleFormat_ = SampleFormat::Unsigned;
    bool loopEnabled_ = false;
    std::size_t loopStart_ = 0;
    std::size_t loopEnd_ = kDefaultActiveLength;
};

} // namespace hyperframe::dsp
