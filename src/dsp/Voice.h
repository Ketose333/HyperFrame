#pragma once

#include "dsp/AdsrEnvelope.h"
#include "dsp/CommandTable.h"
#include "dsp/WaveTable.h"
#include "dsp/WaveTableOscillator.h"

#include <vector>

namespace hyperframe::dsp {

class Voice {
public:
    void setSampleRate(double sampleRate);
    void setEnvelopeSettings(const AdsrEnvelope::Settings& settings);
    void setInterpolation(WaveTableOscillator::Interpolation interpolation);
    void noteOn(int noteNumber, float velocity);
    void slideTo(int noteNumber, float velocity);
    void noteOff(int noteNumber, bool releaseEnvelope = true);
    void reset();
    float render(const std::array<WaveTable, kWaveFrameCount>& frames,
                 const CommandTable& commandTable,
                 const CommandSettings& commandSettings,
                 const std::vector<float>& rawStream,
                 double rawStreamRate,
                 double rawStreamRootNote,
                 const RawStreamLoop& rawStreamLoop,
                 bool rawStreamHoldInterpolation,
                 int sharedCommandStepIndex,
                 float externalPitchBendSemitones = 0.0f);
    bool isActive() const;
    int commandStepIndex() const { return commandStepIndex_; }

private:
    static float midiNoteToFrequency(int noteNumber);
    static float wrapPhase(float phase);
    float renderRawStream(const std::vector<float>& rawStream, double rawStreamRate, double rootNote, float pitchSemitones, const RawStreamLoop& loop, bool holdInterpolation, int bitDepth);

    WaveTableOscillator oscillator_;
    AdsrEnvelope envelope_;
    float lastWaveOutput_ = 0.0f;
    int noteNumber_ = -1;
    float velocity_ = 0.0f;
    float baseMidiNote_ = 69.0f;
    float slideStartMidiNote_ = 69.0f;
    float targetMidiNote_ = 69.0f;
    float slideElapsedSeconds_ = 0.0f;
    int commandStepIndex_ = 0;
    int commandFrameIndex_ = 0;
    float commandStepTimeSeconds_ = 0.0f;
    double rawStreamPosition_ = 0.0;
    float wavePitchBendSemitones_ = 0.0f;
    float waveVibratoPhase_ = 0.0f;
    float hardwareJitterPhase_ = 0.0f;
    bool triggerPending_ = false;
    double sampleRate_ = 44100.0;
};

} // namespace hyperframe::dsp
