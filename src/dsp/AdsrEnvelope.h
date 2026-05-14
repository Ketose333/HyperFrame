#pragma once

namespace hyperframe::dsp {

class AdsrEnvelope {
public:
    struct Settings {
        float attackSeconds = 0.000f;
        float decaySeconds = 0.125f;
        float sustainLevel = 1.0f;
        float releaseSeconds = 0.031f;
    };

    void setSampleRate(double sampleRate);
    void setSettings(const Settings& settings);
    void noteOn();
    void noteOff();
    void reset();
    float nextSample();
    bool isActive() const;

private:
    enum class Stage {
        Idle,
        Attack,
        Decay,
        Sustain,
        Release
    };

    float secondsToStep(float seconds, float distance) const;

    double sampleRate_ = 44100.0;
    Settings settings_;
    Stage stage_ = Stage::Idle;
    float value_ = 0.0f;
    float releaseStep_ = 0.0f;
};

} // namespace hyperframe::dsp
