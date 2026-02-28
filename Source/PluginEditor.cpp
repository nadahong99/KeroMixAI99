#include "PluginProcessor.h"
#include "PluginEditor.h"

// ── Colours ───────────────────────────────────────────────────────────────────
static const juce::Colour kBg(0xffF7F9F7);
static const juce::Colour kCard(0xffffffff);
static const juce::Colour kGreen(0xff4C724D);
static const juce::Colour kKero(0xff99CC00);
static const juce::Colour kEqBg(0xffEAF4EA);
static const juce::Colour kCompBg(0xffFFF8EE);
static const juce::Colour kDelayBg(0xffF3EEFF);
static const juce::Colour kVerbBg(0xffE6F4FF);
static const juce::Colour kAiBg(0xffF3F8F3);
static const juce::Colour kPatchBg(0xffFAFAFA);
static const juce::Colour kBorder(0xffe8e8e8);
static const juce::Colour kLabel(0xff5c7c5d);
static const juce::Colour kLockOn(0xffff9800);
static const juce::Colour kLockOff(0xffe8e8e8);

// group per param
const int KeroMixAIAudioProcessorEditor::PARAM_GROUP[NUM_PARAMS] =
{ 0,0, 0,0,0, 0,0,  1,1,1,1,1,  2,2,2,  3,3,3,3,  4 };

