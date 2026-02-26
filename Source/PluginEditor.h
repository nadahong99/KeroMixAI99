#pragma once
#include <JuceHeader.h>
#include "PluginProcessor.h"

// ─── Settings Screen (API Key) ────────────────────────────────────────────────
class SettingsComponent : public juce::Component
{
public:
    std::function<void(const juce::String&)> onKeyEntered;
    std::function<void()> onClose;

    SettingsComponent()
    {
        titleLabel.setText("Settings", juce::dontSendNotification);
        titleLabel.setFont(juce::Font("Arial", 20.f, juce::Font::bold));
        titleLabel.setColour(juce::Label::textColourId, juce::Colour(0xff4C724D));
        addAndMakeVisible(titleLabel);

        apiLabel.setText("Groq API Key", juce::dontSendNotification);
        apiLabel.setFont(juce::Font(12.f));
        apiLabel.setColour(juce::Label::textColourId, juce::Colour(0xff666666));
        addAndMakeVisible(apiLabel);

        keyInput.setTextToShowWhenEmpty("gsk_...", juce::Colour(0xffaaaaaa));
        keyInput.setFont(juce::Font(12.f));
        keyInput.setColour(juce::TextEditor::backgroundColourId, juce::Colour(0xfff5faf5));
        keyInput.setColour(juce::TextEditor::outlineColourId, juce::Colour(0xff99CC00));
        keyInput.setColour(juce::TextEditor::textColourId, juce::Colour(0xff333333));
        keyInput.setPasswordCharacter(0x25CF);
        addAndMakeVisible(keyInput);

        saveBtn.setButtonText("Save");
        saveBtn.setColour(juce::TextButton::buttonColourId, juce::Colour(0xff99CC00));
        saveBtn.setColour(juce::TextButton::textColourOffId, juce::Colours::white);
        saveBtn.onClick = [this]() {
            auto key = keyInput.getText().trim();
            if (key.isNotEmpty() && onKeyEntered) onKeyEntered(key);
            };
        addAndMakeVisible(saveBtn);

        closeBtn.setButtonText("X");
        closeBtn.setColour(juce::TextButton::buttonColourId, juce::Colour(0xffeeeeee));
        closeBtn.setColour(juce::TextButton::textColourOffId, juce::Colour(0xff888888));
        closeBtn.onClick = [this]() { if (onClose) onClose(); };
        addAndMakeVisible(closeBtn);

        hintLabel.setText("Get a free key at console.groq.com", juce::dontSendNotification);
        hintLabel.setFont(juce::Font(10.f));
        hintLabel.setColour(juce::Label::textColourId, juce::Colour(0xffaaaaaa));
        hintLabel.setJustificationType(juce::Justification::centred);
        addAndMakeVisible(hintLabel);
    }

    void resized() override
    {
        closeBtn.setBounds(getWidth() - 32, 8, 24, 24);
        titleLabel.setBounds(16, 8, getWidth() - 60, 28);
        apiLabel.setBounds(16, 46, getWidth() - 32, 18);
        keyInput.setBounds(16, 66, getWidth() - 32, 30);
        saveBtn.setBounds(16, 104, 80, 28);
        hintLabel.setBounds(16, 140, getWidth() - 32, 18);
    }

    void paint(juce::Graphics& g) override
    {
        g.fillAll(juce::Colours::white);
        g.setColour(juce::Colour(0xffdddddd));
        g.drawRoundedRectangle(getLocalBounds().toFloat().reduced(1), 12.f, 1.5f);
    }

private:
    juce::Label      titleLabel, apiLabel, hintLabel;
    juce::TextEditor keyInput;
    juce::TextButton saveBtn, closeBtn;
};


