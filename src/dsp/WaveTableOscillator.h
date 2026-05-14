#pragma once

#include "dsp/CommandTable.h"
#include "dsp/WaveTable.h"

namespace hyperframe::dsp {

class WaveTableOscillator {
public:
    enum class Interpolation {
        Nearest,
        Linear
    };

    void setSampleRate(double sampleRate);
    void setFrequency(float frequencyHz);
    void setInterpolation(Interpolation interpolation);
    void resetPhase(float phase = 0.0f);
    float nextSample(const WaveTable& table, float phaseAmount, LsdjPhaseMode phaseMode);

private:
    float sampleAt(const WaveTable& table, float readPosition) const;
    static float wrapPhase(float phase);

    double sampleRate_ = 44100.0;
    float frequencyHz_ = 440.0f;
    float phase_ = 0.0f;
    float position_ = 0.0f;
    Interpolation interpolation_ = Interpolation::Nearest;
};

} // namespace hyperframe::dsp
