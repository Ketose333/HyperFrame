#include "dsp/Bytebeat.h"
#include "dsp/SynthEngine.h"
#include "dsp/WaveTableOscillator.h"

#include <algorithm>
#include <cmath>
#include <iostream>
#include <memory>
#include <vector>

namespace {
int countSilentSamples(hyperframe::dsp::WaveTableOscillator& oscillator,
                       const hyperframe::dsp::WaveTable& table,
                       hyperframe::dsp::LsdjPhaseMode phaseMode) {
    int silentSamples = 0;
    for (int i = 0; i < 32; ++i) {
        if (std::abs(oscillator.nextSample(table, 16.0f, phaseMode)) < 0.001f) {
            ++silentSamples;
        }
    }

    return silentSamples;
}

bool lsdjPhaseSmokeTest() {
    hyperframe::dsp::WaveTable table;
    table.setActiveLength(32);
    table.setBitDepth(4);
    table.generate(hyperframe::dsp::WaveTable::Shape::Square);

    auto makeOscillator = [] {
        hyperframe::dsp::WaveTableOscillator oscillator;
        oscillator.setSampleRate(32.0);
        oscillator.setFrequency(1.0f);
        oscillator.setInterpolation(hyperframe::dsp::WaveTableOscillator::Interpolation::Nearest);
        oscillator.resetPhase();
        return oscillator;
    };

    auto normal = makeOscillator();
    auto resync = makeOscillator();
    auto resyn2 = makeOscillator();

    const auto normalSilent = countSilentSamples(normal, table, hyperframe::dsp::LsdjPhaseMode::Normal);
    const auto resyncSilent = countSilentSamples(resync, table, hyperframe::dsp::LsdjPhaseMode::Resync);
    const auto resyn2Silent = countSilentSamples(resyn2, table, hyperframe::dsp::LsdjPhaseMode::Resync2);
    return normalSilent > 8 && resyncSilent == 0 && resyn2Silent == 0;
}

bool hardwareWaveSmokeTest() {
    hyperframe::dsp::SynthEngine engine;
    engine.setSampleRate(44100.0);
    engine.waveTable().setActiveLength(32);
    engine.waveTable().setBitDepth(4);
    engine.waveTable().generate(hyperframe::dsp::WaveTable::Shape::Saw);

    hyperframe::dsp::CommandSettings settings;
    settings.engineMode = hyperframe::dsp::CommandSettings::EngineMode::Wave;
    settings.mono = true;
    engine.setCommandSettings(settings);
    engine.noteOn(57, 0.9f);

    float peak = 0.0f;
    float minimum = 1.0f;
    float maximum = -1.0f;
    for (int i = 0; i < 4410; ++i) {
        const auto sample = engine.renderSample();
        peak = std::max(peak, std::abs(sample));
        minimum = std::min(minimum, sample);
        maximum = std::max(maximum, sample);
    }

    return peak > 0.05f && peak <= 1.0f && minimum < -0.05f && maximum > 0.05f;
}

bool hardwareWaveProfileSmokeTest() {
    using Mode = hyperframe::dsp::CommandSettings::EngineMode;
    using Format = hyperframe::dsp::WaveTable::SampleFormat;

    const auto* gb = hyperframe::dsp::hardwareWaveProfile(Mode::Wave);
    const auto* ws = hyperframe::dsp::hardwareWaveProfile(Mode::WonderSwan);
    const auto* pce = hyperframe::dsp::hardwareWaveProfile(Mode::PcEngine);
    const auto* scc = hyperframe::dsp::hardwareWaveProfile(Mode::Scc);

    if (gb == nullptr || ws == nullptr || pce == nullptr || scc == nullptr) {
        return false;
    }

    return gb->activeLength == 32
        && gb->bitDepth == 4
        && gb->sampleFormat == Format::Unsigned
        && gb->clockHz == 2097152.0f
        && gb->maximumDivider == 2048
        && gb->gameBoyWaveVolume
        && !gb->sharedMotionLoop
        && gb->dacBias < 0.0f
        && gb->clockJitterDepth > 0.0f
        && ws->bitDepth == 4
        && ws->clockHz == 3072000.0f
        && ws->sharedMotionLoop
        && pce->bitDepth == 5
        && pce->maximumDivider == 4096
        && scc->bitDepth == 8
        && scc->sampleFormat == Format::Signed
        && !hyperframe::dsp::isHardwareWaveEngineMode(Mode::Draw)
        && hyperframe::dsp::quantizeHardwareWaveFrequency(440.0f, Mode::Draw) == 440.0f
        && hyperframe::dsp::quantizeHardwareWaveFrequency(440.0f, Mode::Wave) > 0.0f;
}

bool waveOneStepFrameSmokeTest() {
    hyperframe::dsp::SynthEngine engine;
    engine.setSampleRate(32.0);
    engine.setOutputGain(1.0f);

    for (std::size_t point = 0; point < hyperframe::dsp::WaveTable::kMaxPoints; ++point) {
        engine.waveFrame(0).setPoint(point, -1.0f);
        engine.waveFrame(5).setPoint(point, 1.0f);
    }

    hyperframe::dsp::CommandSettings settings;
    settings.engineMode = hyperframe::dsp::CommandSettings::EngineMode::Wave;
    settings.enabled = true;
    settings.mono = true;
    settings.stepCount = 1;
    engine.setCommandSettings(settings);

    hyperframe::dsp::CommandStep step;
    step.frame = 5;
    engine.setCommandStep(0, step);
    engine.noteOn(69, 1.0f);

    engine.renderSample();
    return engine.renderSample() > 0.4f;
}

bool lfsrNoiseSmokeTest() {
    hyperframe::dsp::WaveTable first;
    hyperframe::dsp::WaveTable second;
    first.setActiveLength(32);
    second.setActiveLength(32);
    first.setBitDepth(4);
    second.setBitDepth(4);
    first.generate(hyperframe::dsp::WaveTable::Shape::Noise, 17);
    second.generate(hyperframe::dsp::WaveTable::Shape::Noise, 17);

    auto uniqueValues = std::vector<float> {};
    for (std::size_t i = 0; i < 32; ++i) {
        const auto value = first.point(i);
        if (std::abs(value - second.point(i)) > 0.0001f) {
            return false;
        }
        if (std::find_if(uniqueValues.begin(), uniqueValues.end(), [value](float existing) {
                return std::abs(existing - value) < 0.0001f;
            }) == uniqueValues.end()) {
            uniqueValues.push_back(value);
        }
    }

    return uniqueValues.size() >= 4;
}

bool dacBiasRenderSmokeTest() {
    hyperframe::dsp::SourceSnapshot snapshot;
    auto& frame = snapshot.waveFrames[0];
    frame.setActiveLength(32);
    frame.setBitDepth(4);
    for (std::size_t i = 0; i < hyperframe::dsp::WaveTable::kMaxPoints; ++i) {
        frame.setPoint(i, 0.0f);
    }
    snapshot.rawStream = std::make_shared<const std::vector<float>>();

    hyperframe::dsp::SynthEngine engine;
    engine.setSampleRate(44100.0);
    engine.setOutputGain(1.0f);
    engine.setSourceSnapshot(snapshot);

    hyperframe::dsp::CommandSettings settings;
    settings.engineMode = hyperframe::dsp::CommandSettings::EngineMode::Wave;
    settings.mono = true;
    engine.setCommandSettings(settings);
    engine.noteOn(60, 1.0f);

    float sum = 0.0f;
    for (int i = 0; i < 128; ++i) {
        sum += engine.renderSample();
    }

    return (sum / 128.0f) < -0.005f;
}

bool clockJitterSmokeTest() {
    using Mode = hyperframe::dsp::CommandSettings::EngineMode;
    const auto* profile = hyperframe::dsp::hardwareWaveProfile(Mode::Wave);
    if (profile == nullptr || profile->clockJitterDepth <= 0.0f) {
        return false;
    }

    const auto center = hyperframe::dsp::hardwareWaveClockJitterFactor(*profile, 0.0f);
    const auto high = hyperframe::dsp::hardwareWaveClockJitterFactor(*profile, 0.25f);
    const auto low = hyperframe::dsp::hardwareWaveClockJitterFactor(*profile, 0.75f);
    return std::abs(center - 1.0f) < 0.0001f
        && high > 1.0f
        && low < 1.0f
        && std::abs((high - 1.0f) - (1.0f - low)) < 0.0001f;
}

bool sourceSnapshotSmokeTest() {
    hyperframe::dsp::SourceSnapshot snapshot;
    auto& frame = snapshot.waveFrames[0];
    frame.setActiveLength(32);
    frame.setBitDepth(4);
    frame.generate(hyperframe::dsp::WaveTable::Shape::Square);
    snapshot.rawStream = std::make_shared<const std::vector<float>>();

    hyperframe::dsp::SynthEngine engine;
    engine.setSampleRate(44100.0);
    engine.setOutputGain(1.0f);
    engine.setSourceSnapshot(snapshot);
    engine.noteOn(69, 1.0f);

    float peak = 0.0f;
    for (int i = 0; i < 1024; ++i) {
        peak = std::max(peak, std::abs(engine.renderSample()));
    }

    return peak > 0.1f && peak <= 1.0f;
}

bool drawGlideSmokeTest() {
    hyperframe::dsp::SynthEngine engine;
    engine.setSampleRate(44100.0);
    engine.waveTable().setActiveLength(32);
    engine.waveTable().setBitDepth(4);
    engine.waveTable().generate(hyperframe::dsp::WaveTable::Shape::Square);

    hyperframe::dsp::CommandSettings settings;
    settings.engineMode = hyperframe::dsp::CommandSettings::EngineMode::Draw;
    settings.slideTimeSeconds = 0.25f;
    settings.mono = false;
    engine.setCommandSettings(settings);

    engine.noteOn(48, 0.8f);
    for (int i = 0; i < 512; ++i) {
        engine.renderSample();
    }

    engine.noteOn(72, 0.8f);
    float peak = 0.0f;
    int activeSamples = 0;
    for (int i = 0; i < 4096; ++i) {
        const auto sample = engine.renderSample();
        peak = std::max(peak, std::abs(sample));
        if (std::abs(sample) > 0.001f) {
            ++activeSamples;
        }
    }

    return peak > 0.05f && peak <= 1.0f && activeSamples > 256;
}

bool bytebeatSmokeTest() {
    auto value = std::int64_t { 0 };
    if (!hyperframe::dsp::evaluateBytebeatExpression("t*((t>>12|t>>8)&63&t>>4)", 4096, value)) {
        return false;
    }

    if (!hyperframe::dsp::evaluateBytebeatExpression("t<32?t*8:t>>1", 12, value) || value != 96) {
        return false;
    }

    std::vector<float> samples;
    if (!hyperframe::dsp::renderBytebeat("t", 8000.0, 0.0, 0.01, samples)) {
        return false;
    }

    std::vector<float> tickSamples;
    if (!hyperframe::dsp::renderBytebeatTicks("t", 16, 32, tickSamples)) {
        return false;
    }

    return samples.size() == 80
        && std::abs(samples.front() + 1.0f) < 0.0001f
        && samples.back() > samples.front()
        && tickSamples.size() == 32
        && tickSamples.front() < tickSamples.back()
        && !hyperframe::dsp::renderBytebeat("t+", 8000.0, 0.0, 0.01, samples);
}

bool rawStreamHoldInterpolationSmokeTest() {
    hyperframe::dsp::SynthEngine engine;
    engine.setSampleRate(4.0);
    engine.setOutputGain(1.0f);

    hyperframe::dsp::CommandSettings settings;
    settings.engineMode = hyperframe::dsp::CommandSettings::EngineMode::Raw;
    engine.setCommandSettings(settings);
    engine.setRawStream({ -1.0f, 1.0f, -1.0f, 1.0f }, 2.0, 69, {}, true);
    engine.noteOn(69, 1.0f);

    const auto first = engine.renderSample();
    const auto second = engine.renderSample();
    return first < -0.9f && second < -0.9f;
}

bool rawStreamLoopSmokeTest() {
    hyperframe::dsp::SynthEngine engine;
    engine.setSampleRate(4.0);
    engine.setOutputGain(1.0f);

    hyperframe::dsp::CommandSettings settings;
    settings.engineMode = hyperframe::dsp::CommandSettings::EngineMode::Raw;
    engine.setCommandSettings(settings);

    hyperframe::dsp::RawStreamLoop loop;
    loop.enabled = true;
    loop.start = 0;
    loop.end = 4;
    engine.setRawStream({ -1.0f, -0.5f, 0.5f, 1.0f }, 4.0, 69, loop, true);
    engine.noteOn(69, 1.0f);

    auto activeCount = 0;
    for (int i = 0; i < 12; ++i) {
        const auto sample = engine.renderSample();
        if (std::abs(sample) > 0.01f) {
            ++activeCount;
        }
    }

    return activeCount == 12;
}

bool rawStreamPlayStartSmokeTest() {
    hyperframe::dsp::SynthEngine engine;
    engine.setSampleRate(4.0);
    engine.setOutputGain(1.0f);

    hyperframe::dsp::CommandSettings settings;
    settings.engineMode = hyperframe::dsp::CommandSettings::EngineMode::Raw;
    engine.setCommandSettings(settings);

    hyperframe::dsp::RawStreamLoop loop;
    loop.playStart = 2;
    engine.setRawStream({ -1.0f, -0.5f, 0.5f, 1.0f }, 4.0, 69, loop, true);
    engine.noteOn(69, 1.0f);

    return engine.renderSample() > 0.4f;
}

bool rawStreamFullPlaybackIgnoresNoteOffSmokeTest() {
    hyperframe::dsp::SynthEngine engine;
    engine.setSampleRate(4.0);
    engine.setOutputGain(1.0f);

    hyperframe::dsp::AdsrEnvelope::Settings envelope;
    envelope.attackSeconds = 0.0f;
    envelope.decaySeconds = 0.0f;
    envelope.sustainLevel = 1.0f;
    envelope.releaseSeconds = 0.0f;
    engine.setEnvelopeSettings(envelope);

    hyperframe::dsp::CommandSettings settings;
    settings.engineMode = hyperframe::dsp::CommandSettings::EngineMode::Raw;
    settings.rawBitDepth = hyperframe::dsp::WaveTable::kMaxBitDepth;
    settings.rawPlayFull = true;
    engine.setCommandSettings(settings);
    engine.setRawStream({ 0.75f, 0.75f, 0.75f, 0.75f }, 4.0, 69, {}, true);

    engine.noteOn(69, 1.0f);
    const auto first = engine.renderSample();
    engine.noteOff(69);
    const auto second = engine.renderSample();
    const auto third = engine.renderSample();
    const auto fourth = engine.renderSample();
    const auto afterEnd = engine.renderSample();

    return first > 0.7f
        && second > 0.7f
        && third > 0.7f
        && fourth > 0.7f
        && std::abs(afterEnd) < 0.001f;
}

bool rawStreamGateNoteOffSmokeTest() {
    hyperframe::dsp::SynthEngine engine;
    engine.setSampleRate(4.0);
    engine.setOutputGain(1.0f);

    hyperframe::dsp::AdsrEnvelope::Settings envelope;
    envelope.attackSeconds = 0.0f;
    envelope.decaySeconds = 0.0f;
    envelope.sustainLevel = 1.0f;
    envelope.releaseSeconds = 0.0f;
    engine.setEnvelopeSettings(envelope);

    hyperframe::dsp::CommandSettings settings;
    settings.engineMode = hyperframe::dsp::CommandSettings::EngineMode::Raw;
    settings.rawBitDepth = hyperframe::dsp::WaveTable::kMaxBitDepth;
    settings.rawPlayFull = false;
    engine.setCommandSettings(settings);
    engine.setRawStream({ 0.75f, 0.75f, 0.75f, 0.75f }, 4.0, 69, {}, true);

    engine.noteOn(69, 1.0f);
    const auto first = engine.renderSample();
    engine.noteOff(69);
    const auto second = engine.renderSample();

    return first > 0.7f && std::abs(second) < 0.001f;
}

bool rawStreamBitDepthSmokeTest() {
    hyperframe::dsp::SynthEngine engine;
    engine.setSampleRate(4.0);
    engine.setOutputGain(1.0f);

    hyperframe::dsp::CommandSettings settings;
    settings.engineMode = hyperframe::dsp::CommandSettings::EngineMode::Raw;
    settings.rawBitDepth = 2;
    engine.setCommandSettings(settings);
    engine.setRawStream({ 0.90f, 0.90f }, 4.0, 69, {}, true);
    engine.noteOn(69, 1.0f);

    const auto sample = engine.renderSample();
    return sample > 0.49f && sample < 0.51f;
}

bool drawLoopMetadataSurvivesLengthChangeSmokeTest() {
    hyperframe::dsp::WaveTable table;
    table.setActiveLength(128);
    table.setLoop(true, 64, 96);

    if (!table.loopEnabled() || table.loopStart() != 64 || table.loopEnd() != 96 || table.playbackCycleLength() != 32) {
        return false;
    }

    table.setActiveLength(32);
    if (table.loopEnabled() || table.loopStart() != 64 || table.loopEnd() != 96 || table.playbackCycleLength() != 32) {
        return false;
    }

    table.setActiveLength(128);
    return table.loopEnabled() && table.loopStart() == 64 && table.loopEnd() == 96 && table.playbackCycleLength() == 32;
}
} // namespace