// ── Constructor ───────────────────────────────────────────────────────────────
KeroMixAIAudioProcessorEditor::KeroMixAIAudioProcessorEditor(KeroMixAIAudioProcessor& p)
    : AudioProcessorEditor(&p), audioProcessor(p), juce::Thread("GroqThread")
{
    setSize(900, 540);

    // Sliders
    for (int i = 0; i < NUM_PARAMS; ++i)
    {
        auto& s = sliders[i];
        if (i == NUM_PARAMS - 1) // MASTER
        {
            s.setSliderStyle(juce::Slider::LinearHorizontal);
            s.setTextBoxStyle(juce::Slider::TextBoxRight, false, 50, 18);
        }
        else
        {
            s.setSliderStyle(juce::Slider::RotaryHorizontalVerticalDrag);
            s.setTextBoxStyle(juce::Slider::TextBoxBelow, false, 50, 15);
        }
        s.setColour(juce::Slider::rotarySliderOutlineColourId, kKero.withAlpha(0.18f));
        s.setColour(juce::Slider::rotarySliderFillColourId, kKero);
        s.setColour(juce::Slider::thumbColourId, juce::Colours::white);
        s.setColour(juce::Slider::textBoxTextColourId, kGreen);
        s.setColour(juce::Slider::textBoxOutlineColourId, juce::Colour(0xffdddddd));
        s.setColour(juce::Slider::textBoxBackgroundColourId, juce::Colours::white);
        s.setColour(juce::Slider::trackColourId, kKero.withAlpha(0.3f));
        addAndMakeVisible(s);

        auto& l = labels[i];
        l.setText(paramNames[i], juce::dontSendNotification);
        l.setJustificationType(juce::Justification::centred);
        l.setFont(juce::Font(9.5f, juce::Font::bold));
        l.setColour(juce::Label::textColourId, kLabel);
        addAndMakeVisible(l);

        attachments[i] = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
            audioProcessor.apvts, paramIDs[i], s);
    }

    // Lock buttons
    for (int g = 0; g < NUM_GROUPS; ++g)
    {
        auto& btn = lockBtns[g];
        btn.setButtonText(groupNames[g]);
        btn.setColour(juce::TextButton::buttonColourId, kLockOff);
        btn.setColour(juce::TextButton::textColourOffId, juce::Colour(0xff888888));
        btn.setClickingTogglesState(true);
        btn.onClick = [this, g]() {
            locked[g] = lockBtns[g].getToggleState();
            lockBtns[g].setColour(juce::TextButton::buttonColourId,
                locked[g] ? kLockOn : kLockOff);
            lockBtns[g].setColour(juce::TextButton::textColourOffId,
                locked[g] ? juce::Colours::white : juce::Colour(0xff888888));
            };
        addAndMakeVisible(btn);
    }

    // Prompt
    promptInput.setMultiLine(false);
    promptInput.setReturnKeyStartsNewLine(false);
    promptInput.setTextToShowWhenEmpty("Describe what you want...", juce::Colour(0xffaaaaaa));
    promptInput.setFont(juce::Font(12.f));
    promptInput.setColour(juce::TextEditor::backgroundColourId, juce::Colours::white);
    promptInput.setColour(juce::TextEditor::outlineColourId, kKero.withAlpha(0.5f));
    promptInput.setColour(juce::TextEditor::textColourId, juce::Colour(0xff333333));
    promptInput.onReturnKey = [this]() { sendToGroq(promptInput.getText()); };
    addAndMakeVisible(promptInput);

    sendBtn.setButtonText("Apply AI");
    sendBtn.setColour(juce::TextButton::buttonColourId, kKero);
    sendBtn.setColour(juce::TextButton::textColourOffId, juce::Colours::white);
    sendBtn.onClick = [this]() { sendToGroq(promptInput.getText()); };
    addAndMakeVisible(sendBtn);

    undoBtn.setButtonText("Undo");
    undoBtn.setColour(juce::TextButton::buttonColourId, juce::Colour(0xffffe0b2));
    undoBtn.setColour(juce::TextButton::textColourOffId, juce::Colour(0xff8d4a00));
    undoBtn.setEnabled(false);
    undoBtn.onClick = [this]() { restoreSnapshot(); };
    addAndMakeVisible(undoBtn);

    statusLabel.setText("", juce::dontSendNotification);
    statusLabel.setFont(juce::Font(10.5f));
    statusLabel.setColour(juce::Label::textColourId, juce::Colour(0xff888888));
    statusLabel.setJustificationType(juce::Justification::centredLeft);
    addAndMakeVisible(statusLabel);

    // Quick command buttons
    for (int i = 0; i < NUM_QUICK; ++i)
    {
        quickBtns[i].setButtonText(quickCmds[i]);
        quickBtns[i].setColour(juce::TextButton::buttonColourId, juce::Colour(0xffE8F5E9));
        quickBtns[i].setColour(juce::TextButton::textColourOffId, kGreen);
        quickBtns[i].onClick = [this, i]() { sendToGroq(quickCmds[i]); };
        addAndMakeVisible(quickBtns[i]);
    }

    // Settings button
    settingsBtn.setButtonText(juce::CharPointer_UTF8("\xe2\x9a\x99"));
    settingsBtn.setColour(juce::TextButton::buttonColourId, juce::Colour(0xffeeeeee));
    settingsBtn.setColour(juce::TextButton::textColourOffId, juce::Colour(0xff666666));
    settingsBtn.onClick = [this]() { showSettings(); };
    addAndMakeVisible(settingsBtn);

    // Patch UI
    patchNameInput.setTextToShowWhenEmpty("Patch name...", juce::Colour(0xffaaaaaa));
    patchNameInput.setFont(juce::Font(11.f));
    patchNameInput.setColour(juce::TextEditor::backgroundColourId, juce::Colours::white);
    patchNameInput.setColour(juce::TextEditor::outlineColourId, juce::Colour(0xffdddddd));
    patchNameInput.setColour(juce::TextEditor::textColourId, juce::Colour(0xff333333));
    addAndMakeVisible(patchNameInput);

    saveBtn.setButtonText("Save");
    saveBtn.setColour(juce::TextButton::buttonColourId, kKero);
    saveBtn.setColour(juce::TextButton::textColourOffId, juce::Colours::white);
    saveBtn.onClick = [this]() {
        auto name = patchNameInput.getText().trim();
        if (name.isEmpty()) { statusLabel.setText("Enter patch name!", juce::dontSendNotification); return; }
        audioProcessor.savePatch(name);
        refreshPatchList();
        statusLabel.setText("Saved: " + name, juce::dontSendNotification);
        };
    addAndMakeVisible(saveBtn);

    loadBtn.setButtonText("Load");
    loadBtn.setColour(juce::TextButton::buttonColourId, juce::Colour(0xff4a90d9));
    loadBtn.setColour(juce::TextButton::textColourOffId, juce::Colours::white);
    loadBtn.onClick = [this]() {
        auto name = patchList.getText();
        if (name.isEmpty()) return;
        audioProcessor.loadPatch(name);
        statusLabel.setText("Loaded: " + name, juce::dontSendNotification);
        };
    addAndMakeVisible(loadBtn);

    deleteBtn.setButtonText("Del");
    deleteBtn.setColour(juce::TextButton::buttonColourId, juce::Colour(0xffe57373));
    deleteBtn.setColour(juce::TextButton::textColourOffId, juce::Colours::white);
    deleteBtn.onClick = [this]() {
        auto name = patchList.getText();
        if (name.isEmpty()) return;
        audioProcessor.deletePatch(name);
        refreshPatchList();
        statusLabel.setText("Deleted: " + name, juce::dontSendNotification);
        };
    addAndMakeVisible(deleteBtn);

    patchList.setTextWhenNothingSelected("-- select --");
    patchList.setColour(juce::ComboBox::backgroundColourId, juce::Colours::white);
    patchList.setColour(juce::ComboBox::outlineColourId, juce::Colour(0xffdddddd));
    addAndMakeVisible(patchList);
    refreshPatchList();

    groqApiKey = loadApiKey();
    if (groqApiKey.isEmpty()) showSettings();

    startTimerHz(30);
}

