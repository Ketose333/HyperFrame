#pragma once

#include "dsp/AdsrEnvelope.h"
#include "dsp/Voice.h"
#include "dsp/CommandTable.h"
#include "dsp/WaveTable.h"
#include "dsp/WaveTableOscillator.h"

#include <array>
#include <cstddef>
#include <memory>
#include <vector>

namespace hyperframe::dsp {

struct SourceSnapshot {
    std::array<WaveTable, kWaveFrameCount> waveFrames {};
    std::shared_ptr<const std::vector<float>> rawStream;
    double rawStreamRate = 44100.0;
    double rawStreamRootNote = 60.0;
    RawStreamLoop rawStreamLoop {};
    bool rawStreamHoldInterpolation = false;
};

class SynthEngine {
public:
    static constexpr std::size_t kVoiceCount = 16;

    void setSampleRate(double sampleRate);
    void setEnvelopeSettings(const AdsrEnvelope::Settings& settings);
    void setInterpolation(WaveTableOscillator::Interpolation interpolation);
    void setCommandSettings(const CommandSettings& settings);
    void setCommandStep(int index, const CommandStep& step);
    void setRawStream(const std::vector<float>& samples, double sampleRate, double rootNote, RawStreamLoop loop = {}, bool holdInterpolation = false);
    void setSourceSnapshot(const SourceSnapshot& snapshot);
    void setOutputGain(float gain);
    void noteOn(int noteNumber, float velocity);
    void noteOff(int noteNumber);
    void reset();
    void setPitchBend(float semitones);
    float renderSample();
    int currentMotionStep() const;
    WaveTable& waveTable();
    const WaveTable& waveTable() const;
    WaveTable& waveFrame(int index);
    const WaveTable& waveFrame(int index) const;
    void setSelectedWaveFrame(int index);
    int selectedWaveFrame() const;

private:
    Voice& findVoiceForNoteOn();
    bool usesSharedCommandLoop() const;
    void advanceSharedCommandLoop();

    std::array<Voice, kVoiceCount> voices_{};
    std::array<WaveTable, kWaveFrameCount> waveFrames_{};
    std::shared_ptr<const std::vector<float>> rawStream_;
    double rawStreamRate_ = 44100.0;
    double rawStreamRootNote_ = 60.0;
    RawStreamLoop rawStreamLoop_ {};
    bool rawStreamHoldInterpolation_ = false;
    CommandTable commandTable_{};
    CommandSettings commandSettings_{};
    int selectedWaveFrame_ = 0;
    int sharedCommandStepIndex_ = 0;
    float sharedCommandStepTimeSeconds_ = 0.0f;
    float outputGain_ = 0.5f;
    float pitchBend_ = 0.0f;
    double sampleRate_ = 44100.0;
};

} // namespace hyperframe::dsp
