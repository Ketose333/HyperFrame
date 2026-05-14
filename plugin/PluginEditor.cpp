#include "PluginEditor.h"

#include <algorithm>
#include <cmath>

namespace {
constexpr auto kParamWaveLength = "wave_length";
constexpr auto kParamWaveBits = "wave_bits";
constexpr auto kParamInterpolation = "interpolation";
constexpr auto kParamAttack = "attack";
constexpr auto kParamDecay = "decay";
constexpr auto kParamSustain = "sustain";
constexpr auto kParamRelease = "release";
constexpr auto kParamLsdjPhase = "lsdj_phase";
constexpr auto kParamLsdjPhaseMode = "lsdj_phase_mode";
constexpr auto kParamSlideTime = "slide_time";
constexpr auto kParamSelectedFrame = "selected_frame";
constexpr auto kParamMotionTable = "motion_table";
constexpr auto kParamMotionLoop = "motion_loop";
constexpr auto kParamMotionClockMode = "motion_clock_mode";
constexpr auto kParamMotionRate = "motion_rate";
constexpr auto kParamMotionSteps = "motion_steps";
constexpr auto kParamMotionLoopStart = "motion_loop_start";
constexpr auto kParamEngineMode = "engine_mode";
constexpr auto kParamRawPlayFull = "raw_play_full";
constexpr auto kParamMonoMode = "mono_mode";
constexpr auto kParamGain = "gain";
constexpr double kGbAmplitudeStep = 1.0 / 15.0;
constexpr double kLsdjWaveVolumeStep = 1.0 / 3.0;

float parameterValue(juce::AudioProcessorValueTreeState& parameters, const char* id) {
    return parameters.getRawParameterValue(id)->load();
}

void setParameterValueFromEditor(juce::AudioProcessorValueTreeState& parameters, const juce::String& id, float value) {
    if (auto* parameter = parameters.getParameter(id)) {
        if (auto* ranged = dynamic_cast<juce::RangedAudioParameter*>(parameter)) {
            parameter->beginChangeGesture();
            parameter->setValueNotifyingHost(ranged->convertTo0to1(value));
            parameter->endChangeGesture();
        }
    }
}

bool isLsdjWaveMode(juce::AudioProcessorValueTreeState& parameters) {
    return hyperframe::dsp::isGameBoyWaveEngineMode(static_cast<hyperframe::dsp::CommandSettings::EngineMode>(
        static_cast<int>(parameterValue(parameters, kParamEngineMode))));
}

bool isHardwareWaveMode(juce::AudioProcessorValueTreeState& parameters) {
    return hyperframe::dsp::isHardwareWaveEngineMode(static_cast<hyperframe::dsp::CommandSettings::EngineMode>(
        static_cast<int>(parameterValue(parameters, kParamEngineMode))));
}

bool isRawMode(juce::AudioProcessorValueTreeState& parameters) {
    return static_cast<int>(parameterValue(parameters, kParamEngineMode)) == 0;
}

std::size_t hardwareWaveLength(juce::AudioProcessorValueTreeState& parameters) {
    return static_cast<std::size_t>(hyperframe::dsp::hardwareWaveLength(static_cast<hyperframe::dsp::CommandSettings::EngineMode>(
        static_cast<int>(parameterValue(parameters, kParamEngineMode)))));
}

int hardwareWaveBitDepth(juce::AudioProcessorValueTreeState& parameters) {
    return hyperframe::dsp::hardwareWaveBitDepth(static_cast<hyperframe::dsp::CommandSettings::EngineMode>(
        static_cast<int>(parameterValue(parameters, kParamEngineMode))));
}

std::size_t effectiveWaveLength(juce::AudioProcessorValueTreeState& parameters) {
    return isHardwareWaveMode(parameters)
        ? hardwareWaveLength(parameters)
        : static_cast<std::size_t>(parameterValue(parameters, kParamWaveLength));
}

int effectiveWaveBitDepth(juce::AudioProcessorValueTreeState& parameters) {
    return isHardwareWaveMode(parameters)
        ? hardwareWaveBitDepth(parameters)
        : static_cast<int>(parameterValue(parameters, kParamWaveBits));
}

juce::Colour panelColour() {
    return juce::Colour::fromRGB(22, 24, 27);
}

juce::Colour accentColour() {
    return juce::Colour::fromRGB(0x91, 0xED, 0xFC);
}

juce::String motionParamId(int step, const char* field) {
    return "motion_step_" + juce::String(step).paddedLeft('0', 2) + "_" + field;
}

int roundedInt(double value) {
    return static_cast<int>(std::round(value));
}

juce::String envelopeTimeText(double seconds) {
    const auto clampedSeconds = std::max(0.0, seconds);
    return clampedSeconds < 1.0
        ? juce::String(roundedInt(clampedSeconds * 1000.0)) + "ms"
        : juce::String(clampedSeconds, clampedSeconds < 10.0 ? 2 : 1) + "s";
}

double envelopeTimeValue(const juce::String& text) {
    auto trimmed = text.trim().toLowerCase();
    if (trimmed.endsWith("ms")) {
        return trimmed.dropLastCharacters(2).getDoubleValue() / 1000.0;
    }
    if (trimmed.endsWithChar('s')) {
        return trimmed.dropLastCharacters(1).getDoubleValue();
    }
    return trimmed.getDoubleValue();
}

juce::String percentText(double value) {
    return juce::String(std::clamp(roundedInt(value * 100.0), 0, 100)) + "%";
}

double percentValue(const juce::String& text) {
    auto trimmed = text.trim();
    if (trimmed.endsWithChar('%')) {
        return trimmed.dropLastCharacters(1).getDoubleValue() / 100.0;
    }

    const auto numeric = trimmed.getDoubleValue();
    if (!trimmed.containsChar('.') && numeric > 1.0 && numeric <= 100.0) {
        return numeric / 100.0;
    }
    return numeric;
}

juce::String hzText(double value) {
    return juce::String(std::clamp(roundedInt(value), 1, 64)) + "Hz";
}

double hzValue(const juce::String& text) {
    return static_cast<double>(std::clamp(roundedInt(text.trim().retainCharacters("0123456789.").getDoubleValue()), 1, 64));
}

bool isBpmMotionClock(juce::AudioProcessorValueTreeState& parameters) {
    return parameterValue(parameters, kParamMotionClockMode) > 0.5f;
}

double motionSyncDivision(double value) {
    static constexpr std::array<double, 7> divisions { 1.0, 2.0, 4.0, 8.0, 16.0, 32.0, 64.0 };
    const auto clamped = std::clamp(value, divisions.front(), divisions.back());
    return *std::min_element(divisions.begin(), divisions.end(), [clamped](double left, double right) {
        return std::abs(left - clamped) < std::abs(right - clamped);
    });
}

juce::String motionRateText(double value, bool bpmClock) {
    if (bpmClock) {
        return "1/" + juce::String(static_cast<int>(motionSyncDivision(value)));
    }

    return juce::String(std::clamp(roundedInt(value), 1, 64));
}

double motionRateValue(const juce::String& text) {
    auto trimmed = text.trim().toLowerCase();
    if (trimmed.containsChar('/')) {
        return motionSyncDivision(trimmed.fromLastOccurrenceOf("/", false, false).getDoubleValue());
    }

    return hzValue(trimmed);
}

double slideSyncDivisionForValue(double value) {
    if (value <= 0.0) {
        return 0.0;
    }

    return motionSyncDivision(value * 32.0);
}

double slideSyncDivisionForSeconds(double seconds, double bpm) {
    if (seconds <= 0.0) {
        return 0.0;
    }

    const auto safeBpm = std::clamp(bpm, 20.0, 400.0);
    static constexpr std::array<double, 7> divisions { 1.0, 2.0, 4.0, 8.0, 16.0, 32.0, 64.0 };
    return *std::min_element(divisions.begin(), divisions.end(), [safeBpm, seconds](double left, double right) {
        const auto leftSeconds = (60.0 / safeBpm) * (4.0 / left);
        const auto rightSeconds = (60.0 / safeBpm) * (4.0 / right);
        return std::abs(leftSeconds - seconds) < std::abs(rightSeconds - seconds);
    });
}

double slideSecondsForDivision(double division, double bpm) {
    const auto safeBpm = std::clamp(bpm, 20.0, 400.0);
    return (60.0 / safeBpm) * (4.0 / motionSyncDivision(division));
}

juce::String slideTimeText(double seconds, bool bpmClock, double bpm) {
    if (seconds <= 0.0) {
        return "Off";
    }

    juce::ignoreUnused(bpm);
    if (bpmClock) {
        return "1/" + juce::String(static_cast<int>(slideSyncDivisionForValue(seconds)));
    }

    return juce::String(static_cast<int>(slideSyncDivisionForValue(seconds)));
}

double slideTimeValue(const juce::String& text, bool bpmClock, double bpm) {
    auto trimmed = text.trim().toLowerCase();
    if (trimmed == "off" || trimmed == "0") {
        return 0.0;
    }

    if (trimmed.containsChar('/')) {
        return motionSyncDivision(trimmed.fromLastOccurrenceOf("/", false, false).getDoubleValue()) / 32.0;
    }

    if (trimmed.endsWith("ms") || trimmed.endsWithChar('s')) {
        const auto seconds = envelopeTimeValue(trimmed);
        return bpmClock ? slideSyncDivisionForSeconds(seconds, bpm) / 32.0
                        : slideSyncDivisionForValue(seconds) / 32.0;
    }

    return motionSyncDivision(trimmed.getDoubleValue()) / 32.0;
}

HyperFrameAudioProcessor::AudioSourceImportTarget audioSourceImportTargetFromSelection(int selectedId) {
    using Target = HyperFrameAudioProcessor::AudioSourceImportTarget;
    switch (selectedId) {
    case 2:
        return Target::Wavetable;
    case 3:
        return Target::WaveFrames;
    case 1:
    default:
        return Target::RawStream;
    }
}

HyperFrameAudioProcessor::BytebeatImportTarget bytebeatImportTargetFromSelection(int selectedId) {
    using Target = HyperFrameAudioProcessor::BytebeatImportTarget;
    return selectedId == 2 ? Target::WaveFrames : Target::RawStream;
}

enum class LoopOverlayScale {
    PointSamples,
    CellBoundaries
};

static float rawSampleToX(std::size_t sample, std::size_t sampleCount,
                           juce::Rectangle<float> graph) {
    if (sampleCount <= 1) return graph.getX();
    return graph.getX() + graph.getWidth()
           * static_cast<float>(sample) / static_cast<float>(sampleCount - 1);
}

static std::size_t rawXToSample(float x, std::size_t sampleCount,
                                juce::Rectangle<float> graph) {
    if (sampleCount <= 1) return 0;
    const float ratio = (x - graph.getX()) / graph.getWidth();
    return static_cast<std::size_t>(
        std::lround(std::clamp(ratio, 0.0f, 1.0f)
                    * static_cast<float>(sampleCount - 1)));
}

void paintLoopOverlay(juce::Graphics& graphics,
    juce::Rectangle<float> graph,
    const HyperFrameAudioProcessor::LoopInfo& loopInfo,
    LoopOverlayScale scale,
    float fillAlpha) {
    if (!loopInfo.hasContent || loopInfo.sampleCount <= 1 || loopInfo.end <= loopInfo.start) {
        return;
    }

    const auto denominator = scale == LoopOverlayScale::PointSamples
        ? static_cast<float>(loopInfo.sampleCount - 1)
        : static_cast<float>(loopInfo.sampleCount);
    const auto displayEnd = scale == LoopOverlayScale::PointSamples
        ? std::min(loopInfo.end, loopInfo.sampleCount - 1)
        : loopInfo.end;
    const auto startX = graph.getX() + graph.getWidth() * static_cast<float>(loopInfo.start) / denominator;
    const auto endX = graph.getX() + graph.getWidth() * static_cast<float>(displayEnd) / denominator;

    const float markerAlpha = loopInfo.enabled ? 1.0f : 0.28f;

    if (loopInfo.enabled && loopInfo.end > loopInfo.start + 1) {
        graphics.setColour(juce::Colour::fromRGB(0xFF, 0xD1, 0x66).withAlpha(fillAlpha));
        graphics.fillRect(juce::Rectangle<float>(startX, graph.getY(), std::max(1.0f, endX - startX), graph.getHeight()));
    }

    graphics.setColour(juce::Colour::fromRGB(0xFF, 0xD1, 0x66).withAlpha(markerAlpha));
    graphics.drawVerticalLine(static_cast<int>(std::round(startX)), graph.getY(), graph.getBottom());
    graphics.drawVerticalLine(static_cast<int>(std::round(endX)), graph.getY(), graph.getBottom());

    if (loopInfo.enabled && loopInfo.end > loopInfo.start + 1) {
        graphics.setColour(juce::Colour::fromRGB(0xFF, 0xD1, 0x66));
        graphics.setFont(juce::FontOptions(11.0f, juce::Font::bold));
        graphics.drawText("LOOP", juce::Rectangle<float>(startX + 4.0f, graph.getY() + 4.0f, std::max(42.0f, endX - startX - 8.0f), 16.0f), juce::Justification::centredLeft, false);
    }
}

class AudioSourceImportOptionsComponent final : public juce::Component {
public:
    AudioSourceImportOptionsComponent() {
        configureToggle(normalizeToggle_, "Normalize converted wave", true);
        configureToggle(zeroCrossToggle_, "Snap cycle to upward zero crossing", true);
        setSize(360, 52);
    }