KeroMixAIAudioProcessorEditor::~KeroMixAIAudioProcessorEditor()
{
    stopTimer();
    stopThread(2000);
}

// ── Settings panel ────────────────────────────────────────────────────────────
void KeroMixAIAudioProcessorEditor::showSettings()
{
    settingsPanel = std::make_unique<SettingsComponent>();
    settingsPanel->setBounds(getWidth() - 280, 40, 270, 170);
    settingsPanel->onKeyEntered = [this](const juce::String& key) {
        groqApiKey = key;
        saveApiKey(key);
        hideSettings();
        statusLabel.setText("API key saved!", juce::dontSendNotification);
        };
    settingsPanel->onClose = [this]() { hideSettings(); };
    addAndMakeVisible(*settingsPanel);
    settingsPanel->toFront(true);
}

void KeroMixAIAudioProcessorEditor::hideSettings()
{
    if (settingsPanel) { removeChildComponent(settingsPanel.get()); settingsPanel.reset(); }
}

// ── Patch ─────────────────────────────────────────────────────────────────────
void KeroMixAIAudioProcessorEditor::refreshPatchList()
{
    patchList.clear();
    auto names = audioProcessor.getSavedPatchNames();
    for (int i = 0; i < names.size(); ++i)
        patchList.addItem(names[i], i + 1);
}

// ── Undo ──────────────────────────────────────────────────────────────────────
void KeroMixAIAudioProcessorEditor::saveSnapshot()
{
    for (int i = 0; i < NUM_PARAMS; ++i)
        undoSnapshot[i] = (float)*audioProcessor.apvts.getRawParameterValue(paramIDs[i]);
    hasUndoSnapshot = true;
    undoBtn.setEnabled(true);
}

void KeroMixAIAudioProcessorEditor::restoreSnapshot()
{
    if (!hasUndoSnapshot) return;
    for (int i = 0; i < NUM_PARAMS; ++i)
        if (auto* param = audioProcessor.apvts.getParameter(paramIDs[i]))
            param->setValueNotifyingHost(
                audioProcessor.apvts.getParameterRange(paramIDs[i]).convertTo0to1(undoSnapshot[i]));
    undoBtn.setEnabled(false);
    hasUndoSnapshot = false;
    statusLabel.setText("Reverted.", juce::dontSendNotification);
}

// ── API key ───────────────────────────────────────────────────────────────────
void KeroMixAIAudioProcessorEditor::saveApiKey(const juce::String& key)
{
    auto dir = juce::File::getSpecialLocation(juce::File::userApplicationDataDirectory)
        .getChildFile("KeroMixAI");
    dir.createDirectory();
    dir.getChildFile("config.txt").replaceWithText(key);
}

juce::String KeroMixAIAudioProcessorEditor::loadApiKey()
{
    auto f = juce::File::getSpecialLocation(juce::File::userApplicationDataDirectory)
        .getChildFile("KeroMixAI").getChildFile("config.txt");
    return f.existsAsFile() ? f.loadFileAsString().trim() : juce::String();
}

