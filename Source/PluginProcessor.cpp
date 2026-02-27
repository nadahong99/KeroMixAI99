#include "PluginProcessor.h"
#include "PluginEditor.h"

KeroMixAIAudioProcessor::KeroMixAIAudioProcessor()
    : AudioProcessor(BusesProperties()
        .withInput("Input", juce::AudioChannelSet::stereo(), true)
        .withOutput("Output", juce::AudioChannelSet::stereo(), true)),
    apvts(*this, nullptr, "Parameters", createParameterLayout())
{
}

KeroMixAIAudioProcessor::~KeroMixAIAudioProcessor() {}

bool KeroMixAIAudioProcessor::isBusesLayoutSupported(const BusesLayout& layouts) const
{
    if (layouts.getMainOutputChannelSet() != juce::AudioChannelSet::stereo()) return false;
    return layouts.getMainOutputChannelSet() == layouts.getMainInputChannelSet();
}

juce::AudioProcessorValueTreeState::ParameterLayout KeroMixAIAudioProcessor::createParameterLayout()
{
    std::vector<std::unique_ptr<juce::RangedAudioParameter>> p;

    // EQ - gain + Q per band
    p.push_back(std::make_unique<juce::AudioParameterFloat>("lowG", "Low Gain", -18.f, 18.f, 0.f));
    p.push_back(std::make_unique<juce::AudioParameterFloat>("lowFreq", "Low Freq", 60.f, 600.f, 200.f));
    p.push_back(std::make_unique<juce::AudioParameterFloat>("midG", "Mid Gain", -18.f, 18.f, 0.f));
    p.push_back(std::make_unique<juce::AudioParameterFloat>("midFreq", "Mid Freq", 300.f, 5000.f, 1000.f));
    p.push_back(std::make_unique<juce::AudioParameterFloat>("midQ", "Mid Q", 0.3f, 4.0f, 0.8f));
    p.push_back(std::make_unique<juce::AudioParameterFloat>("highG", "High Gain", -18.f, 18.f, 0.f));
    p.push_back(std::make_unique<juce::AudioParameterFloat>("highFreq", "High Freq", 3000.f, 16000.f, 8000.f));

    // Compressor
    p.push_back(std::make_unique<juce::AudioParameterFloat>("compThresh", "Threshold", -40.f, 0.f, -12.f));
    p.push_back(std::make_unique<juce::AudioParameterFloat>("compRatio", "Ratio", 1.f, 20.f, 4.f));
    p.push_back(std::make_unique<juce::AudioParameterFloat>("compAttack", "Attack", 1.f, 100.f, 10.f));
    p.push_back(std::make_unique<juce::AudioParameterFloat>("compRelease", "Release", 20.f, 500.f, 100.f));
    p.push_back(std::make_unique<juce::AudioParameterFloat>("compMakeup", "Makeup", 0.f, 24.f, 0.f));

    // Delay
    p.push_back(std::make_unique<juce::AudioParameterFloat>("delayTime", "Dly Time", 0.05f, 1.0f, 0.4f));
    p.push_back(std::make_unique<juce::AudioParameterFloat>("delayFeedback", "Dly Feedback", 0.0f, 0.9f, 0.3f));
    p.push_back(std::make_unique<juce::AudioParameterFloat>("delayMix", "Dly Mix", 0.0f, 1.0f, 0.0f));

    // Reverb
    p.push_back(std::make_unique<juce::AudioParameterFloat>("revDecay", "Rev Decay", 0.f, 1.f, 0.5f));
    p.push_back(std::make_unique<juce::AudioParameterFloat>("revSize", "Rev Size", 0.f, 1.f, 0.5f));
    p.push_back(std::make_unique<juce::AudioParameterFloat>("revDamp", "Rev Damp", 0.f, 1.f, 0.3f));
    p.push_back(std::make_unique<juce::AudioParameterFloat>("revMix", "Rev Mix", 0.f, 1.f, 0.0f));

    // Master
    p.push_back(std::make_unique<juce::AudioParameterFloat>("aimix", "Output", 0.f, 1.f, 0.8f));

    return { p.begin(), p.end() };
}