    void resized() override {
        auto bounds = getLocalBounds();
        for (auto* toggle : { &normalizeToggle_, &zeroCrossToggle_ }) {
            toggle->setBounds(bounds.removeFromTop(24));
        }
    }

    bool normalize() const {
        return normalizeToggle_.getToggleState();
    }

    bool zeroCrossingSnap() const {
        return zeroCrossToggle_.getToggleState();
    }

private:
    static void configureToggle(juce::ToggleButton& toggle, const juce::String& text, bool enabled) {
        toggle.setButtonText(text);
        toggle.setToggleState(enabled, juce::dontSendNotification);
        toggle.setColour(juce::ToggleButton::tickColourId, accentColour());
        toggle.setColour(juce::ToggleButton::textColourId, juce::Colour::fromRGB(218, 224, 228));
        toggle.setSize(360, 24);
    }

    juce::ToggleButton normalizeToggle_;
    juce::ToggleButton zeroCrossToggle_;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(AudioSourceImportOptionsComponent)
};
} // namespace

WaveformDisplay::WaveformDisplay(HyperFrameAudioProcessor& processor)
    : processor_(processor) {
    setMouseCursor(juce::MouseCursor::CrosshairCursor);
    startTimerHz(24);
}

void WaveformDisplay::paint(juce::Graphics& graphics) {
    const auto bounds = getLocalBounds().toFloat();
    const auto graph = graphBounds();
    auto& parameters = processor_.parameters();
    const auto rawMode = isRawMode(parameters);
    const auto activeLength = effectiveWaveLength(parameters);
    const auto bitDepth = effectiveWaveBitDepth(parameters);
    const auto interpolation = !isLsdjWaveMode(parameters) && parameterValue(parameters, kParamInterpolation) > 0.5f;

    graphics.setColour(panelColour());
    graphics.fillRoundedRectangle(bounds, 8.0f);

    graphics.setColour(juce::Colour::fromRGB(42, 47, 52));
    graphics.drawRoundedRectangle(graph, 4.0f, 1.0f);

    graphics.setColour(juce::Colour::fromRGB(36, 41, 45));
    for (int i = 1; i < 8; ++i) {
        const auto x = graph.getX() + graph.getWidth() * static_cast<float>(i) / 8.0f;
        graphics.drawVerticalLine(static_cast<int>(std::round(x)), graph.getY(), graph.getBottom());
    }

    for (int i = 1; i < 4; ++i) {
        const auto y = graph.getY() + graph.getHeight() * static_cast<float>(i) / 4.0f;
        graphics.drawHorizontalLine(static_cast<int>(std::round(y)), graph.getX(), graph.getRight());
    }

    const auto zeroY = graph.getCentreY();
    graphics.setColour(juce::Colour::fromRGB(84, 91, 98));
    graphics.drawHorizontalLine(static_cast<int>(std::round(zeroY)), graph.getX(), graph.getRight());

    if (rawMode) {
        const auto bucketCount = static_cast<std::size_t>(std::max(1, static_cast<int>(std::round(graph.getWidth()))));
        const auto hasRawStream = processor_.rawStreamDisplayRanges(bucketCount, sampleDisplayMinimums_, sampleDisplayMaximums_);
        juce::Path centrePath;
        for (std::size_t bucket = 0; bucket < bucketCount; ++bucket) {
            const auto x = graph.getX() + (static_cast<float>(bucket) + 0.5f) * graph.getWidth() / static_cast<float>(bucketCount);
            const auto yMin = juce::jmap(sampleDisplayMinimums_[bucket], -1.0f, 1.0f, graph.getBottom(), graph.getY());
            const auto yMax = juce::jmap(sampleDisplayMaximums_[bucket], -1.0f, 1.0f, graph.getBottom(), graph.getY());
            graphics.setColour(accentColour().withAlpha(0.42f));
            graphics.drawVerticalLine(static_cast<int>(std::round(x)), yMax, yMin);

            const auto centreY = (yMin + yMax) * 0.5f;
            if (bucket == 0) {
                centrePath.startNewSubPath(x, centreY);
            } else {
                centrePath.lineTo(x, centreY);
            }
        }

        graphics.setColour(accentColour());
        graphics.strokePath(centrePath, juce::PathStrokeType(1.5f));

        const auto loopInfo = processor_.rawStreamLoopInfo();
        paintLoopOverlay(graphics, graph, loopInfo, LoopOverlayScale::PointSamples, 0.18f);
        if (loopInfo.hasContent && loopInfo.sampleCount > 1) {
            const auto startX = graph.getX() + (static_cast<float>(std::min(loopInfo.playStart, loopInfo.sampleCount - 1)) / static_cast<float>(loopInfo.sampleCount - 1)) * graph.getWidth();
            graphics.setColour(juce::Colour::fromRGB(255, 218, 120).withAlpha(0.72f));
            graphics.drawVerticalLine(static_cast<int>(std::round(startX)), graph.getY(), graph.getBottom());
            graphics.setFont(juce::FontOptions(11.0f, juce::Font::bold));
            graphics.drawText("START", juce::Rectangle<float>(startX + 4.0f, graph.getBottom() - 20.0f, 54.0f, 16.0f), juce::Justification::centredLeft, false);
        }

        graphics.setFont(juce::FontOptions(13.0f, juce::Font::plain));
        graphics.setColour(juce::Colour::fromRGB(152, 161, 168).withAlpha(hasRawStream ? 0.32f : 0.82f));
        graphics.drawText(hasRawStream ? "Raw" : "Import Raw Stream", graph.reduced(8.0f, 6.0f), juce::Justification::bottomRight, false);
        return;
    }

    juce::Path steppedPath;
    juce::Path pointPath;
    const auto pointWidth = graph.getWidth() / static_cast<float>(activeLength);

    for (std::size_t i = 0; i < activeLength; ++i) {
        const auto value = processor_.waveDisplayPoint(i, activeLength, bitDepth);
        const auto x0 = graph.getX() + static_cast<float>(i) * pointWidth;
        const auto x1 = graph.getX() + static_cast<float>(i + 1) * pointWidth;
        const auto y = juce::jmap(value, -1.0f, 1.0f, graph.getBottom(), graph.getY());
        const auto centreX = x0 + pointWidth * 0.5f;

        if (i == 0) {
            steppedPath.startNewSubPath(x0, y);
        } else {
            steppedPath.lineTo(x0, y);
        }

        steppedPath.lineTo(x1, y);
        pointPath.addEllipse(centreX - 2.0f, y - 2.0f, 4.0f, 4.0f);
    }

    graphics.setColour(accentColour().withAlpha(0.20f));
    graphics.strokePath(steppedPath, juce::PathStrokeType(7.0f));
    graphics.setColour(accentColour());
    graphics.strokePath(steppedPath, juce::PathStrokeType(2.0f));
    graphics.fillPath(pointPath);

    paintLoopOverlay(graphics, graph, processor_.drawLoopInfo(), LoopOverlayScale::CellBoundaries, 0.14f);

    graphics.setFont(juce::FontOptions(13.0f, juce::Font::plain));
    graphics.setColour(juce::Colour::fromRGB(152, 161, 168).withAlpha(0.22f));
    graphics.drawText("HyperFrame", graph.reduced(8.0f, 6.0f), juce::Justification::bottomRight, false);

    juce::ignoreUnused(interpolation);
}

void WaveformDisplay::mouseMove(const juce::MouseEvent& event) {
    if (!isRawMode(processor_.parameters())) {
        setMouseCursor(juce::MouseCursor::CrosshairCursor);
        return;
    }

    const auto loopInfo = processor_.rawStreamLoopInfo();
    if (!loopInfo.hasContent || loopInfo.sampleCount <= 1) {
        setMouseCursor(juce::MouseCursor::NormalCursor);
        return;
    }

    const auto graph = graphBounds();
    const float x = event.position.x;
    constexpr float kHit = 8.0f;

    const auto playStartX = rawSampleToX(
        std::min(loopInfo.playStart, loopInfo.sampleCount - 1),
        loopInfo.sampleCount, graph);
    if (std::abs(x - playStartX) <= kHit) {
        setMouseCursor(juce::MouseCursor::LeftRightResizeCursor);
        return;
    }

    if (loopInfo.end > loopInfo.start) {
        const auto startX = rawSampleToX(loopInfo.start, loopInfo.sampleCount, graph);
        const auto endX   = rawSampleToX(
            std::min(loopInfo.end, loopInfo.sampleCount - 1),
            loopInfo.sampleCount, graph);
        if (std::abs(x - startX) <= kHit || std::abs(x - endX) <= kHit) {
            setMouseCursor(juce::MouseCursor::LeftRightResizeCursor);
            return;
        }
    }

    setMouseCursor(juce::MouseCursor::NormalCursor);
}

