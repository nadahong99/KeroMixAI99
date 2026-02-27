#include "PluginProcessor.h"
#include "PluginEditor.h"

// ── Static & Group Data ──────────────────────────────────────────────────────
const int KeroMixAIAudioProcessorEditor::PARAM_GROUP[NUM_PARAMS] =
{ 0,0, 0,0,0, 0,0,  1,1,1,1,1,  2,2,2,  3,3,3,3,  4 };

// ── Colours ───────────────────────────────────────────────────────────────────
static const juce::Colour kBg(0xffF7F9F7);
static const juce::Colour kCard(0xffffffff);
static const juce::Colour kGreen(0xff4C724D);
static const juce::Colour kKero(0xff99CC00);
static const juce::Colour kBorder(0xffe8e8e8);
static const juce::Colour kLabel(0xff5c7c5d);
static const juce::Colour kLockOn(0xffff9800);
static const juce::Colour kLockOff(0xffe8e8e8);

// ── Constructor ───────────────────────────────────────────────────────────────
KeroMixAIAudioProcessorEditor::KeroMixAIAudioProcessorEditor(KeroMixAIAudioProcessor& p)
    : AudioProcessorEditor(&p), audioProcessor(p), juce::Thread("GroqThread")
{
    setSize(900, 540);

    // 슬라이더 및 레이블 초기화
    for (int i = 0; i < NUM_PARAMS; ++i)
    {
        auto& s = sliders[i];
        if (i == NUM_PARAMS - 1) { // MASTER GAIN
            s.setSliderStyle(juce::Slider::LinearHorizontal);
            s.setTextBoxStyle(juce::Slider::TextBoxRight, false, 50, 18);
        }
        else {
            s.setSliderStyle(juce::Slider::RotaryHorizontalVerticalDrag);
            s.setTextBoxStyle(juce::Slider::TextBoxBelow, false, 50, 15);
        }
        s.setColour(juce::Slider::rotarySliderFillColourId, kKero);
        s.setColour(juce::Slider::textBoxTextColourId, kGreen);
        s.setColour(juce::Slider::textBoxOutlineColourId, juce::Colour(0xffdddddd));
        addAndMakeVisible(s);

        auto& l = labels[i];
        l.setText(paramNames[i], juce::dontSendNotification);
        l.setJustificationType(juce::Justification::centred);
        l.setFont(juce::Font(10.0f, juce::Font::bold));
        l.setColour(juce::Label::textColourId, kLabel);
        addAndMakeVisible(l);

        attachments[i] = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
            audioProcessor.apvts, paramIDs[i], s);
    }

    // 그룹 잠금 버튼
    for (int g = 0; g < NUM_GROUPS; ++g)
    {
        auto& btn = lockBtns[g];
        btn.setButtonText(groupNames[g]);
        btn.setColour(juce::TextButton::buttonColourId, kLockOff);
        btn.setClickingTogglesState(true);
        btn.onClick = [this, g]() {
            locked[g] = lockBtns[g].getToggleState();
            lockBtns[g].setColour(juce::TextButton::buttonColourId, locked[g] ? kLockOn : kLockOff);
            };
        addAndMakeVisible(btn);
    }

    // AI 입력 창 및 버튼
    promptInput.setMultiLine(false);
    promptInput.setTextToShowWhenEmpty("부사(약간, 훨씬 등)를 넣어 명령하세요...", juce::Colour(0xffaaaaaa));
    promptInput.onReturnKey = [this]() { sendToGroq(promptInput.getText()); };
    addAndMakeVisible(promptInput);

    sendBtn.setButtonText("Apply AI");
    sendBtn.setColour(juce::TextButton::buttonColourId, kKero);
    sendBtn.onClick = [this]() { sendToGroq(promptInput.getText()); };
    addAndMakeVisible(sendBtn);

    undoBtn.setButtonText("Undo");
    undoBtn.setEnabled(false);
    undoBtn.onClick = [this]() { restoreSnapshot(); };
    addAndMakeVisible(undoBtn);

    statusLabel.setJustificationType(juce::Justification::centredLeft);
    addAndMakeVisible(statusLabel);

    // 퀵 버튼 8개
    for (int i = 0; i < NUM_QUICK; ++i)
    {
        quickBtns[i].setButtonText(quickCmds[i]);
        quickBtns[i].onClick = [this, i]() { sendToGroq(quickCmds[i]); };
        addAndMakeVisible(quickBtns[i]);
    }

    settingsBtn.setButtonText(juce::CharPointer_UTF8("\xe2\x9a\x99"));
    settingsBtn.onClick = [this]() { showSettings(); };
    addAndMakeVisible(settingsBtn);

    // 패치 시스템
    patchNameInput.setTextToShowWhenEmpty("Patch name...", juce::Colour(0xffaaaaaa));
    addAndMakeVisible(patchNameInput);

    saveBtn.setButtonText("Save");
    saveBtn.onClick = [this]() {
        auto name = patchNameInput.getText().trim();
        if (name.isNotEmpty()) { audioProcessor.savePatch(name); refreshPatchList(); }
        };
    addAndMakeVisible(saveBtn);

    loadBtn.setButtonText("Load");
    loadBtn.onClick = [this]() { audioProcessor.loadPatch(patchList.getText()); };
    addAndMakeVisible(loadBtn);

    deleteBtn.setButtonText("Del");
    deleteBtn.onClick = [this]() { audioProcessor.deletePatch(patchList.getText()); refreshPatchList(); };
    addAndMakeVisible(deleteBtn);

    addAndMakeVisible(patchList);
    refreshPatchList();

    groqApiKey = loadApiKey();
    startTimerHz(30);
}