void KeroMixAIAudioProcessor::prepareToPlay(double sampleRate, int samplesPerBlock)
{
    for (int ch = 0; ch < 2; ++ch)
    {
        for (int b = 0; b < 3; ++b) eqFilters[ch][b].reset();
        compEnv[ch] = 0.f;
        compGainDb[ch] = 0.f;
    }
    reverbEngine.setSampleRate(sampleRate);
    delayBuffer.setSize(2, (int)(sampleRate * 2.1));
    delayBuffer.clear();
    writePos = 0;

    juce::ScopedLock sl(fftLock);
    juce::zeromem(fftFifo, sizeof(fftFifo));
    fftFifoIndex = 0;
    fftDataReady = false;
}

void KeroMixAIAudioProcessor::releaseResources() {}

void KeroMixAIAudioProcessor::processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;
    const int totalIn = getTotalNumInputChannels();
    const int numSamp = buffer.getNumSamples();
    const double sr = getSampleRate();
    if (sr <= 0.0) return;

    // ── Bypass ────────────────────────────────────────────────────────────
    if (bypassed.load()) return;

    // ── EQ (parametric 3-band) ────────────────────────────────────────────
    {
        float lG = juce::Decibels::decibelsToGain((float)*apvts.getRawParameterValue("lowG"));
        float lF = (float)*apvts.getRawParameterValue("lowFreq");
        float mG = juce::Decibels::decibelsToGain((float)*apvts.getRawParameterValue("midG"));
        float mF = (float)*apvts.getRawParameterValue("midFreq");
        float mQ = (float)*apvts.getRawParameterValue("midQ");
        float hG = juce::Decibels::decibelsToGain((float)*apvts.getRawParameterValue("highG"));
        float hF = (float)*apvts.getRawParameterValue("highFreq");

        for (int ch = 0; ch < juce::jmin(totalIn, 2); ++ch)
        {
            eqFilters[ch][0].setCoefficients(juce::IIRCoefficients::makeLowShelf(sr, lF, 0.71, lG));
            eqFilters[ch][1].setCoefficients(juce::IIRCoefficients::makePeakFilter(sr, mF, mQ, mG));
            eqFilters[ch][2].setCoefficients(juce::IIRCoefficients::makeHighShelf(sr, hF, 0.71, hG));
            for (int b = 0; b < 3; ++b)
                eqFilters[ch][b].processSamples(buffer.getWritePointer(ch), numSamp);
        }
    }

    // ── Compressor (feed-forward, per-channel, smooth gain) ───────────────
    {
        float threshDb = (float)*apvts.getRawParameterValue("compThresh");
        float ratio = (float)*apvts.getRawParameterValue("compRatio");
        float attackMs = (float)*apvts.getRawParameterValue("compAttack");
        float releaseMs = (float)*apvts.getRawParameterValue("compRelease");
        float makeupDb = (float)*apvts.getRawParameterValue("compMakeup");

        // time constants
        float attackCoef = std::exp(-1.f / (float)(sr * attackMs * 0.001));
        float releaseCoef = std::exp(-1.f / (float)(sr * releaseMs * 0.001));
        float makeup = juce::Decibels::decibelsToGain(makeupDb);

        for (int ch = 0; ch < juce::jmin(totalIn, 2); ++ch)
        {
            auto* data = buffer.getWritePointer(ch);
            for (int i = 0; i < numSamp; ++i)
            {
                float inDb = juce::Decibels::gainToDecibels(std::abs(data[i]) + 1e-9f);

                // gain computer
                float overDb = inDb - threshDb;
                float targetGainDb = overDb > 0.f ? -(overDb * (1.f - 1.f / ratio)) : 0.f;

                // smooth
                if (targetGainDb < compGainDb[ch])
                    compGainDb[ch] = attackCoef * compGainDb[ch] + (1.f - attackCoef) * targetGainDb;
                else
                    compGainDb[ch] = releaseCoef * compGainDb[ch] + (1.f - releaseCoef) * targetGainDb;

                data[i] *= juce::Decibels::decibelsToGain(compGainDb[ch]) * makeup;
            }
        }
    }

    // ── Delay (stereo, feedback) ──────────────────────────────────────────
    {
        float dMix = (float)*apvts.getRawParameterValue("delayMix");
        if (dMix > 0.001f)
        {
            float dT = (float)*apvts.getRawParameterValue("delayTime");
            float dFb = (float)*apvts.getRawParameterValue("delayFeedback");
            int   dS = juce::jmin((int)(sr * dT), delayBuffer.getNumSamples() - 1);

            for (int ch = 0; ch < juce::jmin(totalIn, 2); ++ch)
            {
                auto* data = buffer.getWritePointer(ch);
                auto* dBuf = delayBuffer.getWritePointer(ch);
                int   lPos = writePos;

                for (int i = 0; i < numSamp; ++i)
                {
                    float dry = data[i];
                    int   rP = (lPos - dS + delayBuffer.getNumSamples()) % delayBuffer.getNumSamples();
                    float wet = dBuf[rP];
                    dBuf[lPos] = dry + wet * dFb;
                    data[i] = dry + wet * dMix;
                    lPos = (lPos + 1) % delayBuffer.getNumSamples();
                }
                if (ch == 0) writePos = lPos;
            }
        }
    }

    // ── Reverb ────────────────────────────────────────────────────────────
    {
        float rMix = (float)*apvts.getRawParameterValue("revMix");
        if (rMix > 0.001f && totalIn >= 2)
        {
            juce::Reverb::Parameters rp;
            rp.roomSize = juce::jlimit(0.f, 1.f,
                (float)*apvts.getRawParameterValue("revDecay") * 0.85f +
                (float)*apvts.getRawParameterValue("revSize") * 0.14f);
            rp.damping = (float)*apvts.getRawParameterValue("revDamp");
            rp.wetLevel = rMix;
            rp.dryLevel = 1.0f;
            rp.width = 1.0f;
            reverbEngine.setParameters(rp);
            reverbEngine.processStereo(buffer.getWritePointer(0), buffer.getWritePointer(1), numSamp);
        }
    }

    // ── Master output ─────────────────────────────────────────────────────
    buffer.applyGain((float)*apvts.getRawParameterValue("aimix"));

    // ── FFT fifo ──────────────────────────────────────────────────────────
    {
        juce::ScopedLock sl(fftLock);
        auto* L = buffer.getReadPointer(0);
        auto* R = totalIn > 1 ? buffer.getReadPointer(1) : L;
        for (int i = 0; i < numSamp; ++i)
        {
            fftFifo[fftFifoIndex++] = (L[i] + R[i]) * 0.5f;
            if (fftFifoIndex >= FFT_SIZE)
            {
                fftDataReady = true;
                fftFifoIndex = 0;
            }
        }
    }
}