void WaveformDisplay::mouseDown(const juce::MouseEvent& event) {
    if (!isRawMode(processor_.parameters())) {
        if (event.mods.isRightButtonDown()) {
            juce::PopupMenu menu;
            menu.addItem(1, "Copy Frame");
            menu.addItem(2, "Paste Frame", processor_.hasFrameClipboard());
            juce::Component::SafePointer<WaveformDisplay> safeThis(this);
            menu.showMenuAsync({}, [safeThis](int result) {
                if (!safeThis) return;
                if (result == 1) {
                    safeThis->processor_.copyCurrentFrame();
                } else if (result == 2) {
                    safeThis->processor_.pasteToCurrentFrame();
                    safeThis->repaint();
                }
            });
            return;
        }
        processor_.beginWaveEdit();
        hasLastEditPoint_ = false;
        editAtPosition(event.position, false);
        return;
    }

    rawDragTarget_ = RawDragTarget::None;
    const auto loopInfo = processor_.rawStreamLoopInfo();
    if (!loopInfo.hasContent || loopInfo.sampleCount <= 1) return;

    const auto graph = graphBounds();
    const float x = event.position.x;
    constexpr float kHit = 8.0f;

    const auto playStartX = rawSampleToX(
        std::min(loopInfo.playStart, loopInfo.sampleCount - 1),
        loopInfo.sampleCount, graph);
    if (std::abs(x - playStartX) <= kHit) {
        rawDragTarget_ = RawDragTarget::PlayStart;
        rawDragLoopInfo_ = loopInfo;
        setMouseCursor(juce::MouseCursor::LeftRightResizeCursor);
        return;
    }

    if (loopInfo.end > loopInfo.start) {
        const auto startX = rawSampleToX(loopInfo.start, loopInfo.sampleCount, graph);
        const auto endX   = rawSampleToX(
            std::min(loopInfo.end, loopInfo.sampleCount - 1),
            loopInfo.sampleCount, graph);
        if (std::abs(x - startX) <= kHit) {
            rawDragTarget_ = RawDragTarget::Start;
            rawDragLoopInfo_ = loopInfo;
            setMouseCursor(juce::MouseCursor::LeftRightResizeCursor);
            return;
        }
        if (std::abs(x - endX) <= kHit) {
            rawDragTarget_ = RawDragTarget::End;
            rawDragLoopInfo_ = loopInfo;
            setMouseCursor(juce::MouseCursor::LeftRightResizeCursor);
            return;
        }
    }
}

void WaveformDisplay::mouseDrag(const juce::MouseEvent& event) {
    if (!isRawMode(processor_.parameters())) {
        editAtPosition(event.position, true);
        return;
    }

    if (rawDragTarget_ == RawDragTarget::None) return;

    const auto& loopInfo = rawDragLoopInfo_;
    const auto graph = graphBounds();
    const auto newSample = rawXToSample(event.position.x, loopInfo.sampleCount, graph);

    auto start     = loopInfo.start;
    auto end       = loopInfo.end;
    auto playStart = loopInfo.playStart;

    switch (rawDragTarget_) {
        case RawDragTarget::Start:     start     = newSample; break;
        case RawDragTarget::End:       end       = newSample; break;
        case RawDragTarget::PlayStart: playStart = newSample; break;
        default: break;
    }

    const bool enableLoop = (rawDragTarget_ == RawDragTarget::Start || rawDragTarget_ == RawDragTarget::End)
        ? true : loopInfo.enabled;
    processor_.setRawStreamLoop(enableLoop, start, end, playStart);
    repaint();
}

void WaveformDisplay::mouseUp(const juce::MouseEvent& event) {
    juce::ignoreUnused(event);
    if (!isRawMode(processor_.parameters())) {
        processor_.commitWaveEdit();
    }
    hasLastEditPoint_ = false;
    rawDragTarget_ = RawDragTarget::None;
    setMouseCursor(juce::MouseCursor::NormalCursor);
}

void WaveformDisplay::timerCallback() {
    repaint();
}

juce::Rectangle<float> WaveformDisplay::graphBounds() const {
    return getLocalBounds().toFloat().reduced(18.0f, 16.0f);
}

void WaveformDisplay::editAtPosition(juce::Point<float> position, bool interpolateFromPrevious) {
    if (isRawMode(processor_.parameters())) {
        return;
    }

    const auto graph = graphBounds();
    const auto activeLength = effectiveWaveLength(processor_.parameters());
    const auto x = std::clamp(position.x, graph.getX(), graph.getRight() - 0.01f);
    const auto y = std::clamp(position.y, graph.getY(), graph.getBottom());
    const auto normalizedX = (x - graph.getX()) / graph.getWidth();
    const auto index = std::clamp(static_cast<int>(std::floor(normalizedX * static_cast<float>(activeLength))),
        0,
        static_cast<int>(activeLength) - 1);
    const auto value = juce::jmap(y, graph.getBottom(), graph.getY(), -1.0f, 1.0f);

    if (interpolateFromPrevious && hasLastEditPoint_ && index != lastEditIndex_) {
        const auto distance = std::abs(index - lastEditIndex_);
        for (int step = 0; step <= distance; ++step) {
            const auto t = static_cast<float>(step) / static_cast<float>(distance);
            const auto editIndex = static_cast<int>(std::round(juce::jmap(t,
                0.0f,
                1.0f,
                static_cast<float>(lastEditIndex_),
                static_cast<float>(index))));
            const auto editValue = juce::jmap(t, 0.0f, 1.0f, lastEditValue_, value);
            processor_.setWaveDisplayPoint(static_cast<std::size_t>(editIndex), activeLength, editValue);
        }
    } else {
        processor_.setWaveDisplayPoint(static_cast<std::size_t>(index), activeLength, value);
    }

    hasLastEditPoint_ = true;
    lastEditIndex_ = index;
    lastEditValue_ = value;
    repaint();
}