KeroMixAIAudioProcessorEditor::~KeroMixAIAudioProcessorEditor() { stopTimer(); stopThread(2000); }

// ── 필수 가상 함수 (링커 에러 방지용 전체 구현) ──────────────────────────────────
void KeroMixAIAudioProcessorEditor::paint(juce::Graphics& g)
{
    g.fillAll(kBg);
    auto card = getLocalBounds().reduced(10).toFloat();
    g.setColour(kCard); g.fillRoundedRectangle(card, 15.f);
    g.setColour(kBorder); g.drawRoundedRectangle(card, 15.f, 1.5f);
    drawKeropi(g, 25, 15, 0.7f);
}

void KeroMixAIAudioProcessorEditor::resized()
{
    auto area = getLocalBounds().reduced(20);
    auto rightSide = area.removeFromRight(300);

    // 퀵 버튼 8개 배치
    auto btnArea = rightSide.removeFromTop(80);
    int bW = btnArea.getWidth() / 4, bH = 35;
    for (int i = 0; i < NUM_QUICK; ++i)
        quickBtns[i].setBounds(btnArea.getX() + (i % 4) * bW, btnArea.getY() + (i / 4) * bH, bW - 4, bH - 4);

    // AI 입력
    auto aiArea = rightSide.removeFromTop(100);
    promptInput.setBounds(aiArea.removeFromTop(35).reduced(2));
    sendBtn.setBounds(aiArea.removeFromTop(35).removeFromRight(80).reduced(2));
    undoBtn.setBounds(aiArea.removeFromLeft(60).reduced(2));
    statusLabel.setBounds(aiArea.reduced(2));

    // 패치 시스템
    auto patchArea = rightSide.removeFromTop(50);
    patchNameInput.setBounds(patchArea.removeFromLeft(100).reduced(2));
    saveBtn.setBounds(patchArea.removeFromLeft(50).reduced(2));
    patchList.setBounds(patchArea.removeFromLeft(80).reduced(2));
    loadBtn.setBounds(patchArea.removeFromLeft(50).reduced(2));

    settingsBtn.setBounds(getWidth() - 40, 15, 25, 25);

    // 슬라이더 배치는 공간에 맞춰 자동 루프 (기본 구현)
    auto sliderArea = area.removeFromLeft(getWidth() - 350);
    int cols = 5, rows = 4;
    int sW = sliderArea.getWidth() / cols, sH = sliderArea.getHeight() / rows;
    for (int i = 0; i < NUM_PARAMS - 1; ++i) {
        int r = i / cols, c = i % cols;
        sliders[i].setBounds(sliderArea.getX() + c * sW, sliderArea.getY() + r * sH, sW - 10, sH - 25);
        labels[i].setBounds(sliders[i].getX(), sliders[i].getBottom(), sW - 10, 20);
    }
}

