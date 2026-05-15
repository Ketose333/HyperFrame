#pragma once

#include "PluginProcessor.h"

#include <juce_audio_utils/juce_audio_utils.h>
#include <juce_audio_processors/juce_audio_processors.h>

#include <array>
#include <memory>
#include <vector>

class WaveformDisplay final : public juce::Component,
                              private juce::Timer {
public:
    explicit WaveformDisplay(HyperFrameAudioProcessor& processor);
    ~WaveformDisplay() override = default;

    void paint(juce::Graphics& graphics) override;
    void mouseMove(const juce::MouseEvent& event) override;
    void mouseDown(const juce::MouseEvent& event) override;
    void mouseDrag(const juce::MouseEvent& event) override;
    void mouseUp(const juce::MouseEvent& event) override;

private:
    void timerCallback() override;
    juce::Rectangle<float> graphBounds() const;
    void editAtPosition(juce::Point<float> position, bool interpolateFromPrevious);

    HyperFrameAudioProcessor& processor_;
    bool hasLastEditPoint_ = false;
    int lastEditIndex_ = 0;
    float lastEditValue_ = 0.0f;
    std::vector<float> sampleDisplayMinimums_;
    std::vector<float> sampleDisplayMaximums_;

    enum class RawDragTarget { None, Start, End, PlayStart };
    RawDragTarget rawDragTarget_ = RawDragTarget::None;
    HyperFrameAudioProcessor::LoopInfo rawDragLoopInfo_ {};

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(WaveformDisplay)
};

class HyperFrameAudioProcessorEditor final : public juce::AudioProcessorEditor,
                                             private juce::Timer {
public:
    explicit HyperFrameAudioProcessorEditor(HyperFrameAudioProcessor& processor);
    ~HyperFrameAudioProcessorEditor() override = default;

    void paint(juce::Graphics& graphics) override;
    void resized() override;

private:
    using SliderAttachment = juce::AudioProcessorValueTreeState::SliderAttachment;
    using ButtonAttachment = juce::AudioProcessorValueTreeState::ButtonAttachment;
    using ComboBoxAttachment = juce::AudioProcessorValueTreeState::ComboBoxAttachment;

    static void configureSlider(juce::Slider& slider, juce::Label& label, const juce::String& text);
    static void configureSectionLabel(juce::Label& label, const juce::String& text);
    static void configureCommandButton(juce::TextButton& button, const juce::String& text);
    bool keyPressed(const juce::KeyPress& key) override;
    void timerCallback() override;
    void syncMotionStepControls();
    void updateMotionRateDisplay();
    void updateModeLabels();
    void setEngineModeFromEditor(int modeIndex);
    void setMotionStepParameter(const char* field, float value);
    void refreshPresetBox();

    HyperFrameAudioProcessor& processor_;
    WaveformDisplay waveformDisplay_;
    juce::MidiKeyboardComponent keyboardComponent_;

    juce::Slider waveLengthSlider_;
    juce::Slider waveBitsSlider_;
    juce::Slider frameSlider_;
    juce::Slider attackSlider_;
    juce::Slider decaySlider_;
    juce::Slider sustainSlider_;
    juce::Slider releaseSlider_;
    juce::Slider lsdjPhaseSlider_;
    juce::Slider slideTimeSlider_;
    juce::Slider motionRateSlider_;
    juce::Slider motionStepsSlider_;
    juce::Slider commandFrameSlider_;
    juce::Slider commandPitchSlider_;
    juce::Slider commandPhaseSlider_;
    juce::Slider commandLevelSlider_;
    juce::Slider gainSlider_;
    juce::ToggleButton interpolationToggle_;
    juce::ToggleButton rawPlayFullToggle_;
    juce::ToggleButton commandTableToggle_;
    juce::ToggleButton motionLoopToggle_;
    juce::ToggleButton monoToggle_;
    juce::ToggleButton adsrOverrideToggle_;
    juce::ComboBox presetBox_;
    juce::ComboBox commandStepBox_;
    juce::ComboBox engineModeBox_;
    juce::ComboBox commandClockBox_;
    juce::ComboBox lsdjPhaseModeBox_;
    juce::ComboBox waveShapeBox_;
    juce::TextButton normalizeButton_;
    juce::TextButton smoothButton_;
    juce::TextButton sourceButton_;
    juce::TextButton presetFileButton_;
    juce::TextButton gbRamButton_;
    juce::TextButton loopToggleButton_;

    juce::Label waveLengthLabel_;
    juce::Label waveBitsLabel_;
    juce::Label frameLabel_;
    juce::Label attackLabel_;
    juce::Label decayLabel_;
    juce::Label sustainLabel_;
    juce::Label releaseLabel_;
    juce::Label lsdjPhaseLabel_;
    juce::Label lsdjPhaseModeLabel_;
    juce::Label slideTimeLabel_;
    juce::Label motionRateLabel_;
    juce::Label motionStepsLabel_;
    juce::Label commandFrameLabel_;
    juce::Label commandPitchLabel_;
    juce::Label commandPhaseLabel_;
    juce::Label commandLevelLabel_;
    juce::Label gainLabel_;
    juce::Label presetLabel_;
    juce::Label commandStepLabel_;
    juce::Label engineModeLabel_;
    juce::Label commandClockLabel_;
    juce::Label gbRamStatusLabel_;
    juce::Label motionStepPlayLabel_;
    juce::Label waveSectionLabel_;
    juce::Label phaseSectionLabel_;
    juce::Label commandSectionLabel_;
    juce::Label commandLaneSectionLabel_;
    juce::Label envelopeSectionLabel_;
    juce::Label keyboardSectionLabel_;

    std::unique_ptr<SliderAttachment> waveLengthAttachment_;
    std::unique_ptr<SliderAttachment> waveBitsAttachment_;
    std::unique_ptr<SliderAttachment> frameAttachment_;
    std::unique_ptr<SliderAttachment> attackAttachment_;
    std::unique_ptr<SliderAttachment> decayAttachment_;
    std::unique_ptr<SliderAttachment> sustainAttachment_;
    std::unique_ptr<SliderAttachment> releaseAttachment_;
    std::unique_ptr<SliderAttachment> lsdjPhaseAttachment_;
    std::unique_ptr<SliderAttachment> slideTimeAttachment_;
    std::unique_ptr<SliderAttachment> motionRateAttachment_;
    std::unique_ptr<SliderAttachment> motionStepsAttachment_;
    std::unique_ptr<SliderAttachment> gainAttachment_;
    std::unique_ptr<ButtonAttachment> interpolationAttachment_;
    std::unique_ptr<ButtonAttachment> rawPlayFullAttachment_;
    std::unique_ptr<ButtonAttachment> commandTableAttachment_;
    std::unique_ptr<ButtonAttachment> motionLoopAttachment_;
    std::unique_ptr<ButtonAttachment> monoAttachment_;
    std::unique_ptr<ButtonAttachment> adsrOverrideAttachment_;
    std::unique_ptr<ComboBoxAttachment> commandClockAttachment_;
    std::unique_ptr<ComboBoxAttachment> lsdjPhaseModeAttachment_;
    std::array<juce::Rectangle<int>, 8> sectionBounds_;
    std::unique_ptr<juce::FileChooser> sampleChooser_;
    std::unique_ptr<juce::FileChooser> presetChooser_;
    juce::File lastFileChooserDirectory_ = juce::File::getSpecialLocation(juce::File::userDocumentsDirectory);
    bool updatingMotionStepControls_ = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(HyperFrameAudioProcessorEditor)
};