// ─── Main Editor ─────────────────────────────────────────────────────────────
class KeroMixAIAudioProcessorEditor : public juce::AudioProcessorEditor,
    private juce::Thread,
    private juce::Timer
{
public:
    KeroMixAIAudioProcessorEditor(KeroMixAIAudioProcessor&);
    ~KeroMixAIAudioProcessorEditor() override;

    void paint(juce::Graphics&) override;
    void resized() override;
    void mouseDown(const juce::MouseEvent&) override;

private:
    KeroMixAIAudioProcessor& audioProcessor;

    // ── Parameters ────────────────────────────────────────────────────────
    static const int NUM_PARAMS = 20;
    juce::Slider sliders[NUM_PARAMS];
    juce::Label  labels[NUM_PARAMS];
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> attachments[NUM_PARAMS];

    const juce::String paramIDs[NUM_PARAMS] = {
        "lowG","lowFreq","midG","midFreq","midQ","highG","highFreq",
        "compThresh","compRatio","compAttack","compRelease","compMakeup",
        "delayTime","delayFeedback","delayMix",
        "revDecay","revSize","revDamp","revMix",
        "aimix"
    };
    const juce::String paramNames[NUM_PARAMS] = {
        "Low Gain","Low Freq","Mid Gain","Mid Freq","Mid Q","High Gain","High Freq",
        "Thresh","Ratio","Attack","Release","Makeup",
        "Time","Feedback","Mix",
        "Decay","Size","Damp","Mix",
        "MASTER"
    };

    // ── Lock groups ───────────────────────────────────────────────────────
    static const int NUM_GROUPS = 5;
    juce::TextButton lockBtns[NUM_GROUPS];
    bool             locked[NUM_GROUPS] = {};
    const juce::String groupNames[NUM_GROUPS] = { "EQ","COMP","DELAY","REVERB","MASTER" };
    static const int PARAM_GROUP[NUM_PARAMS];
    bool isParamInLockedGroup(int i) const { return locked[PARAM_GROUP[i]]; }

    // ── AI UI ─────────────────────────────────────────────────────────────
    juce::TextEditor  promptInput;
    juce::TextButton  sendBtn, undoBtn;
    juce::Label       statusLabel;

    // AI quick commands
    static const int NUM_QUICK = 8;
    juce::TextButton  quickBtns[NUM_QUICK];
    const juce::String quickCmds[NUM_QUICK] = {
        "Warmer",
        "Brighter",
        "More punch",
        "Add reverb",
        "Dry & clean",
        "Reduce mud",
        "Airy & spacious",
        "Subtle overall"
    };

    // ── Settings button + panel ───────────────────────────────────────────
    juce::TextButton  settingsBtn;
    std::unique_ptr<SettingsComponent> settingsPanel;
    void showSettings();
    void hideSettings();

    // ── Patch UI ──────────────────────────────────────────────────────────
    juce::TextButton  saveBtn, loadBtn, deleteBtn;
    juce::ComboBox    patchList;
    juce::TextEditor  patchNameInput;
    void refreshPatchList();

    // ── Undo ──────────────────────────────────────────────────────────────
    float undoSnapshot[NUM_PARAMS] = {};
    bool  hasUndoSnapshot = false;
    void  saveSnapshot();
    void  restoreSnapshot();

    // ── Chat history ──────────────────────────────────────────────────────
    struct ChatMessage { juce::String role, content; };
    std::vector<ChatMessage> chatHistory;
    static const int MAX_HISTORY = 6;

    // ── FFT ───────────────────────────────────────────────────────────────
    static const int FFT_ORDER = 11;
    static const int FFT_SIZE = 1 << FFT_ORDER;
    juce::dsp::FFT                       fft{ FFT_ORDER };
    juce::dsp::WindowingFunction<float>  fftWindow{ FFT_SIZE,
                                          juce::dsp::WindowingFunction<float>::hann };
    float fftFifo[FFT_SIZE] = {};
    float fftData[FFT_SIZE * 2] = {};
    int   fftFifoIndex = 0;
    bool  fftDataReady = false;
    float specLow = -60.f, specMid = -60.f, specHigh = -60.f;

    void processFFT();
    void timerCallback() override;

    // ── Groq thread ───────────────────────────────────────────────────────
    juce::String          pendingPrompt;
    juce::CriticalSection threadLock;
    void run() override;
    void sendToGroq(const juce::String& prompt);
    void applyParamsFromJson(const juce::String& json);

    // ── API key ───────────────────────────────────────────────────────────
    juce::String groqApiKey;
    void         saveApiKey(const juce::String& key);
    juce::String loadApiKey();

    // ── Draw ──────────────────────────────────────────────────────────────
    void drawKeropi(juce::Graphics& g, float x, float y, float scale);
    void drawSpectrumBar(juce::Graphics& g, float x, float y,
        float w, float h, float dB, juce::Colour col);
    void drawSectionBg(juce::Graphics& g, juce::Rectangle<float> r,
        juce::Colour bg, const juce::String& title);

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(KeroMixAIAudioProcessorEditor)
};