void KeroMixAIAudioProcessorEditor::timerCallback() { /* 스펙트럼 로직 필요시 추가 */ }
void KeroMixAIAudioProcessorEditor::mouseDown(const juce::MouseEvent& e) { hideSettings(); }

// ── AI & Logic ────────────────────────────────────────────────────────────────
void KeroMixAIAudioProcessorEditor::saveSnapshot() {
    for (int i = 0; i < NUM_PARAMS; ++i)
        undoSnapshot[i] = (float)*audioProcessor.apvts.getRawParameterValue(paramIDs[i]);
    hasUndoSnapshot = true; undoBtn.setEnabled(true);
}

void KeroMixAIAudioProcessorEditor::restoreSnapshot() {
    if (!hasUndoSnapshot) return;
    for (int i = 0; i < NUM_PARAMS; ++i) {
        if (auto* p = audioProcessor.apvts.getParameter(paramIDs[i])) {
            auto range = audioProcessor.apvts.getParameterRange(paramIDs[i]);
            p->setValueNotifyingHost(range.convertTo0to1(undoSnapshot[i]));
        }
    }
    undoBtn.setEnabled(false);
}

void KeroMixAIAudioProcessorEditor::sendToGroq(const juce::String& prompt)
{
    if (prompt.trim().isEmpty() || groqApiKey.isEmpty()) return;
    saveSnapshot();
    juce::String currentParams = "{";
    for (int i = 0; i < NUM_PARAMS; ++i) {
        currentParams << "\"" << paramIDs[i] << "\":" << (float)*audioProcessor.apvts.getRawParameterValue(paramIDs[i]) << (i == NUM_PARAMS - 1 ? "" : ",");
    }
    currentParams << "}";
    { juce::ScopedLock sl(threadLock); pendingPrompt = prompt + "|||" + currentParams; }
    statusLabel.setText("Thinking...", juce::dontSendNotification);
    startThread();
}

void KeroMixAIAudioProcessorEditor::run()
{
    juce::String full; { juce::ScopedLock sl(threadLock); full = pendingPrompt; }
    auto parts = juce::StringArray::fromTokens(full, "|||", "");
    if (parts.size() < 2) return;

    juce::String sys = "Audio Expert. Rules: Default change 15-20%. Slightly/Low 5-8%. Much/High 40-60%. ONLY JSON.";
    juce::URL url("https://api.groq.com/openai/v1/chat/completions");
    auto body = "{\"model\":\"llama-3.1-8b-instant\",\"messages\":[{\"role\":\"system\",\"content\":\"" + sys + "\"},{\"role\":\"user\",\"content\":\"Current:" + parts[1].replace("\"", "\\\"") + " Req:" + parts[0].replace("\"", "\\\"") + "\"}],\"temperature\":0.1}";

    auto ws = url.withPOSTData(body).createInputStream(juce::URL::InputStreamOptions(juce::URL::ParameterHandling::inPostData)
        .withExtraHeaders("Authorization: Bearer " + groqApiKey + "\r\nContent-Type: application/json").withConnectionTimeoutMs(10000));

    if (ws) {
        juce::String resp = ws->readEntireStreamAsString();
        int ci = resp.indexOf("\"content\":");
        if (ci >= 0) {
            int start = resp.indexOf(ci, "{");
            int end = resp.lastIndexOf("}");
            if (start >= 0 && end > start) {
                juce::String pj = resp.substring(start, end + 1).replace("\\\"", "\"").replace("\\n", " ");
                juce::MessageManager::callAsync([this, pj]() { applyParamsFromJson(pj); });
                return;
            }
        }
    }
    juce::MessageManager::callAsync([this]() { statusLabel.setText("Error.", juce::dontSendNotification); });
}