// ── State / Patch ──────────────────────────────────────────────────────────
void KeroMixAIAudioProcessor::getStateInformation(juce::MemoryBlock& destData)
{
    auto state = apvts.copyState();
    std::unique_ptr<juce::XmlElement> xml(state.createXml());
    copyXmlToBinary(*xml, destData);
}

void KeroMixAIAudioProcessor::setStateInformation(const void* data, int sizeInBytes)
{
    std::unique_ptr<juce::XmlElement> xml(getXmlFromBinary(data, sizeInBytes));
    if (xml && xml->hasTagName(apvts.state.getType()))
        apvts.replaceState(juce::ValueTree::fromXml(*xml));
}

juce::File KeroMixAIAudioProcessor::getPatchDirectory()
{
    auto dir = juce::File::getSpecialLocation(juce::File::userApplicationDataDirectory)
        .getChildFile("KeroMixAI").getChildFile("Patches");
    dir.createDirectory();
    return dir;
}

void KeroMixAIAudioProcessor::savePatch(const juce::String& name)
{
    auto state = apvts.copyState();
    std::unique_ptr<juce::XmlElement> xml(state.createXml());
    xml->setAttribute("patchName", name);
    auto file = getPatchDirectory().getChildFile(name + ".xml");
    xml->writeTo(file);
}

juce::StringArray KeroMixAIAudioProcessor::getSavedPatchNames()
{
    juce::StringArray names;
    for (auto& f : getPatchDirectory().findChildFiles(juce::File::findFiles, false, "*.xml"))
        names.add(f.getFileNameWithoutExtension());
    names.sort(true);
    return names;
}

bool KeroMixAIAudioProcessor::loadPatch(const juce::String& name)
{
    auto file = getPatchDirectory().getChildFile(name + ".xml");
    if (!file.existsAsFile()) return false;
    auto xml = juce::XmlDocument::parse(file);
    if (!xml) return false;
    apvts.replaceState(juce::ValueTree::fromXml(*xml));
    return true;
}

bool KeroMixAIAudioProcessor::deletePatch(const juce::String& name)
{
    return getPatchDirectory().getChildFile(name + ".xml").deleteFile();
}

juce::AudioProcessorEditor* KeroMixAIAudioProcessor::createEditor()
{
    return new KeroMixAIAudioProcessorEditor(*this);
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new KeroMixAIAudioProcessor();
}