int main() {
    hyperframe::dsp::SynthEngine engine;
    engine.setSampleRate(44100.0);
    engine.waveTable().setActiveLength(32);
    engine.waveTable().setBitDepth(4);
    engine.waveTable().generate(hyperframe::dsp::WaveTable::Shape::Square);
    engine.noteOn(69, 0.8f);

    float peak = 0.0f;
    float minimum = 1.0f;
    float maximum = -1.0f;
    int signChanges = 0;
    float previous = 0.0f;

    for (int i = 0; i < 4410; ++i) {
        const auto sample = engine.renderSample();
        peak = std::max(peak, std::abs(sample));
        minimum = std::min(minimum, sample);
        maximum = std::max(maximum, sample);

        if (i > 0 && ((previous < 0.0f && sample >= 0.0f) || (previous >= 0.0f && sample < 0.0f))) {
            ++signChanges;
        }

        previous = sample;
    }

    if (peak <= 0.0f || peak > 1.0f || minimum >= -0.1f || maximum <= 0.1f || signChanges < 8) {
        std::cerr << "Unexpected render: peak=" << peak
                  << " min=" << minimum
                  << " max=" << maximum
                  << " signChanges=" << signChanges << '\n';
        return 1;
    }

    if (!lsdjPhaseSmokeTest()) {
        std::cerr << "Unexpected LSDJ phase transform behavior\n";
        return 1;
    }

    if (!hardwareWaveSmokeTest()) {
        std::cerr << "Unexpected hardware wave render behavior\n";
        return 1;
    }

    if (!hardwareWaveProfileSmokeTest()) {
        std::cerr << "Unexpected hardware wave profile behavior\n";
        return 1;
    }

    if (!waveOneStepFrameSmokeTest()) {
        std::cerr << "Unexpected one-step Wave frame behavior\n";
        return 1;
    }

    if (!lfsrNoiseSmokeTest()) {
        std::cerr << "Unexpected LFSR noise behavior\n";
        return 1;
    }

    if (!dacBiasRenderSmokeTest()) {
        std::cerr << "Unexpected DAC bias render behavior\n";
        return 1;
    }

    if (!clockJitterSmokeTest()) {
        std::cerr << "Unexpected clock jitter behavior\n";
        return 1;
    }

    if (!sourceSnapshotSmokeTest()) {
        std::cerr << "Unexpected source snapshot behavior\n";
        return 1;
    }

    if (!drawGlideSmokeTest()) {
        std::cerr << "Unexpected Draw glide behavior\n";
        return 1;
    }

    if (!bytebeatSmokeTest()) {
        std::cerr << "Unexpected bytebeat parser/render behavior\n";
        return 1;
    }

    if (!rawStreamHoldInterpolationSmokeTest()) {
        std::cerr << "Unexpected raw stream hold interpolation behavior\n";
        return 1;
    }

    if (!rawStreamLoopSmokeTest()) {
        std::cerr << "Unexpected raw stream loop behavior\n";
        return 1;
    }

    if (!rawStreamPlayStartSmokeTest()) {
        std::cerr << "Unexpected raw stream play-start behavior\n";
        return 1;
    }

    if (!rawStreamFullPlaybackIgnoresNoteOffSmokeTest()) {
        std::cerr << "Unexpected raw stream full playback behavior\n";
        return 1;
    }

    if (!rawStreamGateNoteOffSmokeTest()) {
        std::cerr << "Unexpected raw stream gate note-off behavior\n";
        return 1;
    }

    if (!rawStreamBitDepthSmokeTest()) {
        std::cerr << "Unexpected raw stream bit-depth behavior\n";
        return 1;
    }

    if (!drawLoopMetadataSurvivesLengthChangeSmokeTest()) {
        std::cerr << "Unexpected Draw loop metadata preservation behavior\n";
        return 1;
    }

    std::cout << "core smoke test passed, peak=" << peak
              << " min=" << minimum
              << " max=" << maximum
              << " signChanges=" << signChanges << '\n';
    return 0;
}
