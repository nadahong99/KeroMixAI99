#pragma once
#include <JuceHeader.h>

class KeroMixAIAudioProcessor : public juce::AudioProcessor
{
public:
    KeroMixAIAudioProcessor();
    ~KeroMixAIAudioProcessor() override;

    void prepareToPlay(double sampleRate, int samplesPerBlock) override;
    void releaseResources() override;
    bool isBusesLayoutSupported(const BusesLayout& layouts) const override;
    void processBlock(juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return "KeroMixAI"; }
    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return false; }
    double getTailLengthSeconds() const override { return 2.0; }

    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram(int) override {}
    const juce::String getProgramName(int) override { return {}; }
    void changeProgramName(int, const juce::String&) override {}

    void getStateInformation(juce::MemoryBlock& destData) override;
    void setStateInformation(const void* data, int sizeInBytes) override;

    // Patch
    void savePatch(const juce::String& name);
    juce::StringArray getSavedPatchNames();
    bool loadPatch(const juce::String& name);
    bool deletePatch(const juce::String& name);
    juce::File getPatchDirectory();

    juce::AudioProcessorValueTreeState apvts;
    std::atomic<bool> bypassed{ false };

    // FFT
    static const int FFT_SIZE = 2048;
    float fftFifo[FFT_SIZE] = {};
    int   fftFifoIndex = 0;
    bool  fftDataReady = false;
    juce::CriticalSection fftLock;

private:
    juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();

    // 최대 2채널 고정 배열 (모노=ch0만 사용, 스테레오=ch0,ch1)
    juce::IIRFilter eqFilters[2][3];
    float           compEnv[2]    = { 0.f, 0.f };
    float           compGainDb[2] = { 0.f, 0.f };

    juce::AudioBuffer<float> delayBuffer;
    int writePos = 0;

    juce::Reverb reverbEngine;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(KeroMixAIAudioProcessor)
};