HyperFrameAudioProcessorEditor::HyperFrameAudioProcessorEditor(HyperFrameAudioProcessor& processor)
    : AudioProcessorEditor(&processor),
      processor_(processor),
      waveformDisplay_(processor),
      keyboardComponent_(processor.keyboardState(), juce::MidiKeyboardComponent::horizontalKeyboard) {
    setSize(900, 720);
    setWantsKeyboardFocus(true);

    addAndMakeVisible(waveformDisplay_);
    keyboardComponent_.setAvailableRange(24, 96);
    keyboardComponent_.setLowestVisibleKey(36);
    keyboardComponent_.setKeyWidth(18.0f);
    keyboardComponent_.setMidiChannel(1);
    keyboardComponent_.setColour(juce::MidiKeyboardComponent::whiteNoteColourId, juce::Colour::fromRGB(228, 235, 238));
    keyboardComponent_.setColour(juce::MidiKeyboardComponent::blackNoteColourId, juce::Colour::fromRGB(24, 27, 30));
    keyboardComponent_.setColour(juce::MidiKeyboardComponent::keySeparatorLineColourId, juce::Colour::fromRGB(68, 76, 82));
    keyboardComponent_.setColour(juce::MidiKeyboardComponent::mouseOverKeyOverlayColourId, accentColour().withAlpha(0.22f));
    keyboardComponent_.setColour(juce::MidiKeyboardComponent::keyDownOverlayColourId, accentColour().withAlpha(0.52f));
    keyboardComponent_.setColour(juce::MidiKeyboardComponent::textLabelColourId, juce::Colour::fromRGB(64, 71, 76));
    keyboardComponent_.setColour(juce::MidiKeyboardComponent::upDownButtonBackgroundColourId, panelColour());
    keyboardComponent_.setColour(juce::MidiKeyboardComponent::upDownButtonArrowColourId, accentColour());
    addAndMakeVisible(keyboardComponent_);

    configureSlider(waveLengthSlider_, waveLengthLabel_, "Length");
    configureSlider(waveBitsSlider_, waveBitsLabel_, "Bits");
    configureSlider(frameSlider_, frameLabel_, "Frame");
    configureSlider(attackSlider_, attackLabel_, "Attack");
    configureSlider(decaySlider_, decayLabel_, "Decay");
    configureSlider(sustainSlider_, sustainLabel_, "Sustain");
    configureSlider(releaseSlider_, releaseLabel_, "Release");
    configureSlider(lsdjPhaseSlider_, lsdjPhaseLabel_, "Phase Amt");
    configureSlider(slideTimeSlider_, slideTimeLabel_, "Slide");
    configureSlider(motionRateSlider_, motionRateLabel_, "Rate");
    configureSlider(motionStepsSlider_, motionStepsLabel_, "Steps");
    configureSlider(commandFrameSlider_, commandFrameLabel_, "Frame");
    configureSlider(commandPitchSlider_, commandPitchLabel_, "Pitch");
    configureSlider(commandPhaseSlider_, commandPhaseLabel_, "Phase");
    configureSlider(commandLevelSlider_, commandLevelLabel_, "Level");
    commandFrameSlider_.setRange(0.0, static_cast<double>(hyperframe::dsp::kWaveFrameCount), 1.0);
    commandPitchSlider_.setRange(-24.0, 24.0, 1.0);
    commandPhaseSlider_.setRange(-31.0, 31.0, 1.0);
    commandLevelSlider_.setRange(0.0, 1.0, kGbAmplitudeStep);
    commandFrameSlider_.setDoubleClickReturnValue(true, 0.0);
    commandPitchSlider_.setDoubleClickReturnValue(true, 0.0);
    commandPhaseSlider_.setDoubleClickReturnValue(true, 0.0);
    commandLevelSlider_.setDoubleClickReturnValue(true, 1.0);
    commandPitchSlider_.setNumDecimalPlacesToDisplay(0);
    commandPhaseSlider_.setNumDecimalPlacesToDisplay(0);
    commandLevelSlider_.setNumDecimalPlacesToDisplay(2);
    attackSlider_.setNumDecimalPlacesToDisplay(3);
    decaySlider_.setNumDecimalPlacesToDisplay(3);
    sustainSlider_.setNumDecimalPlacesToDisplay(0);
    releaseSlider_.setNumDecimalPlacesToDisplay(3);
    slideTimeSlider_.setNumDecimalPlacesToDisplay(3);
    motionRateSlider_.setNumDecimalPlacesToDisplay(0);
    attackSlider_.textFromValueFunction = envelopeTimeText;
    decaySlider_.textFromValueFunction = envelopeTimeText;
    releaseSlider_.textFromValueFunction = envelopeTimeText;
    attackSlider_.valueFromTextFunction = envelopeTimeValue;
    decaySlider_.valueFromTextFunction = envelopeTimeValue;
    releaseSlider_.valueFromTextFunction = envelopeTimeValue;
    sustainSlider_.textFromValueFunction = percentText;
    sustainSlider_.valueFromTextFunction = percentValue;
    updateMotionRateDisplay();
    lsdjPhaseSlider_.setNumDecimalPlacesToDisplay(0);
    configureSlider(gainSlider_, gainLabel_, "Gain");
    gainSlider_.setSliderStyle(juce::Slider::LinearHorizontal);
    gainSlider_.setTextBoxStyle(juce::Slider::TextBoxRight, false, 48, 20);
    waveShapeBox_.addItem("Sine", 1);
    waveShapeBox_.addItem("Triangle", 2);
    waveShapeBox_.addItem("Saw Up", 3);
    waveShapeBox_.addItem("Saw Down", 4);
    waveShapeBox_.addItem("Pulse 12.5%", 5);
    waveShapeBox_.addItem("Pulse 25%", 6);
    waveShapeBox_.addItem("Pulse 50%", 7);
    waveShapeBox_.addItem("Noise", 8);
    waveShapeBox_.setSelectedId(1, juce::dontSendNotification);
    waveShapeBox_.setColour(juce::ComboBox::backgroundColourId, panelColour());
    waveShapeBox_.setColour(juce::ComboBox::textColourId, juce::Colour::fromRGB(218, 224, 228));
    waveShapeBox_.setColour(juce::ComboBox::outlineColourId, juce::Colour::fromRGB(54, 61, 67));
    waveShapeBox_.onChange = [this] {
        using Shape = hyperframe::dsp::WaveTable::Shape;
        const auto id = waveShapeBox_.getSelectedId();
        if (id == 0) return;
        Shape shape = Shape::Sine;
        switch (id) {
            case 2: shape = Shape::Triangle; break;
            case 3: shape = Shape::Saw; break;
            case 4: shape = Shape::SawDown; break;
            case 5: shape = Shape::Square125; break;
            case 6: shape = Shape::Square25; break;
            case 7: shape = Shape::Square; break;
            case 8: shape = Shape::Noise; break;
            default: break;
        }
        processor_.applyWaveShape(shape);
        waveformDisplay_.repaint();
    };
    addAndMakeVisible(waveShapeBox_);
    configureCommandButton(normalizeButton_, "Normalize");
    configureCommandButton(smoothButton_, "Smooth");
    configureCommandButton(sourceButton_, "Source...");
    configureCommandButton(presetFileButton_, "Bank...");
    configureCommandButton(gbRamButton_, "Wave RAM...");
    configureCommandButton(loopToggleButton_, "Loop");
    loopToggleButton_.setClickingTogglesState(false);
    loopToggleButton_.onClick = [this] {
        const auto info = processor_.rawStreamLoopInfo();
        processor_.setRawStreamLoop(!info.enabled, info.start, info.end, info.playStart);
    };
    addAndMakeVisible(loopToggleButton_);

    for (auto* slider : { &waveLengthSlider_, &waveBitsSlider_, &frameSlider_, &attackSlider_, &decaySlider_,
                          &sustainSlider_, &releaseSlider_,
                          &lsdjPhaseSlider_, &slideTimeSlider_,
                          &motionRateSlider_, &motionStepsSlider_, &commandFrameSlider_,
                          &commandPitchSlider_, &commandPhaseSlider_, &commandLevelSlider_,
                          &gainSlider_ }) {
        addAndMakeVisible(*slider);
    }

    for (auto* label : { &waveLengthLabel_, &waveBitsLabel_, &frameLabel_, &attackLabel_, &decayLabel_,
                         &sustainLabel_, &releaseLabel_,
                         &lsdjPhaseLabel_, &slideTimeLabel_,
                         &motionRateLabel_, &motionStepsLabel_, &commandFrameLabel_,
                         &commandPitchLabel_, &commandPhaseLabel_, &commandLevelLabel_,
                         &gainLabel_ }) {
        addAndMakeVisible(*label);
    }

    configureSectionLabel(waveSectionLabel_, "SOURCE");
    configureSectionLabel(phaseSectionLabel_, "VOICE");
    configureSectionLabel(commandSectionLabel_, "MOTION");
    configureSectionLabel(commandLaneSectionLabel_, "STEP");
    configureSectionLabel(envelopeSectionLabel_, "ENVELOPE");
    configureSectionLabel(keyboardSectionLabel_, "KEYS");
    for (auto* label : { &waveSectionLabel_, &phaseSectionLabel_, &commandSectionLabel_, &commandLaneSectionLabel_,
                         &envelopeSectionLabel_, &keyboardSectionLabel_ }) {
        addAndMakeVisible(*label);
    }

    presetLabel_.setText("Preset", juce::dontSendNotification);
    presetLabel_.setColour(juce::Label::textColourId, juce::Colour::fromRGB(152, 161, 168));
    presetLabel_.setJustificationType(juce::Justification::centredLeft);
    addAndMakeVisible(presetLabel_);

    presetBox_.setColour(juce::ComboBox::backgroundColourId, panelColour());
    presetBox_.setColour(juce::ComboBox::textColourId, juce::Colour::fromRGB(218, 224, 228));
    presetBox_.setColour(juce::ComboBox::outlineColourId, juce::Colour::fromRGB(54, 61, 67));
    presetBox_.onChange = [this] {
        const auto selected = presetBox_.getSelectedId();
        if (selected > 0) {
            processor_.setCurrentProgram(selected - 1);
            syncMotionStepControls();
            updateModeLabels();
            waveformDisplay_.repaint();
        }
    };
    addAndMakeVisible(presetBox_);
    refreshPresetBox();

    commandStepLabel_.setText("Step", juce::dontSendNotification);
    commandStepLabel_.setColour(juce::Label::textColourId, juce::Colour::fromRGB(152, 161, 168));
    commandStepLabel_.setJustificationType(juce::Justification::centredLeft);
    addAndMakeVisible(commandStepLabel_);

    for (int i = 0; i < hyperframe::dsp::kCommandStepCount; ++i) {
        commandStepBox_.addItem(juce::String(i + 1), i + 1);
    }
    commandStepBox_.setSelectedId(processor_.selectedMotionStep() + 1, juce::dontSendNotification);
    commandStepBox_.setColour(juce::ComboBox::backgroundColourId, panelColour());
    commandStepBox_.setColour(juce::ComboBox::textColourId, juce::Colour::fromRGB(218, 224, 228));
    commandStepBox_.setColour(juce::ComboBox::outlineColourId, juce::Colour::fromRGB(54, 61, 67));
    commandStepBox_.onChange = [this] {
        const auto selected = commandStepBox_.getSelectedId();
        if (selected > 0) {
            processor_.selectMotionStep(selected - 1);
            syncMotionStepControls();
        }
    };
    addAndMakeVisible(commandStepBox_);

    engineModeLabel_.setText("Engine", juce::dontSendNotification);
    engineModeLabel_.setColour(juce::Label::textColourId, juce::Colour::fromRGB(152, 161, 168));
    engineModeLabel_.setJustificationType(juce::Justification::centred);
    addAndMakeVisible(engineModeLabel_);

    engineModeBox_.addItem("Raw", 1);
    engineModeBox_.addItem("Draw", 2);
    engineModeBox_.addItem("Wave", 3);
    engineModeBox_.addItem("WonderSwan", 4);
    engineModeBox_.addItem("PC Engine", 5);
    engineModeBox_.addItem("SCC", 6);
    engineModeBox_.setColour(juce::ComboBox::backgroundColourId, panelColour());
    engineModeBox_.setColour(juce::ComboBox::textColourId, juce::Colour::fromRGB(218, 224, 228));
    engineModeBox_.setColour(juce::ComboBox::outlineColourId, juce::Colour::fromRGB(54, 61, 67));
    engineModeBox_.onChange = [this] {
        const auto selected = engineModeBox_.getSelectedId();
        if (selected > 0) {
            setEngineModeFromEditor(selected - 1);
        }
    };
    addAndMakeVisible(engineModeBox_);

    commandClockLabel_.setText("Clock", juce::dontSendNotification);
    commandClockLabel_.setColour(juce::Label::textColourId, juce::Colour::fromRGB(152, 161, 168));
    commandClockLabel_.setJustificationType(juce::Justification::centred);
    addAndMakeVisible(commandClockLabel_);

    commandClockBox_.addItem("Hz", 1);
    commandClockBox_.addItem("BPM", 2);
    commandClockBox_.setColour(juce::ComboBox::backgroundColourId, panelColour());
    commandClockBox_.setColour(juce::ComboBox::textColourId, juce::Colour::fromRGB(218, 224, 228));
    commandClockBox_.setColour(juce::ComboBox::outlineColourId, juce::Colour::fromRGB(54, 61, 67));
    commandClockBox_.onChange = [this] {
        updateMotionRateDisplay();
        updateModeLabels();
    };
    addAndMakeVisible(commandClockBox_);

    lsdjPhaseModeLabel_.setText("Phase Mode", juce::dontSendNotification);
    lsdjPhaseModeLabel_.setColour(juce::Label::textColourId, juce::Colour::fromRGB(152, 161, 168));
    lsdjPhaseModeLabel_.setJustificationType(juce::Justification::centred);
    addAndMakeVisible(lsdjPhaseModeLabel_);

    lsdjPhaseModeBox_.addItem("Normal", 1);
    lsdjPhaseModeBox_.addItem("Resync", 2);
    lsdjPhaseModeBox_.addItem("Resync 2", 3);
    lsdjPhaseModeBox_.setColour(juce::ComboBox::backgroundColourId, panelColour());
    lsdjPhaseModeBox_.setColour(juce::ComboBox::textColourId, juce::Colour::fromRGB(218, 224, 228));
    lsdjPhaseModeBox_.setColour(juce::ComboBox::outlineColourId, juce::Colour::fromRGB(54, 61, 67));
    addAndMakeVisible(lsdjPhaseModeBox_);

    for (auto* button : { &normalizeButton_, &smoothButton_,
                          &sourceButton_, &presetFileButton_, &gbRamButton_ }) {
        addAndMakeVisible(*button);
    }

    normalizeButton_.onClick = [this] {
        processor_.normalizeWaveTable();
        waveformDisplay_.repaint();
    };
    smoothButton_.onClick = [this] {
        processor_.smoothWaveTable();
        waveformDisplay_.repaint();
    };
    auto launchPresetImport = [this] {
        presetChooser_ = std::make_unique<juce::FileChooser>(
            "Import a HyperFrame preset",
            lastFileChooserDirectory_,
            "*.hyperframe");
        juce::Component::SafePointer<HyperFrameAudioProcessorEditor> safeThis(this);
        presetChooser_->launchAsync(juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
            [safeThis](const juce::FileChooser& chooser) {
                auto* editor = safeThis.getComponent();
                if (editor == nullptr) {
                    return;
                }

                const auto file = chooser.getResult();
                if (!file.existsAsFile()) {
                    return;
                }

                editor->lastFileChooserDirectory_ = file.getParentDirectory();
                const auto imported = editor->processor_.importPresetFile(file);
                editor->presetBox_.setSelectedId(editor->processor_.currentProgramSelection() + 1, juce::dontSendNotification);
                editor->commandStepBox_.setSelectedId(editor->processor_.selectedMotionStep() + 1, juce::dontSendNotification);
                editor->syncMotionStepControls();
                editor->updateModeLabels();
                editor->waveformDisplay_.repaint();
                editor->gbRamStatusLabel_.setText(imported ? "Imported preset " + file.getFileName() : "Could not import preset", juce::dontSendNotification);
            });
    };
    auto launchPresetExport = [this] {
        presetChooser_ = std::make_unique<juce::FileChooser>(
            "Export a HyperFrame preset",
            lastFileChooserDirectory_.getChildFile("HyperFrame.hyperframe"),
            "*.hyperframe");
        juce::Component::SafePointer<HyperFrameAudioProcessorEditor> safeThis(this);
        presetChooser_->launchAsync(juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::canSelectFiles | juce::FileBrowserComponent::warnAboutOverwriting,
            [safeThis](const juce::FileChooser& chooser) {
                auto* editor = safeThis.getComponent();
                if (editor == nullptr) {
                    return;
                }

                auto file = chooser.getResult();
                if (file == juce::File {}) {
                    return;
                }

                file = file.withFileExtension(".hyperframe");
                editor->lastFileChooserDirectory_ = file.getParentDirectory();
                const auto exported = editor->processor_.exportPresetFile(file);
                editor->gbRamStatusLabel_.setText(exported ? "Exported preset " + file.getFileName() : "Could not export preset", juce::dontSendNotification);
            });
    };
    auto launchSoundFontImport = [this] {
        presetChooser_ = std::make_unique<juce::FileChooser>(
            "Import an SF2 bank",
            lastFileChooserDirectory_,
            "*.sf2");
        juce::Component::SafePointer<HyperFrameAudioProcessorEditor> safeThis(this);
        presetChooser_->launchAsync(juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
            [safeThis](const juce::FileChooser& chooser) {
                auto* editor = safeThis.getComponent();
                if (editor == nullptr) {
                    return;
                }

                const auto file = chooser.getResult();
                if (!file.existsAsFile()) {
                    return;
                }

                editor->lastFileChooserDirectory_ = file.getParentDirectory();
                const auto imported = editor->processor_.importSoundFontBank(file);
                editor->refreshPresetBox();
                editor->syncMotionStepControls();
                editor->updateModeLabels();
                editor->waveformDisplay_.repaint();
                editor->gbRamStatusLabel_.setText(imported ? "Imported SF2 Draw bank " + file.getFileName() : "No 8-512 cycle/loop presets found in SF2", juce::dontSendNotification);
            });
    };
    auto launchLsdjImport = [this] {
        presetChooser_ = std::make_unique<juce::FileChooser>(
            "Import an LSDJ wave bank",
            lastFileChooserDirectory_,
            "*.lsdsng;*.sav;*.snt");
        juce::Component::SafePointer<HyperFrameAudioProcessorEditor> safeThis(this);
        presetChooser_->launchAsync(juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
            [safeThis](const juce::FileChooser& chooser) {
                auto* editor = safeThis.getComponent();
                if (editor == nullptr) {
                    return;
                }

                const auto file = chooser.getResult();
                if (!file.existsAsFile()) {
                    return;
                }

                editor->lastFileChooserDirectory_ = file.getParentDirectory();
                const auto imported = editor->processor_.importLsdjBank(file);
                editor->refreshPresetBox();
                editor->syncMotionStepControls();
                editor->updateModeLabels();
                editor->waveformDisplay_.repaint();
                editor->gbRamStatusLabel_.setText(imported ? "Imported LSDJ Wave bank " + file.getFileName() : "No LSDJ wave presets found", juce::dontSendNotification);
            });
    };
    presetFileButton_.onClick = [this, launchPresetImport, launchPresetExport, launchSoundFontImport, launchLsdjImport] {
        const auto currentIdx = processor_.getCurrentProgram();
        constexpr int kFactoryPresetCount = 15;
        const auto isImportedPreset = currentIdx >= kFactoryPresetCount;

        juce::PopupMenu menu;
        menu.addItem(1, "Import Preset...");
        menu.addItem(2, "Export Preset...");
        menu.addSeparator();
        menu.addItem(3, "Import SF2 Bank...");
        menu.addItem(4, "Import LSDJ Bank...");
        menu.addSeparator();
        menu.addItem(5, "Rename Preset...", isImportedPreset);
        juce::Component::SafePointer<HyperFrameAudioProcessorEditor> safeThis(this);
        menu.showMenuAsync(juce::PopupMenu::Options().withTargetComponent(&presetFileButton_),
            [safeThis, launchPresetImport, launchPresetExport, launchSoundFontImport, launchLsdjImport, currentIdx](int result) {
                auto* editor = safeThis.getComponent();
                if (editor == nullptr) return;
                if (result == 1) {
                    launchPresetImport();
                } else if (result == 2) {
                    launchPresetExport();
                } else if (result == 3) {
                    launchSoundFontImport();
                } else if (result == 4) {
                    launchLsdjImport();
                } else if (result == 5) {
                    auto* inputWindow = new juce::AlertWindow("Rename Preset", "Enter new name:", juce::AlertWindow::NoIcon, editor);
                    inputWindow->addTextEditor("name", editor->processor_.getProgramName(currentIdx), "Name");
                    inputWindow->addButton("OK", 1, juce::KeyPress(juce::KeyPress::returnKey));
                    inputWindow->addButton("Cancel", 0, juce::KeyPress(juce::KeyPress::escapeKey));
                    inputWindow->enterModalState(true, juce::ModalCallbackFunction::create([safeThis, currentIdx, inputWindow](int r) {
                        auto* ed = safeThis.getComponent();
                        if (ed == nullptr || r != 1) return;
                        const auto newName = inputWindow->getTextEditorContents("name").trim();
                        if (newName.isNotEmpty()) {
                            ed->processor_.changeProgramName(currentIdx, newName);
                            ed->refreshPresetBox();
                        }
                    }), true);
                }
            });
    };
    auto launchAudioSourceImport = [this] {
        sampleChooser_ = std::make_unique<juce::FileChooser>(
            "Import an audio source",
            lastFileChooserDirectory_,
            "*.wav;*.aif;*.aiff;*.flac;*.ogg;*.mp3");
        juce::Component::SafePointer<HyperFrameAudioProcessorEditor> safeThis(this);
        sampleChooser_->launchAsync(juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
            [safeThis](const juce::FileChooser& chooser) {
                auto* editor = safeThis.getComponent();
                if (editor == nullptr) {
                    return;
                }

                const auto file = chooser.getResult();
                if (!file.existsAsFile()) {
                    return;
                }

                editor->lastFileChooserDirectory_ = file.getParentDirectory();
                auto* optionsWindow = new juce::AlertWindow(
                    "Import Audio Source",
                    "Choose how HyperFrame should interpret the selected audio.",
                    juce::AlertWindow::QuestionIcon,
                    editor);
                optionsWindow->addComboBox("target",
                    { "Use as Raw Stream", "Convert to Wavetable", "Convert to Wave Frames" },
                    "Target");
                if (auto* targetBox = optionsWindow->getComboBoxComponent("target")) {
                    targetBox->setSelectedId(1, juce::dontSendNotification);
                }
                optionsWindow->addTextEditor("start_ms", "0", "Start ms");
                optionsWindow->addTextEditor("length_ms", "0", "Length ms (0 = rest)");
                auto* importOptions = new AudioSourceImportOptionsComponent();
                optionsWindow->addCustomComponent(importOptions);
                optionsWindow->addButton("Import", 1, juce::KeyPress(juce::KeyPress::returnKey));
                optionsWindow->addButton("Cancel", 0, juce::KeyPress(juce::KeyPress::escapeKey));

                optionsWindow->enterModalState(true,
                    juce::ModalCallbackFunction::create([safeThis, file, optionsWindow, importOptions](int result) {
                        auto* modalEditor = safeThis.getComponent();
                        if (modalEditor == nullptr || result != 1) {
                            optionsWindow->removeCustomComponent(0);
                            delete importOptions;
                            return;
                        }

                        HyperFrameAudioProcessor::AudioSourceImportOptions options;
                        if (auto* targetBox = optionsWindow->getComboBoxComponent("target")) {
                            options.target = audioSourceImportTargetFromSelection(targetBox->getSelectedId());
                        }
                        options.startMs = std::max(0.0, optionsWindow->getTextEditorContents("start_ms").getDoubleValue());
                        options.lengthMs = std::max(0.0, optionsWindow->getTextEditorContents("length_ms").getDoubleValue());
                        options.normalize = importOptions->normalize();
                        options.zeroCrossingSnap = importOptions->zeroCrossingSnap();
                        optionsWindow->removeCustomComponent(0);
                        delete importOptions;

                        const auto imported = modalEditor->processor_.importAudioSourceFile(file, options);
                        modalEditor->syncMotionStepControls();
                        modalEditor->updateModeLabels();
                        juce::String status = "Could not import audio source";
                        if (imported) {
                            using Target = HyperFrameAudioProcessor::AudioSourceImportTarget;
                            switch (options.target) {
                            case Target::WaveFrames:
                                status = "Imported 16 wave frames";
                                break;
                            case Target::RawStream:
                                status = modalEditor->processor_.rawStreamLoopInfo().enabled
                                    ? "Loaded looped raw stream"
                                    : "Loaded raw stream";
                                break;
                            case Target::Wavetable:
                            default:
                                status = "Imported wavetable cycle";
                                break;
                            }
                        }
                        modalEditor->gbRamStatusLabel_.setText(status, juce::dontSendNotification);
                        modalEditor->waveformDisplay_.repaint();
                    }),
                    true);
            });
    };
    auto launchBytebeatImport = [this] {
        auto* optionsWindow = new juce::AlertWindow(
            "Import Bytebeat",
            "Render one tick loop as a raw stream, or capture frames into Draw mode.",
            juce::AlertWindow::QuestionIcon,
            this);
        optionsWindow->addTextEditor("expression", "t*((t>>12|t>>8)&63&t>>4)", "Expression");
        if (auto* expressionEditor = optionsWindow->getTextEditor("expression")) {
            expressionEditor->setMultiLine(true);
            expressionEditor->setReturnKeyStartsNewLine(true);
            expressionEditor->setSize(420, 72);
        }
        optionsWindow->addComboBox("target",
            { "Use as Raw Stream", "Capture 16 Frames" },
            "Target");
        if (auto* targetBox = optionsWindow->getComboBoxComponent("target")) {
            targetBox->setSelectedId(1, juce::dontSendNotification);
        }
        optionsWindow->addTextEditor("sample_rate", "8000", "Clock Hz");
        optionsWindow->addTextEditor("start_tick", "0", "Start tick");
        optionsWindow->addTextEditor("loop_ticks", "65536", "Loop ticks");
        optionsWindow->addButton("Import", 1, juce::KeyPress(juce::KeyPress::returnKey));
        optionsWindow->addButton("Cancel", 0, juce::KeyPress(juce::KeyPress::escapeKey));

        juce::Component::SafePointer<HyperFrameAudioProcessorEditor> safeThis(this);
        optionsWindow->enterModalState(true,
            juce::ModalCallbackFunction::create([safeThis, optionsWindow](int result) {
                auto* modalEditor = safeThis.getComponent();
                if (modalEditor == nullptr || result != 1) {
                    return;
                }

                HyperFrameAudioProcessor::BytebeatImportOptions options;
                options.expression = optionsWindow->getTextEditorContents("expression");
                if (auto* targetBox = optionsWindow->getComboBoxComponent("target")) {
                    options.target = bytebeatImportTargetFromSelection(targetBox->getSelectedId());
                }
                options.sampleRate = std::clamp(optionsWindow->getTextEditorContents("sample_rate").getDoubleValue(), 1.0, 2000000.0);
                options.startTick = static_cast<std::uint32_t>(std::clamp(optionsWindow->getTextEditorContents("start_tick").getLargeIntValue(), juce::int64 { 0 }, juce::int64 { 2147483647 }));
                options.loopTicks = static_cast<std::uint32_t>(std::clamp(optionsWindow->getTextEditorContents("loop_ticks").getLargeIntValue(), juce::int64 { 8 }, juce::int64 { 1048576 }));

                const auto imported = modalEditor->processor_.importBytebeat(options);
                modalEditor->syncMotionStepControls();
                modalEditor->updateModeLabels();
                modalEditor->waveformDisplay_.repaint();
                modalEditor->gbRamStatusLabel_.setText(
                    imported
                        ? (options.target == HyperFrameAudioProcessor::BytebeatImportTarget::WaveFrames
                              ? "Captured bytebeat Draw frames"
                              : "Loaded C-aligned bytebeat raw stream")
                        : "Could not import bytebeat expression",
                    juce::dontSendNotification);
            }),
            true);
    };
    auto launchRawLoopEdit = [this] {
        const auto loopInfo = processor_.rawStreamLoopInfo();
        if (!loopInfo.hasContent) {
            gbRamStatusLabel_.setText("Raw Loop needs a stream", juce::dontSendNotification);
            return;
        }

        auto* optionsWindow = new juce::AlertWindow(
            "Raw Loop",
            "Set raw stream playback start and loop points in source ticks.",
            juce::AlertWindow::QuestionIcon,
            this);
        optionsWindow->addTextEditor("play_start", juce::String(static_cast<int>(loopInfo.playStart)), "Play start");
        const auto defaultEnd = loopInfo.end > loopInfo.start + 1 ? loopInfo.end : loopInfo.sampleCount;
        optionsWindow->addTextEditor("start", juce::String(static_cast<int>(loopInfo.start)), "Start");
        optionsWindow->addTextEditor("end", juce::String(static_cast<int>(defaultEnd)), "End");
        optionsWindow->addButton("Apply", 1, juce::KeyPress(juce::KeyPress::returnKey));
        optionsWindow->addButton("Cancel", 0, juce::KeyPress(juce::KeyPress::escapeKey));

        juce::Component::SafePointer<HyperFrameAudioProcessorEditor> safeThis(this);
        optionsWindow->enterModalState(true,
            juce::ModalCallbackFunction::create([safeThis, optionsWindow](int result) {
                auto* modalEditor = safeThis.getComponent();
                if (modalEditor == nullptr || result != 1) {
                    return;
                }

                const auto loopInfo = modalEditor->processor_.rawStreamLoopInfo();
                auto start = static_cast<std::size_t>(std::clamp(optionsWindow->getTextEditorContents("start").getLargeIntValue(), juce::int64 { 0 }, static_cast<juce::int64>(std::max<std::size_t>(1, loopInfo.sampleCount) - 1)));
                auto end = static_cast<std::size_t>(std::clamp(optionsWindow->getTextEditorContents("end").getLargeIntValue(), juce::int64 { 0 }, static_cast<juce::int64>(loopInfo.sampleCount)));
                auto playStart = static_cast<std::size_t>(std::clamp(optionsWindow->getTextEditorContents("play_start").getLargeIntValue(), juce::int64 { 0 }, static_cast<juce::int64>(std::max<std::size_t>(1, loopInfo.sampleCount) - 1)));
                if (end <= start + 1) {
                    end = std::min(loopInfo.sampleCount, start + 2);
                }

                modalEditor->processor_.setRawStreamLoop(loopInfo.enabled, start, end, playStart);
                modalEditor->waveformDisplay_.repaint();
                modalEditor->gbRamStatusLabel_.setText(
                    "Raw start " + juce::String(static_cast<int>(playStart)) + ", loop " + juce::String(static_cast<int>(start)) + "-" + juce::String(static_cast<int>(end)),
                    juce::dontSendNotification);
            }),
            true);
    };
    auto launchStepAdvancedEdit = [this] {
        const auto stepIndex = processor_.selectedMotionStep();
        auto* optionsWindow = new juce::AlertWindow(
            "Step Advanced",
            "Edit Wave step bend and vibrato values for the selected step.",
            juce::AlertWindow::QuestionIcon,
            this);
        auto& parameters = processor_.parameters();
        optionsWindow->addTextEditor("bend", juce::String(parameterValue(parameters, motionParamId(stepIndex, "bend").toRawUTF8()), 0), "Bend semitones/sec");
        optionsWindow->addTextEditor("vib_rate", juce::String(parameterValue(parameters, motionParamId(stepIndex, "vib_rate").toRawUTF8()), 0), "Vibrato Hz");
        optionsWindow->addTextEditor("loop_start", juce::String(static_cast<int>(parameterValue(parameters, kParamMotionLoopStart)) + 1), "Loop start step");
        optionsWindow->addButton("Apply", 1, juce::KeyPress(juce::KeyPress::returnKey));
        optionsWindow->addButton("Cancel", 0, juce::KeyPress(juce::KeyPress::escapeKey));

        juce::Component::SafePointer<HyperFrameAudioProcessorEditor> safeThis(this);
        optionsWindow->enterModalState(true,
            juce::ModalCallbackFunction::create([safeThis, optionsWindow, stepIndex](int result) {
                auto* modalEditor = safeThis.getComponent();
                if (modalEditor == nullptr || result != 1) {
                    return;
                }

                auto& modalParameters = modalEditor->processor_.parameters();
                const auto bend = static_cast<float>(std::clamp(optionsWindow->getTextEditorContents("bend").getDoubleValue(), -96.0, 96.0));
                const auto vibratoRate = static_cast<float>(std::clamp(optionsWindow->getTextEditorContents("vib_rate").getDoubleValue(), 0.0, 64.0));
                const auto loopStart = std::clamp(static_cast<int>(optionsWindow->getTextEditorContents("loop_start").getLargeIntValue()), 1, hyperframe::dsp::kCommandStepCount);
                setParameterValueFromEditor(modalParameters, motionParamId(stepIndex, "bend"), bend);
                setParameterValueFromEditor(modalParameters, motionParamId(stepIndex, "vib_rate"), vibratoRate);
                setParameterValueFromEditor(modalParameters, kParamMotionLoopStart, static_cast<float>(loopStart - 1));
                modalEditor->gbRamStatusLabel_.setText("Updated step " + juce::String(stepIndex + 1) + " bend/vibrato", juce::dontSendNotification);
            }),
            true);
    };
    sourceButton_.onClick = [this, launchAudioSourceImport, launchBytebeatImport, launchRawLoopEdit, launchStepAdvancedEdit] {
        juce::PopupMenu menu;
        menu.addItem(1, "Import Audio...");
        menu.addItem(2, "Import Bytebeat...");
        menu.addSeparator();
        menu.addItem(6, "Step Advanced...");
        menu.addItem(4, "Raw Loop...", processor_.hasRawStream());
        menu.addItem(3, "Convert Raw to Draw", processor_.hasRawStream());
        juce::Component::SafePointer<HyperFrameAudioProcessorEditor> safeThis(this);
        menu.showMenuAsync(juce::PopupMenu::Options().withTargetComponent(&sourceButton_),
            [safeThis, launchAudioSourceImport, launchBytebeatImport, launchRawLoopEdit, launchStepAdvancedEdit](int result) {
                auto* editor = safeThis.getComponent();
                if (editor == nullptr) {
                    return;
                }

                if (result == 1) {
                    launchAudioSourceImport();
                    return;
                }

                if (result == 2) {
                    launchBytebeatImport();
                    return;
                }

                if (result == 4) {
                    launchRawLoopEdit();
                    return;
                }

                if (result == 6) {
                    launchStepAdvancedEdit();
                    return;
                }

                if (result != 3) {
                    return;
                }

                const auto converted = editor->processor_.convertRawStreamToDrawExact();
                editor->syncMotionStepControls();
                editor->updateModeLabels();
                editor->waveformDisplay_.repaint();
                editor->gbRamStatusLabel_.setText(converted ? "Converted exact raw stream to Draw" : "Exact Draw needs raw length 8-512", juce::dontSendNotification);
            });
    };
    gbRamButton_.onClick = [this] {
        juce::PopupMenu menu;
        menu.addItem(1, "Copy GB Hex");
        menu.addItem(2, "Switch to Wave Mode");
        juce::Component::SafePointer<HyperFrameAudioProcessorEditor> safeThis(this);
        menu.showMenuAsync(juce::PopupMenu::Options().withTargetComponent(&gbRamButton_),
            [safeThis](int result) {
                auto* editor = safeThis.getComponent();
                if (editor == nullptr) {
                    return;
                }

                if (result == 1) {
                    const auto hex = editor->processor_.exportGameBoyWaveRamHex();
                    juce::SystemClipboard::copyTextToClipboard(hex);
                    editor->gbRamStatusLabel_.setText("Copied " + hex, juce::dontSendNotification);
                } else if (result == 2) {
                    editor->setEngineModeFromEditor(2);
                    editor->gbRamStatusLabel_.setText("Wave mode " + editor->processor_.exportGameBoyWaveRamHex(), juce::dontSendNotification);
                }
            });
    };

    gbRamStatusLabel_.setText("", juce::dontSendNotification);
    gbRamStatusLabel_.setColour(juce::Label::textColourId, juce::Colour::fromRGB(122, 132, 140));
    gbRamStatusLabel_.setJustificationType(juce::Justification::centredLeft);
    addAndMakeVisible(gbRamStatusLabel_);

    motionStepPlayLabel_.setText("", juce::dontSendNotification);
    motionStepPlayLabel_.setColour(juce::Label::textColourId, accentColour());
    motionStepPlayLabel_.setJustificationType(juce::Justification::centredLeft);
    addAndMakeVisible(motionStepPlayLabel_);

    interpolationToggle_.setButtonText("Interpolation");
    interpolationToggle_.setColour(juce::ToggleButton::tickColourId, accentColour());
    interpolationToggle_.setColour(juce::ToggleButton::textColourId, juce::Colour::fromRGB(218, 224, 228));
    addAndMakeVisible(interpolationToggle_);

    rawPlayFullToggle_.setButtonText("Full Raw");
    rawPlayFullToggle_.setColour(juce::ToggleButton::tickColourId, accentColour());
    rawPlayFullToggle_.setColour(juce::ToggleButton::textColourId, juce::Colour::fromRGB(218, 224, 228));
    addAndMakeVisible(rawPlayFullToggle_);

    commandTableToggle_.setButtonText("Table");
    commandTableToggle_.setColour(juce::ToggleButton::tickColourId, accentColour());
    commandTableToggle_.setColour(juce::ToggleButton::textColourId, juce::Colour::fromRGB(218, 224, 228));
    addAndMakeVisible(commandTableToggle_);

    motionLoopToggle_.setButtonText("Loop");
    motionLoopToggle_.setColour(juce::ToggleButton::tickColourId, accentColour());
    motionLoopToggle_.setColour(juce::ToggleButton::textColourId, juce::Colour::fromRGB(218, 224, 228));
    addAndMakeVisible(motionLoopToggle_);

    monoToggle_.setButtonText("Mono");
    monoToggle_.setColour(juce::ToggleButton::tickColourId, accentColour());
    monoToggle_.setColour(juce::ToggleButton::textColourId, juce::Colour::fromRGB(218, 224, 228));
    addAndMakeVisible(monoToggle_);

    commandFrameSlider_.onValueChange = [this] { setMotionStepParameter("frame", static_cast<float>(commandFrameSlider_.getValue())); };
    commandPitchSlider_.onValueChange = [this] { setMotionStepParameter("pitch", static_cast<float>(commandPitchSlider_.getValue())); };
    commandPhaseSlider_.onValueChange = [this] { setMotionStepParameter("phase", static_cast<float>(commandPhaseSlider_.getValue())); };
    commandLevelSlider_.onValueChange = [this] { setMotionStepParameter("level", static_cast<float>(commandLevelSlider_.getValue())); };

    auto& parameters = processor_.parameters();
    waveLengthAttachment_ = std::make_unique<SliderAttachment>(parameters, kParamWaveLength, waveLengthSlider_);
    waveBitsAttachment_ = std::make_unique<SliderAttachment>(parameters, kParamWaveBits, waveBitsSlider_);
    frameAttachment_ = std::make_unique<SliderAttachment>(parameters, kParamSelectedFrame, frameSlider_);
    attackAttachment_ = std::make_unique<SliderAttachment>(parameters, kParamAttack, attackSlider_);
    decayAttachment_ = std::make_unique<SliderAttachment>(parameters, kParamDecay, decaySlider_);
    sustainAttachment_ = std::make_unique<SliderAttachment>(parameters, kParamSustain, sustainSlider_);
    releaseAttachment_ = std::make_unique<SliderAttachment>(parameters, kParamRelease, releaseSlider_);
    lsdjPhaseAttachment_ = std::make_unique<SliderAttachment>(parameters, kParamLsdjPhase, lsdjPhaseSlider_);
    slideTimeAttachment_ = std::make_unique<SliderAttachment>(parameters, kParamSlideTime, slideTimeSlider_);
    motionRateAttachment_ = std::make_unique<SliderAttachment>(parameters, kParamMotionRate, motionRateSlider_);
    updateMotionRateDisplay();
    motionStepsAttachment_ = std::make_unique<SliderAttachment>(parameters, kParamMotionSteps, motionStepsSlider_);
    gainAttachment_ = std::make_unique<SliderAttachment>(parameters, kParamGain, gainSlider_);
    interpolationAttachment_ = std::make_unique<ButtonAttachment>(parameters, kParamInterpolation, interpolationToggle_);
    rawPlayFullAttachment_ = std::make_unique<ButtonAttachment>(parameters, kParamRawPlayFull, rawPlayFullToggle_);
    commandTableAttachment_ = std::make_unique<ButtonAttachment>(parameters, kParamMotionTable, commandTableToggle_);
    motionLoopAttachment_ = std::make_unique<ButtonAttachment>(parameters, kParamMotionLoop, motionLoopToggle_);
    monoAttachment_ = std::make_unique<ButtonAttachment>(parameters, kParamMonoMode, monoToggle_);
    engineModeBox_.setSelectedId(static_cast<int>(parameterValue(parameters, kParamEngineMode)) + 1, juce::dontSendNotification);
    commandClockAttachment_ = std::make_unique<ComboBoxAttachment>(parameters, kParamMotionClockMode, commandClockBox_);
    lsdjPhaseModeAttachment_ = std::make_unique<ComboBoxAttachment>(parameters, kParamLsdjPhaseMode, lsdjPhaseModeBox_);
    syncMotionStepControls();
    updateModeLabels();
    startTimerHz(8);
}