// ── FFT ───────────────────────────────────────────────────────────────────────
void KeroMixAIAudioProcessorEditor::processFFT()
{
    fftWindow.multiplyWithWindowingTable(fftData, FFT_SIZE);
    fft.performFrequencyOnlyForwardTransform(fftData);

    const double sr = audioProcessor.getSampleRate();
    if (sr <= 0.0) return;
    const int    bins = FFT_SIZE / 2;
    const double bw = sr / FFT_SIZE;

    auto toDb = [&](int s, int e) -> float {
        float sum = 0.f; int n = 0;
        for (int i = s; i < e && i < bins; ++i) { sum += fftData[i] * fftData[i]; ++n; }
        if (!n) return -60.f;
        return juce::jmax(-60.f, juce::Decibels::gainToDecibels(std::sqrt(sum / n) / FFT_SIZE));
        };

    int lEnd = (int)(300.0 / bw), mEnd = (int)(4000.0 / bw), hEnd = (int)(20000.0 / bw);
    const float a = 0.15f;
    specLow += a * (toDb(1, lEnd) - specLow);
    specMid += a * (toDb(lEnd, mEnd) - specMid);
    specHigh += a * (toDb(mEnd, hEnd) - specHigh);
    fftDataReady = false;
}

void KeroMixAIAudioProcessorEditor::timerCallback()
{
    {
        juce::ScopedLock sl(audioProcessor.fftLock);
        if (audioProcessor.fftDataReady)
        {
            juce::zeromem(fftData, sizeof(fftData));
            memcpy(fftData, audioProcessor.fftFifo, sizeof(float) * FFT_SIZE);
            audioProcessor.fftDataReady = false;
            fftDataReady = true;
        }
    }
    if (fftDataReady) processFFT();
    repaint();
}

// ── Groq ──────────────────────────────────────────────────────────────────────
void KeroMixAIAudioProcessorEditor::sendToGroq(const juce::String& prompt)
{
    if (prompt.trim().isEmpty()) return;
    if (groqApiKey.isEmpty()) { showSettings(); return; }

    saveSnapshot();

    juce::String lockedList, currentParams = "{";
    for (int i = 0; i < NUM_PARAMS; ++i)
    {
        if (i > 0) currentParams << ",";
        currentParams << "\"" << paramIDs[i] << "\":"
            << (float)*audioProcessor.apvts.getRawParameterValue(paramIDs[i]);
        if (isParamInLockedGroup(i))
            lockedList += (lockedList.isEmpty() ? "" : ",") + paramIDs[i];
    }
    currentParams << "}";

    juce::String spec;
    spec << "Low:" << juce::String(specLow, 1) << "dB Mid:" << juce::String(specMid, 1)
        << "dB High:" << juce::String(specHigh, 1) << "dB";

    {
        juce::ScopedLock sl(threadLock);
        pendingPrompt = prompt + "|||" + currentParams + "|||" + lockedList + "|||" + spec;
    }

    statusLabel.setText("Processing...", juce::dontSendNotification);
    sendBtn.setEnabled(false);
    for (int i = 0; i < NUM_QUICK; ++i) quickBtns[i].setEnabled(false);
    if (!isThreadRunning()) startThread();
}

