#pragma once

#include "dsp/SynthEngine.h"

#include <juce_audio_formats/juce_audio_formats.h>
#include <juce_audio_utils/juce_audio_utils.h>
#include <juce_audio_processors/juce_audio_processors.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <memory>
#include <optional>
#include <vector>

class HyperFrameAudioProcessor final : public juce::AudioProcessor,
                                    private juce::AudioProcessorValueTreeState::Listener,
                                    private juce::AsyncUpdater {
public:
    HyperFrameAudioProcessor();
    ~HyperFrameAudioProcessor() override;

    void prepareToPlay(double sampleRate, int samplesPerBlock) override;
    void releaseResources() override;
    bool isBusesLayoutSupported(const BusesLayout& layouts) const override;
    void processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midiMessages) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override;

    const juce::String getName() const override;
    bool acceptsMidi() const override;
    bool producesMidi() const override;
    bool isMidiEffect() const override;
    double getTailLengthSeconds() const override;

    int getNumPrograms() override;
    int getCurrentProgram() override;
    void setCurrentProgram(int index) override;
    const juce::String getProgramName(int index) override;
    void changeProgramName(int index, const juce::String& newName) override;

    void getStateInformation(juce::MemoryBlock& destData) override;
    void setStateInformation(const void* data, int sizeInBytes) override;

    juce::AudioProcessorValueTreeState& parameters();
    const hyperframe::dsp::WaveTable& waveTable() const;
    float waveDisplayPoint(std::size_t index, std::size_t activeLength, int bitDepth) const;
    bool hasRawStream() const;
    bool rawStreamDisplayRanges(std::size_t bucketCount, std::vector<float>& minimums, std::vector<float>& maximums) const;
    void setWaveDisplayPoint(std::size_t index, std::size_t activeLength, float value);
    void applyWaveShape(hyperframe::dsp::WaveTable::Shape shape);
    void normalizeWaveTable();
    void smoothWaveTable();
    enum class AudioSourceImportTarget {
        RawStream,
        Wavetable,
        WaveFrames
    };

    struct AudioSourceImportOptions {
        AudioSourceImportTarget target = AudioSourceImportTarget::RawStream;
        double startMs = 0.0;
        double lengthMs = 0.0;
        bool normalize = true;
        bool zeroCrossingSnap = true;
    };

    struct LoopInfo {
        bool hasContent = false;
        bool enabled = false;
        std::size_t sampleCount = 0;
        std::size_t start = 0;
        std::size_t end = 0;
        std::size_t playStart = 0;
    };

    enum class BytebeatImportTarget {
        RawStream,
        WaveFrames
    };

    struct BytebeatImportOptions {
        BytebeatImportTarget target = BytebeatImportTarget::RawStream;
        juce::String expression = "t*((t>>12|t>>8)&63&t>>4)";
        double sampleRate = 8000.0;
        std::uint32_t startTick = 0;
        std::uint32_t loopTicks = 65536;
        double frameStrideMs = 250.0;
        bool normalizeFrames = true;
    };

    bool importAudioSourceFile(const juce::File& file, const AudioSourceImportOptions& options = {});
    bool importBytebeat(const BytebeatImportOptions& options);
    LoopInfo rawStreamLoopInfo() const;
    LoopInfo drawLoopInfo() const;
    void setRawStreamLoop(bool enabled, std::size_t start, std::size_t end, std::size_t playStart);
    bool convertRawStreamToDrawExact();
    bool exportPresetFile(const juce::File& file);
    bool importPresetFile(const juce::File& file);
    bool importSoundFontBank(const juce::File& file);
    bool importLsdjBank(const juce::File& file);
    juce::String exportGameBoyWaveRamHex() const;
    void setEngineModeWithProfile(int modeIndex);
    void selectMotionStep(int index);
    int selectedMotionStep() const;
    int currentProgramSelection() const;
    int currentMotionStep() const;
    double hostBpm() const;
    juce::MidiKeyboardState& keyboardState();

    void beginWaveEdit();
    void commitWaveEdit();
    bool canUndoWaveEdit() const;
    bool canRedoWaveEdit() const;
    void undoWaveEdit();
    void redoWaveEdit();

    void copyCurrentFrame();
    void pasteToCurrentFrame();
    bool hasFrameClipboard() const;