void HyperFrameAudioProcessorEditor::paint(juce::Graphics& graphics) {
    graphics.fillAll(juce::Colour::fromRGB(13, 15, 17));

    for (const auto& bounds : sectionBounds_) {
        if (bounds.isEmpty()) {
            continue;
        }

        const auto section = bounds.toFloat();
        graphics.setColour(panelColour().withAlpha(0.72f));
        graphics.fillRoundedRectangle(section, 6.0f);
        graphics.setColour(juce::Colour::fromRGB(44, 50, 55));
        graphics.drawRoundedRectangle(section, 6.0f, 1.0f);
    }
}

void HyperFrameAudioProcessorEditor::resized() {
    for (auto& bounds : sectionBounds_) {
        bounds = {};
    }

    auto area = getLocalBounds().reduced(14);
    waveformDisplay_.setBounds(area.removeFromTop(180).reduced(24, 0));
    area.removeFromTop(8);

    auto toolRow = area.removeFromTop(34);
    presetLabel_.setBounds(toolRow.removeFromLeft(48));
    presetBox_.setBounds(toolRow.removeFromLeft(184).reduced(3, 3));
    presetFileButton_.setBounds(toolRow.removeFromLeft(88).reduced(3, 3));
    commandStepLabel_.setBounds(toolRow.removeFromLeft(42));
    commandStepBox_.setBounds(toolRow.removeFromLeft(62).reduced(3, 3));
    motionStepPlayLabel_.setBounds(toolRow.removeFromLeft(36).reduced(2, 4));

    gainLabel_.setBounds(toolRow.removeFromRight(32).reduced(0, 8));
    gainSlider_.setBounds(toolRow.removeFromRight(100).reduced(3, 4));
    waveShapeBox_.setBounds(toolRow.reduced(3, 3));

    auto actionRow = area.removeFromTop(30);
    normalizeButton_.setBounds(actionRow.removeFromLeft(96).reduced(3, 3));
    smoothButton_.setBounds(actionRow.removeFromLeft(82).reduced(3, 3));
    sourceButton_.setBounds(actionRow.removeFromLeft(78).reduced(3, 3));
    gbRamButton_.setBounds(actionRow.removeFromLeft(96).reduced(3, 3));
    loopToggleButton_.setBounds(actionRow.removeFromLeft(56).reduced(3, 3));
    gbRamStatusLabel_.setBounds(actionRow.reduced(6, 3));
    area.removeFromTop(8);

    auto controlArea = area;
    std::size_t sectionIndex = 0;
    auto prepareSection = [this, &sectionIndex](juce::Rectangle<int>& row, juce::Label& label) {
        if (sectionIndex < sectionBounds_.size()) {
            sectionBounds_[sectionIndex++] = row.reduced(0, 2);
        }

        label.setBounds(row.removeFromLeft(96).reduced(12, 8));
        row.removeFromLeft(8);
    };

    auto topRow = controlArea.removeFromTop(72);
    auto middleRow = controlArea.removeFromTop(72);
    auto commandRow = controlArea.removeFromTop(72);
    auto commandLaneRow = controlArea.removeFromTop(72);
    auto bottomRow = controlArea.removeFromTop(72);
    controlArea.removeFromTop(8);
    auto keyboardRow = controlArea.removeFromTop(58);

    prepareSection(topRow, waveSectionLabel_);
    prepareSection(middleRow, phaseSectionLabel_);
    prepareSection(commandRow, commandSectionLabel_);
    prepareSection(commandLaneRow, commandLaneSectionLabel_);
    prepareSection(bottomRow, envelopeSectionLabel_);
    prepareSection(keyboardRow, keyboardSectionLabel_);

    const auto sourceWidth = topRow.getWidth() / 5;
    engineModeLabel_.setBounds(topRow.removeFromLeft(sourceWidth).removeFromTop(18));
    engineModeBox_.setBounds(engineModeLabel_.getBounds().withY(engineModeLabel_.getBottom()).withHeight(26).reduced(8, 0));
    waveLengthLabel_.setBounds(topRow.removeFromLeft(sourceWidth).removeFromTop(18));
    waveLengthSlider_.setBounds(waveLengthLabel_.getBounds().withY(waveLengthLabel_.getBottom()).withHeight(54));
    waveBitsLabel_.setBounds(topRow.removeFromLeft(sourceWidth).removeFromTop(18));
    waveBitsSlider_.setBounds(waveBitsLabel_.getBounds().withY(waveBitsLabel_.getBottom()).withHeight(54));
    frameLabel_.setBounds(topRow.removeFromLeft(sourceWidth).removeFromTop(18));
    frameSlider_.setBounds(frameLabel_.getBounds().withY(frameLabel_.getBottom()).withHeight(54));
    auto sourceToggleArea = topRow.reduced(10, 10);
    interpolationToggle_.setBounds(sourceToggleArea.removeFromTop(24));
    rawPlayFullToggle_.setBounds(sourceToggleArea.removeFromTop(24));

    const auto voiceWidth = middleRow.getWidth() / 4;
    lsdjPhaseModeLabel_.setBounds(middleRow.removeFromLeft(voiceWidth).removeFromTop(18));
    lsdjPhaseModeBox_.setBounds(lsdjPhaseModeLabel_.getBounds().withY(lsdjPhaseModeLabel_.getBottom()).withHeight(26).reduced(8, 0));
    lsdjPhaseLabel_.setBounds(middleRow.removeFromLeft(voiceWidth).removeFromTop(18));
    lsdjPhaseSlider_.setBounds(lsdjPhaseLabel_.getBounds().withY(lsdjPhaseLabel_.getBottom()).withHeight(54));
    slideTimeLabel_.setBounds(middleRow.removeFromLeft(voiceWidth).removeFromTop(18));
    slideTimeSlider_.setBounds(slideTimeLabel_.getBounds().withY(slideTimeLabel_.getBottom()).withHeight(54));
    monoToggle_.setBounds(middleRow.reduced(10, 18));

    const auto commandWidth = commandRow.getWidth() / 5;
    commandClockLabel_.setBounds(commandRow.removeFromLeft(commandWidth).removeFromTop(18));
    commandClockBox_.setBounds(commandClockLabel_.getBounds().withY(commandClockLabel_.getBottom()).withHeight(26).reduced(8, 0));
    motionRateLabel_.setBounds(commandRow.removeFromLeft(commandWidth).removeFromTop(18));
    motionRateSlider_.setBounds(motionRateLabel_.getBounds().withY(motionRateLabel_.getBottom()).withHeight(54));
    motionStepsLabel_.setBounds(commandRow.removeFromLeft(commandWidth).removeFromTop(18));
    motionStepsSlider_.setBounds(motionStepsLabel_.getBounds().withY(motionStepsLabel_.getBottom()).withHeight(54));
    commandTableToggle_.setBounds(commandRow.removeFromLeft(commandWidth).reduced(10, 18));
    motionLoopToggle_.setBounds(commandRow.reduced(10, 18));

    const auto commandLaneWidth = commandLaneRow.getWidth() / 4;
    commandFrameLabel_.setBounds(commandLaneRow.removeFromLeft(commandLaneWidth).removeFromTop(18));
    commandFrameSlider_.setBounds(commandFrameLabel_.getBounds().withY(commandFrameLabel_.getBottom()).withHeight(54));
    commandPitchLabel_.setBounds(commandLaneRow.removeFromLeft(commandLaneWidth).removeFromTop(18));
    commandPitchSlider_.setBounds(commandPitchLabel_.getBounds().withY(commandPitchLabel_.getBottom()).withHeight(54));
    commandPhaseLabel_.setBounds(commandLaneRow.removeFromLeft(commandLaneWidth).removeFromTop(18));
    commandPhaseSlider_.setBounds(commandPhaseLabel_.getBounds().withY(commandPhaseLabel_.getBottom()).withHeight(54));
    commandLevelLabel_.setBounds(commandLaneRow.removeFromLeft(commandLaneWidth).removeFromTop(18));
    commandLevelSlider_.setBounds(commandLevelLabel_.getBounds().withY(commandLevelLabel_.getBottom()).withHeight(54));

    const auto envelopeWidth = bottomRow.getWidth() / 4;
    attackLabel_.setBounds(bottomRow.removeFromLeft(envelopeWidth).removeFromTop(18));
    attackSlider_.setBounds(attackLabel_.getBounds().withY(attackLabel_.getBottom()).withHeight(54));
    decayLabel_.setBounds(bottomRow.removeFromLeft(envelopeWidth).removeFromTop(18));
    decaySlider_.setBounds(decayLabel_.getBounds().withY(decayLabel_.getBottom()).withHeight(54));
    sustainLabel_.setBounds(bottomRow.removeFromLeft(envelopeWidth).removeFromTop(18));
    sustainSlider_.setBounds(sustainLabel_.getBounds().withY(sustainLabel_.getBottom()).withHeight(54));
    releaseLabel_.setBounds(bottomRow.removeFromLeft(envelopeWidth).removeFromTop(18));
    releaseSlider_.setBounds(releaseLabel_.getBounds().withY(releaseLabel_.getBottom()).withHeight(54));

    keyboardComponent_.setBounds(keyboardRow.reduced(6, 6));
}