void KeroMixAIAudioProcessorEditor::run()
{
    juce::String full;
    { juce::ScopedLock sl(threadLock); full = pendingPrompt; }

    auto parts = juce::StringArray::fromTokens(full, "|||", "");
    auto uText = parts[0];
    auto curJson = parts.size() > 1 ? parts[1] : "{}";
    auto locked = parts.size() > 2 ? parts[2] : "";
    auto spec = parts.size() > 3 ? parts[3] : "";

    juce::String lockNote = locked.isEmpty()
        ? "No params locked."
        : "LOCKED - do NOT change: " + locked + ". Omit from JSON.";

    juce::String sys =
        "You are an audio mix engineer AI. "
        "Input: current param values, spectrum LOW/MID/HIGH dB, user request. "
        "Output: ONLY a JSON object with changed param keys. "
        "Params: lowG/midG/highG dB, lowFreq/midFreq/highFreq Hz, midQ 0.3-4, "
        "compThresh dB, compRatio, compAttack ms, compRelease ms, compMakeup dB, "
        "delayTime s, delayFeedback 0-0.9, delayMix 0-1, "
        "revDecay/revSize/revDamp/revMix 0-1, aimix 0-1. "
        "Rules:1.Small changes unless user says much/a lot. "
        "2.Base on current values. Never reset. "
        "3." + lockNote + " "
        "4.Use spectrum to guide EQ. "
        "5.warm=+lowG-highG.bright=+highG.punchy=+compRatio-compThresh."
        "airy=+revMix+revSize.dry=-revMix-delayMix.muddy=-lowG-midG.harsh=-highG. "
        "6.JSON only. No markdown.";

    auto esc = [](const juce::String& s) -> juce::String {
        juce::String r;
        for (int i = 0; i < s.length(); ++i) {
            auto c = s[i];
            if (c == '"')  r << "\\\"";
            else if (c == '\\') r << "\\\\";
            else if (c == '\n') r << "\\n";
            else if (c == '\r') r << "\\r";
            else if (c == '\t') r << "\\t";
            else              r << juce::String::charToString(c);
        }
        return r;
        };

    juce::String msgs = "[{\"role\":\"system\",\"content\":\"" + esc(sys) + "\"}";
    for (auto& m : chatHistory)
        msgs += ",{\"role\":\"" + esc(m.role) + "\",\"content\":\"" + esc(m.content) + "\"}";
    juce::String uc = "Spectrum:" + spec + " Params:" + curJson + " Request:" + esc(uText);
    msgs += ",{\"role\":\"user\",\"content\":\"" + esc(uc) + "\"}]";

    juce::String body;
    body << "{\"model\":\"llama-3.1-8b-instant\","
        << "\"messages\":" << msgs << ","
        << "\"max_tokens\":300,\"temperature\":0.2}";

    juce::URL url("https://api.groq.com/openai/v1/chat/completions");
    auto ws = url.withPOSTData(body).createInputStream(
        juce::URL::InputStreamOptions(juce::URL::ParameterHandling::inPostData)
        .withExtraHeaders("Authorization: Bearer " + groqApiKey + "\r\nContent-Type: application/json")
        .withConnectionTimeoutMs(12000));

    if (!ws) {
        juce::MessageManager::callAsync([this]() {
            statusLabel.setText("Connection failed.", juce::dontSendNotification);
            sendBtn.setEnabled(true);
            for (int i = 0; i < NUM_QUICK; ++i) quickBtns[i].setEnabled(true);
            });
        return;
    }

    juce::String resp = ws->readEntireStreamAsString();
    juce::String pj;
    int ci = resp.indexOf("\"content\":\"");
    if (ci >= 0) {
        int bs = resp.indexOf(ci, "{");
        if (bs >= 0) {
            int depth = 0, be = -1;
            for (int i = bs; i < resp.length(); ++i) {
                if (resp[i] == '{') ++depth;
                else if (resp[i] == '}') { if (--depth == 0) { be = i; break; } }
            }
            if (be > bs)
                pj = resp.substring(bs, be + 1).replace("\\n", " ").replace("\\\"", "\"").replace("\\/", "/");
        }
    }

    if (pj.isEmpty()) {
        juce::File::getSpecialLocation(juce::File::userDesktopDirectory)
            .getChildFile("keromix_debug.txt").replaceWithText(resp);
        juce::MessageManager::callAsync([this]() {
            statusLabel.setText("Parse failed. See keromix_debug.txt", juce::dontSendNotification);
            sendBtn.setEnabled(true);
            for (int i = 0; i < NUM_QUICK; ++i) quickBtns[i].setEnabled(true);
            });
        return;
    }

    chatHistory.push_back({ "user",      "Params:" + curJson + " Request:" + uText });
    chatHistory.push_back({ "assistant", pj });
    if ((int)chatHistory.size() > MAX_HISTORY * 2)
        chatHistory.erase(chatHistory.begin(), chatHistory.begin() + 2);

    juce::MessageManager::callAsync([this, pj]() { applyParamsFromJson(pj); });
}