void KeroMixAIAudioProcessorEditor::applyParamsFromJson(const juce::String& json)
{
    int count = 0;
    for (int i = 0; i < NUM_PARAMS; ++i) {
        if (locked[PARAM_GROUP[i]]) continue;
        juce::String search = "\"" + paramIDs[i] + "\":";
        int pos = json.indexOf(search);
        if (pos < 0) continue;
        pos += search.length();
        while (pos < json.length() && !juce::CharacterFunctions::isDigit(json[pos]) && json[pos] != '-') ++pos;
        int end = pos;
        while (end < json.length() && (juce::CharacterFunctions::isDigit(json[end]) || json[end] == '.' || json[end] == '-')) ++end;
        if (end > pos) {
            float v = json.substring(pos, end).getFloatValue();
            if (auto* param = audioProcessor.apvts.getParameter(paramIDs[i])) {
                auto range = audioProcessor.apvts.getParameterRange(paramIDs[i]);
                param->setValueNotifyingHost(range.convertTo0to1(juce::jlimit(range.start, range.end, v)));
                count++;
            }
        }
    }
    statusLabel.setText("Applied " + juce::String(count) + " changes.", juce::dontSendNotification);
}

// ── Helpers & Settings ────────────────────────────────────────────────────────
void KeroMixAIAudioProcessorEditor::showSettings() {
    settingsPanel = std::make_unique<SettingsComponent>();
    settingsPanel->onKeyEntered = [this](const juce::String& k) { saveApiKey(k); groqApiKey = k; hideSettings(); };
    settingsPanel->onClose = [this]() { hideSettings(); };
    addAndMakeVisible(*settingsPanel);
    settingsPanel->setBounds(getLocalBounds().reduced(100, 150));
}

void KeroMixAIAudioProcessorEditor::hideSettings() { settingsPanel.reset(); }

void KeroMixAIAudioProcessorEditor::refreshPatchList() {
    patchList.clear(); auto names = audioProcessor.getSavedPatchNames();
    for (int i = 0; i < names.size(); ++i) patchList.addItem(names[i], i + 1);
}

void KeroMixAIAudioProcessorEditor::saveApiKey(const juce::String& k) {
    auto f = juce::File::getSpecialLocation(juce::File::userApplicationDataDirectory).getChildFile("KeroMixAI/config.txt");
    f.createDirectory(); f.replaceWithText(k);
}

juce::String KeroMixAIAudioProcessorEditor::loadApiKey() {
    auto f = juce::File::getSpecialLocation(juce::File::userApplicationDataDirectory).getChildFile("KeroMixAI/config.txt");
    return f.existsAsFile() ? f.loadFileAsString().trim() : "";
}

void KeroMixAIAudioProcessorEditor::drawKeropi(juce::Graphics& g, float x, float y, float sc) {
    g.setColour(kKero); g.fillEllipse(x, y + 13 * sc, 55 * sc, 45 * sc);
    g.setColour(juce::Colours::black); g.drawEllipse(x, y + 13 * sc, 55 * sc, 45 * sc, 2 * sc);
    g.setColour(juce::Colours::white);
    g.fillEllipse(x + 5 * sc, y, 22 * sc, 22 * sc); g.fillEllipse(x + 28 * sc, y, 22 * sc, 22 * sc);
    g.setColour(juce::Colours::black);
    g.fillEllipse(x + 12 * sc, y + 5 * sc, 8 * sc, 8 * sc); g.fillEllipse(x + 35 * sc, y + 5 * sc, 8 * sc, 8 * sc);
}