void HyperFrameAudioProcessorEditor::configureSlider(juce::Slider& slider, juce::Label& label, const juce::String& text) {
    label.setText(text, juce::dontSendNotification);
    label.setColour(juce::Label::textColourId, juce::Colour::fromRGB(152, 161, 168));
    label.setJustificationType(juce::Justification::centred);

    slider.setSliderStyle(juce::Slider::RotaryHorizontalVerticalDrag);
    slider.setTextBoxStyle(juce::Slider::TextBoxBelow, false, 74, 20);
    slider.setColour(juce::Slider::rotarySliderFillColourId, accentColour());
    slider.setColour(juce::Slider::rotarySliderOutlineColourId, juce::Colour::fromRGB(45, 51, 56));
    slider.setColour(juce::Slider::thumbColourId, juce::Colour::fromRGB(235, 241, 245));
    slider.setColour(juce::Slider::textBoxTextColourId, juce::Colour::fromRGB(218, 224, 228));
    slider.setColour(juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
}

void HyperFrameAudioProcessorEditor::configureSectionLabel(juce::Label& label, const juce::String& text) {
    label.setText(text, juce::dontSendNotification);
    label.setColour(juce::Label::textColourId, accentColour().withAlpha(0.82f));
    label.setJustificationType(juce::Justification::centredLeft);
    label.setFont(juce::FontOptions(12.0f, juce::Font::bold));
}

void HyperFrameAudioProcessorEditor::configureCommandButton(juce::TextButton& button, const juce::String& text) {
    button.setButtonText(text);
    button.setColour(juce::TextButton::buttonColourId, juce::Colour::fromRGB(30, 34, 38));
    button.setColour(juce::TextButton::buttonOnColourId, accentColour().withAlpha(0.35f));
    button.setColour(juce::TextButton::textColourOffId, juce::Colour::fromRGB(218, 224, 228));
    button.setColour(juce::TextButton::textColourOnId, juce::Colours::white);
}

void HyperFrameAudioProcessorEditor::timerCallback() {
    updateModeLabels();

    const auto motionEnabled = parameterValue(processor_.parameters(), kParamMotionTable) > 0.5f;
    if (motionEnabled) {
        const int step = processor_.currentMotionStep();
        motionStepPlayLabel_.setText(">" + juce::String(step + 1), juce::dontSendNotification);
    } else {
        motionStepPlayLabel_.setText("", juce::dontSendNotification);
    }
}

bool HyperFrameAudioProcessorEditor::keyPressed(const juce::KeyPress& key) {
    const auto ctrlZ = juce::KeyPress('z', juce::ModifierKeys::commandModifier, 0);
    const auto ctrlY = juce::KeyPress('y', juce::ModifierKeys::commandModifier, 0);
    const auto ctrlShiftZ = juce::KeyPress('z', juce::ModifierKeys::commandModifier | juce::ModifierKeys::shiftModifier, 0);

    if (key == ctrlZ) {
        if (processor_.canUndoWaveEdit()) {
            processor_.undoWaveEdit();
            waveformDisplay_.repaint();
        }
        return true;
    }
    if (key == ctrlY || key == ctrlShiftZ) {
        if (processor_.canRedoWaveEdit()) {
            processor_.redoWaveEdit();
            waveformDisplay_.repaint();
        }
        return true;
    }
    return false;
}

void HyperFrameAudioProcessorEditor::syncMotionStepControls() {
    const auto step = processor_.selectedMotionStep();
    auto& parameters = processor_.parameters();
    updatingMotionStepControls_ = true;

    commandFrameSlider_.setValue(parameterValue(parameters, motionParamId(step, "frame").toRawUTF8()), juce::dontSendNotification);
    commandPitchSlider_.setValue(parameterValue(parameters, motionParamId(step, "pitch").toRawUTF8()), juce::dontSendNotification);
    commandPhaseSlider_.setValue(parameterValue(parameters, motionParamId(step, "phase").toRawUTF8()), juce::dontSendNotification);
    commandLevelSlider_.setValue(parameterValue(parameters, motionParamId(step, "level").toRawUTF8()), juce::dontSendNotification);

    updatingMotionStepControls_ = false;
}

void HyperFrameAudioProcessorEditor::refreshPresetBox() {
    presetBox_.clear(juce::dontSendNotification);
    for (int i = 0; i < processor_.getNumPrograms(); ++i) {
        presetBox_.addItem(processor_.getProgramName(i), i + 1);
    }
    presetBox_.setSelectedId(processor_.currentProgramSelection() + 1, juce::dontSendNotification);
}

void HyperFrameAudioProcessorEditor::updateMotionRateDisplay() {
    motionRateSlider_.textFromValueFunction = [this](double value) {
        return motionRateText(value, isBpmMotionClock(processor_.parameters()));
    };
    motionRateSlider_.valueFromTextFunction = motionRateValue;
    motionRateSlider_.updateText();
    motionRateSlider_.repaint();

    slideTimeSlider_.textFromValueFunction = [this](double value) {
        return slideTimeText(value, isBpmMotionClock(processor_.parameters()), processor_.hostBpm());
    };
    slideTimeSlider_.valueFromTextFunction = [this](const juce::String& text) {
        return slideTimeValue(text, isBpmMotionClock(processor_.parameters()), processor_.hostBpm());
    };
    slideTimeSlider_.updateText();
    slideTimeSlider_.repaint();
}

void HyperFrameAudioProcessorEditor::updateModeLabels() {
    const auto mode = static_cast<int>(parameterValue(processor_.parameters(), kParamEngineMode));
    const auto isRaw = mode == 0;
    const auto isWave = mode == 2;
    const auto isHardwareWave = isHardwareWaveMode(processor_.parameters());
    const auto isBpmClock = isBpmMotionClock(processor_.parameters());
    const auto selectedModeId = mode + 1;
    if (engineModeBox_.getSelectedId() != selectedModeId) {
        engineModeBox_.setSelectedId(selectedModeId, juce::dontSendNotification);
    }

    commandFrameLabel_.setText("Frame", juce::dontSendNotification);
    commandPitchLabel_.setText("Pitch", juce::dontSendNotification);
    commandPhaseLabel_.setText("Phase", juce::dontSendNotification);
    lsdjPhaseLabel_.setText("Phase Amt", juce::dontSendNotification);
    lsdjPhaseModeLabel_.setText("Phase Mode", juce::dontSendNotification);
    slideTimeLabel_.setText("Slide", juce::dontSendNotification);
    motionRateLabel_.setText("Rate", juce::dontSendNotification);
    motionStepsLabel_.setText("Steps", juce::dontSendNotification);
    commandTableToggle_.setButtonText("Table");
    motionLoopToggle_.setButtonText("Loop");
    commandClockBox_.setSelectedId(isBpmClock ? 2 : 1, juce::dontSendNotification);
    updateMotionRateDisplay();

    attackSlider_.setEnabled(!isHardwareWave);
    decaySlider_.setEnabled(!isHardwareWave);
    sustainSlider_.setEnabled(!isHardwareWave);
    releaseSlider_.setEnabled(!isHardwareWave);
    commandFrameSlider_.setEnabled(!isRaw);
    commandPitchSlider_.setEnabled(true);
    commandPhaseSlider_.setEnabled(!isRaw);
    commandPhaseSlider_.setRange(-31.0, 31.0, 1.0);
    commandLevelSlider_.setRange(0.0, 1.0, isWave ? kLsdjWaveVolumeStep : kGbAmplitudeStep);
    if (isWave) {
        commandFrameLabel_.setText("Frame", juce::dontSendNotification);
        commandPitchLabel_.setText("Bend", juce::dontSendNotification);
        commandPhaseLabel_.setText("Vib Depth", juce::dontSendNotification);
        commandLevelLabel_.setText("Volume", juce::dontSendNotification);
        motionRateLabel_.setText("Speed", juce::dontSendNotification);
        motionStepsLabel_.setText("Length", juce::dontSendNotification);
        commandTableToggle_.setButtonText("Table");
    } else {
        commandLevelLabel_.setText("Level", juce::dontSendNotification);
    }
    waveLengthSlider_.setEnabled(!isHardwareWave && !isRaw);
    waveBitsSlider_.setEnabled(!isHardwareWave);
    interpolationToggle_.setEnabled(!isHardwareWave && !isRaw);
    rawPlayFullToggle_.setEnabled(isRaw);
    lsdjPhaseSlider_.setEnabled(!isRaw);
    lsdjPhaseModeBox_.setEnabled(!isRaw);
    slideTimeSlider_.setEnabled(true);
    frameSlider_.setEnabled(!isRaw);

    for (auto* button : { &sourceButton_, &gbRamButton_ }) {
        button->setEnabled(true);
    }

    for (auto* button : { &normalizeButton_, &smoothButton_ }) {
        button->setEnabled(!isRaw);
    }

    const auto loopInfo = processor_.rawStreamLoopInfo();
    loopToggleButton_.setEnabled(isRaw && loopInfo.hasContent);
    loopToggleButton_.setToggleState(loopInfo.enabled, juce::dontSendNotification);
}

void HyperFrameAudioProcessorEditor::setEngineModeFromEditor(int modeIndex) {
    processor_.setEngineModeWithProfile(modeIndex);
    engineModeBox_.setSelectedId(modeIndex + 1, juce::dontSendNotification);
    syncMotionStepControls();
    updateModeLabels();
    waveformDisplay_.repaint();
}

void HyperFrameAudioProcessorEditor::setMotionStepParameter(const char* field, float value) {
    if (updatingMotionStepControls_) {
        return;
    }

    const auto id = motionParamId(processor_.selectedMotionStep(), field);
    setParameterValueFromEditor(processor_.parameters(), id, value);
}