void KeroMixAIAudioProcessorEditor::applyParamsFromJson(const juce::String& json)
{
    int applied = 0;
    for (int i = 0; i < NUM_PARAMS; ++i)
    {
        if (isParamInLockedGroup(i)) continue;
        juce::String search = "\"" + paramIDs[i] + "\":";
        int pos = json.indexOf(search);
        if (pos < 0) continue;
        pos += search.length();
        while (pos < json.length() && json[pos] == ' ') ++pos;
        int end = pos;
        while (end < json.length() &&
            (json[end] == '-' || json[end] == '.' || (json[end] >= '0' && json[end] <= '9'))) ++end;
        if (end > pos) {
            float v = json.substring(pos, end).getFloatValue();
            if (auto* param = audioProcessor.apvts.getParameter(paramIDs[i])) {
                auto range = audioProcessor.apvts.getParameterRange(paramIDs[i]);
                param->setValueNotifyingHost(range.convertTo0to1(juce::jlimit(range.start, range.end, v)));
                ++applied;
            }
        }
    }
    promptInput.clear();
    statusLabel.setText("AI applied! (" + juce::String(applied) + " params)", juce::dontSendNotification);
    sendBtn.setEnabled(true);
    for (int i = 0; i < NUM_QUICK; ++i) quickBtns[i].setEnabled(true);
}

// ── Draw helpers ──────────────────────────────────────────────────────────────
void KeroMixAIAudioProcessorEditor::drawKeropi(juce::Graphics& g, float x, float y, float sc)
{
    g.setColour(kKero);
    g.fillEllipse(x, y + 13 * sc, 55 * sc, 45 * sc);
    g.setColour(juce::Colours::black);
    g.drawEllipse(x, y + 13 * sc, 55 * sc, 45 * sc, 2 * sc);
    auto eye = [&](float ex, float ey) {
        g.setColour(juce::Colours::white); g.fillEllipse(ex, ey, 22 * sc, 22 * sc);
        g.setColour(juce::Colours::black); g.drawEllipse(ex, ey, 22 * sc, 22 * sc, 2 * sc);
        };
    eye(x - 2 * sc, y); eye(x + 35 * sc, y);
    g.setColour(juce::Colours::black);
    g.fillEllipse(x + 5 * sc, y + 7 * sc, 7 * sc, 7 * sc);
    g.fillEllipse(x + 42 * sc, y + 7 * sc, 7 * sc, 7 * sc);
    juce::Path mouth;
    mouth.startNewSubPath(x + 20 * sc, y + 38 * sc);
    mouth.lineTo(x + 27 * sc, y + 43 * sc);
    mouth.lineTo(x + 34 * sc, y + 38 * sc);
    g.strokePath(mouth, juce::PathStrokeType(2 * sc));
}

void KeroMixAIAudioProcessorEditor::drawSpectrumBar(juce::Graphics& g,
    float x, float y, float w, float h, float dB, juce::Colour col)
{
    float norm = juce::jlimit(0.f, 1.f, (dB + 60.f) / 60.f);
    g.setColour(col.withAlpha(0.2f)); g.fillRoundedRectangle(x, y, w, h, 3.f);
    g.setColour(col.withAlpha(0.85f)); g.fillRoundedRectangle(x, y + h - norm * h, w, norm * h, 3.f);
}

void KeroMixAIAudioProcessorEditor::drawSectionBg(juce::Graphics& g,
    juce::Rectangle<float> r, juce::Colour bg, const juce::String& title)
{
    g.setColour(bg); g.fillRoundedRectangle(r, 12.f);
    g.setColour(kGreen.withAlpha(0.7f));
    g.setFont(juce::Font("Arial", 11.f, juce::Font::bold));
    g.drawText(title, (int)r.getX() + 10, (int)r.getY() + 5, 60, 16, juce::Justification::left, false);
}