private:
    struct Program;
    enum class ImportedProgramKind {
        DrawCycle,
        LsdjWaveFrames
    };

    struct ImportedProgram {
        ImportedProgramKind kind = ImportedProgramKind::DrawCycle;
        juce::String name;
        std::vector<float> cycle;
        std::array<std::array<std::uint8_t, 16>, hyperframe::dsp::kWaveFrameCount> waveRamFrames {};
        int waveFrameCount = 0;
        std::array<int, hyperframe::dsp::kCommandStepCount> waveMotionFrames {};
        std::array<float, hyperframe::dsp::kCommandStepCount> waveMotionPitch {};
        std::array<float, hyperframe::dsp::kCommandStepCount> waveMotionPitchBend {};
        std::array<float, hyperframe::dsp::kCommandStepCount> waveMotionPhase {};
        std::array<float, hyperframe::dsp::kCommandStepCount> waveMotionVibratoRate {};
        std::array<float, hyperframe::dsp::kCommandStepCount> waveMotionLevel {};
        int waveMotionSteps = 0;
        int waveMotionLoopStart = 0;
        bool waveMotionEnabled = true;
        bool waveMotionLoop = true;
        float waveMotionRate = 12.0f;
        float waveLevel = 1.0f;
        int waveSelectedFrame = 1;
        double sampleRate = 44100.0;
        float attack = 0.0f;
        float decay = 0.0f;
        float sustain = 1.0f;
        float release = 0.031f;
        hyperframe::dsp::RawStreamLoop loop {};
    };

    static juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();
    static const std::vector<Program>& programs();
    void updateEngineParameters();
    void applyEngineModeProfile(hyperframe::dsp::CommandSettings::EngineMode engineMode);
    void setParameterValue(const char* id, float value);
    void setRawStreamPlaybackDefaults();
    void setMotionStepValues(int stepIndex, float frame, float pitch, float bend, float phase, float vibratoRate, float level);
    void setSequentialMotionStepDefaults();
    void addStrictParameterListeners();
    void removeStrictParameterListeners();
    void enforceWaveStrictParameters();
    void enforceMotionClockParameters();
    void updateHostTempo();
    void parameterChanged(const juce::String& parameterID, float newValue) override;
    void handleAsyncUpdate() override;
    int editableWaveFrameIndex() const;
    void restoreWaveTableState(const juce::ValueTree& state);
    juce::ValueTree createWaveTableState() const;
    void restoreRawStreamState(const juce::ValueTree& state);
    juce::ValueTree createRawStreamState() const;
    void updateMotionTableFromParameters();
    void setImportedProgram(const ImportedProgram& program);
    hyperframe::dsp::WaveTable& authoringWaveFrame(int index);
    const hyperframe::dsp::WaveTable& authoringWaveFrame(int index) const;
    void syncAuthoringWaveFrameProfileLocked(hyperframe::dsp::WaveTable& waveFrame);
    void clearRawSourceLocked();
    void publishSourceSnapshotLocked();
    void applyPendingSourceSnapshot();

    using WaveFrameSnapshot = std::array<hyperframe::dsp::WaveTable, hyperframe::dsp::kWaveFrameCount>;
    static constexpr int kMaxWaveUndoDepth = 50;

    juce::AudioProcessorValueTreeState parameters_;
    hyperframe::dsp::SynthEngine engine_;
    juce::MidiKeyboardState keyboardState_;
    hyperframe::dsp::CommandTable motionTable_{};
    std::array<hyperframe::dsp::WaveTable, hyperframe::dsp::kWaveFrameCount> waveFrames_{};
    std::shared_ptr<const std::vector<float>> rawStreamSnapshot_;
    std::shared_ptr<const hyperframe::dsp::SourceSnapshot> sourceSnapshot_;
    std::shared_ptr<const hyperframe::dsp::SourceSnapshot> activeSourceSnapshot_;
    mutable juce::CriticalSection waveTableLock_;
    int currentProgram_ = 0;
    int selectedMotionStep_ = 0;
    unsigned int waveSeed_ = 1;
    bool suppressHostNotification_ = false;
    std::vector<float> rawStream_;
    bool rawStreamSnapshotDirty_ = true;
    bool rawStreamHoldInterpolation_ = false;
    double rawStreamRate_ = 44100.0;
    double rawStreamSourceRootNote_ = 60.0;
    double rawStreamRootNote_ = 60.0;
    hyperframe::dsp::RawStreamLoop rawStreamLoop_ {};
    std::vector<ImportedProgram> importedPrograms_;
    double hostBpm_ = 120.0;
    std::deque<WaveFrameSnapshot> waveUndoStack_;
    std::deque<WaveFrameSnapshot> waveRedoStack_;
    WaveFrameSnapshot waveEditSnapshot_{};
    bool waveEditPending_ = false;
    std::optional<hyperframe::dsp::WaveTable> frameClipboard_;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(HyperFrameAudioProcessor)
};