// ── Paint ─────────────────────────────────────────────────────────────────────
void KeroMixAIAudioProcessorEditor::paint(juce::Graphics& g)
{
    const float W = (float)getWidth(), H = (float)getHeight();
    const float pad = 10.f;

    g.setColour(kBg); g.fillAll();

    juce::Rectangle<float> card(pad, pad, W - pad * 2, H - pad * 2);
    g.setColour(kCard); g.fillRoundedRectangle(card, 18.f);
    g.setColour(kBorder); g.drawRoundedRectangle(card, 18.f, 1.5f);

    g.setColour(kGreen.withAlpha(0.06f));
    g.fillRoundedRectangle(pad, pad, W - pad * 2, 44.f, 18.f);

    drawKeropi(g, pad + 14, pad + 2, 0.62f);

    g.setColour(kGreen);
    g.setFont(juce::Font("Arial", 20.f, juce::Font::bold));
    g.drawText("KeroMixAI", (int)(pad + 62), (int)(pad + 8), 180, 26, juce::Justification::left, false);
    g.setFont(juce::Font(9.f));
    g.setColour(juce::Colour(0xffaaaaaa));
    g.drawText("by NADAHONG", (int)(pad + 62), (int)(pad + 28), 140, 14, juce::Justification::left, false);

    const float dspY = pad + 48.f;
    const float leftW = W * 0.62f;
    const float rightX = leftW + 4.f;
    const float rightW = W - rightX - pad;
    float colW = (leftW - pad * 2 - 8.f) / 2.f;

    drawSectionBg(g, { pad + 4, dspY,        colW, 190.f }, kEqBg, "EQ");
    drawSectionBg(g, { pad + 4, dspY + 196.f,  colW, 240.f }, kCompBg, "COMP");
    drawSectionBg(g, { pad + 4 + colW + 6, dspY,        colW, 130.f }, kDelayBg, "DELAY");
    drawSectionBg(g, { pad + 4 + colW + 6, dspY + 136.f,  colW, 300.f }, kVerbBg, "REVERB");

    g.setColour(juce::Colour(0xffF0F4F0));
    g.fillRoundedRectangle(pad + 4, H - pad - 36.f, leftW - 8.f, 30.f, 8.f);
    g.setColour(kGreen.withAlpha(0.5f));
    g.setFont(juce::Font(9.f, juce::Font::bold));
    g.drawText("MASTER", (int)(pad + 8), (int)(H - pad - 34), 50, 14, juce::Justification::left, false);

    g.setColour(kBorder);
    g.drawLine(leftW + 2, dspY, leftW + 2, H - pad, 1.f);

    drawSectionBg(g, { rightX, dspY,        rightW, 178.f }, kAiBg, "AI MIX");
    drawSectionBg(g, { rightX, dspY + 184.f,  rightW, 68.f }, kPatchBg, "PATCHES");

    g.setColour(juce::Colour(0xffEEF4EE));
    g.fillRoundedRectangle(rightX, dspY + 258.f, rightW, 55.f, 8.f);
    g.setColour(kGreen.withAlpha(0.4f));
    g.setFont(juce::Font(9.f, juce::Font::bold));
    g.drawText("SPECTRUM", (int)rightX + 10, (int)(dspY + 262), 65, 12, juce::Justification::left, false);

    float bW = 22.f, bH = 32.f, bY = dspY + 262.f;
    float bX = rightX + rightW / 2.f - bW * 1.5f - 4.f;
    drawSpectrumBar(g, bX, bY, bW, bH, specLow, juce::Colour(0xff66bb6a));
    drawSpectrumBar(g, bX + bW + 4, bY, bW, bH, specMid, juce::Colour(0xff42a5f5));
    drawSpectrumBar(g, bX + bW * 2 + 8, bY, bW, bH, specHigh, juce::Colour(0xffef5350));
    g.setFont(juce::Font(8.f)); g.setColour(juce::Colour(0xff999999));
    g.drawText("L", (int)bX, (int)(bY + bH + 1), (int)bW, 10, juce::Justification::centred, false);
    g.drawText("M", (int)(bX + bW + 4), (int)(bY + bH + 1), (int)bW, 10, juce::Justification::centred, false);
    g.drawText("H", (int)(bX + bW * 2 + 8), (int)(bY + bH + 1), (int)bW, 10, juce::Justification::centred, false);
}

// ── Resized ───────────────────────────────────────────────────────────────────
void KeroMixAIAudioProcessorEditor::resized()
{
    const float W = (float)getWidth();
    const float H = (float)getHeight();
    const float pad = 10.f;
    const float dspY = pad + 52.f;
    const float leftW = W * 0.62f;
    const float colW = (leftW - pad * 2 - 8.f) / 2.f;
    const float rightX = leftW + 4.f;
    const float rightW = W - rightX - pad;

    auto placeRow = [&](int startIdx, int count, float rx, float ry, float rw, float rh, int topSkip)
        {
            const int kW = 58, kH = 58, lH = 14;
            const float spacing = rw / count;
            for (int i = 0; i < count; ++i)
            {
                int cx = (int)(rx + i * spacing + (spacing - kW) * 0.5f);
                int cy = (int)(ry + topSkip);
                labels[startIdx + i].setBounds(cx, cy, kW, lH);
                sliders[startIdx + i].setBounds(cx, cy + lH, kW, kH);
            }
        };

    {
        const int kW = 58, kH = 58, lH = 14, topSkip = 22;
        float rx = pad + 6, ry = dspY, rw = colW - 4;
        float sp = rw / 3.f;
        int gainIdx[3] = { 0,2,5 };
        for (int i = 0; i < 3; ++i) {
            int cx = (int)(rx + i * sp + (sp - kW) * 0.5f), cy = (int)(ry + topSkip);
            labels[gainIdx[i]].setBounds(cx, cy, kW, lH);
            sliders[gainIdx[i]].setBounds(cx, cy + lH, kW, kH);
        }
        int fIdx[4] = { 1,3,4,6 };
        float fSp = rw / 4.f;
        for (int i = 0; i < 4; ++i) {
            int cx = (int)(rx + i * fSp + (fSp - kW) * 0.5f), cy = (int)(ry + topSkip + kH + lH + 4);
            labels[fIdx[i]].setBounds(cx, cy, kW - 4, lH);
            sliders[fIdx[i]].setBounds(cx, cy + lH, kW - 4, 50);
        }
    }

    placeRow(7, 5, pad + 6, dspY + 196.f, colW - 4, 90.f, 22);
    placeRow(12, 3, pad + 6 + colW + 6, dspY, colW - 4, 90.f, 22);
    placeRow(15, 4, pad + 6 + colW + 6, dspY + 136.f, colW - 4, 90.f, 22);

    {
        labels[19].setBounds((int)(pad + 8), (int)(H - pad - 34), 52, 16);
        sliders[19].setBounds((int)(pad + 62), (int)(H - pad - 38), (int)(leftW - 74), 32);
    }

    {
        float lbW = (leftW - pad * 2 - 16.f) / NUM_GROUPS;
        for (int g = 0; g < NUM_GROUPS; ++g)
            lockBtns[g].setBounds((int)(pad + 6 + g * (lbW + 4)), (int)(pad + 30), (int)lbW, 16);
    }

    settingsBtn.setBounds((int)(W - pad - 32), (int)(pad + 10), 24, 24);

    {
        float aiX = rightX + 8, aiW = rightW - 16;
        float aiY = dspY + 22;

        float qW = (aiW - 12.f) / 4.f;
        for (int i = 0; i < NUM_QUICK; ++i) {
            int row = i / 4, col = i % 4;
            quickBtns[i].setBounds((int)(aiX + col * (qW + 4)), (int)(aiY + row * 28), (int)(qW), 24);
        }

        float pY = aiY + 64.f;
        promptInput.setBounds((int)aiX, (int)pY, (int)(aiW - 68), 28);
        sendBtn.setBounds((int)(aiX + aiW - 64), (int)pY, 60, 28);

        undoBtn.setBounds((int)aiX, (int)(pY + 34), 56, 22);
        statusLabel.setBounds((int)(aiX + 62), (int)(pY + 36), (int)(aiW - 62), 18);
    }

    {
        float pX = rightX + 8, pW = rightW - 16, pY = dspY + 184 + 10;
        float nW = (pW - 8.f) * 0.38f;
        patchNameInput.setBounds((int)pX, (int)pY, (int)nW, 26);
        saveBtn.setBounds((int)(pX + nW + 4), (int)pY, 44, 26);
        patchList.setBounds((int)(pX + nW + 52), (int)pY, (int)(pW - nW - 104), 26);
        loadBtn.setBounds((int)(pX + pW - 52), (int)pY, 40, 26);
        deleteBtn.setVisible(false);
    }

    if (settingsPanel)
        settingsPanel->setBounds((int)(W - 290), 40, 270, 170);
}

void KeroMixAIAudioProcessorEditor::mouseDown(const juce::MouseEvent&)
{
    hideSettings();
}
