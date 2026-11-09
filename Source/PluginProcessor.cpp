#include "PluginProcessor.h"
#include "Parameters.h"
#include "PluginEditor.h"

LyraTuneProcessor::LyraTuneProcessor()
    : AudioProcessor (BusesProperties()
                          .withInput ("Input", juce::AudioChannelSet::stereo(), true)
                          .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, &undoManager, "STATE", createParameterLayout())
{
    bypassParam = apvts.getParameter (ParamID::bypass);
    pBypass = apvts.getRawParameterValue (ParamID::bypass);
    pSpeed = apvts.getRawParameterValue (ParamID::speed);
    pAmount = apvts.getRawParameterValue (ParamID::amount);
    pRoot = apvts.getRawParameterValue (ParamID::root);
    pScale = apvts.getRawParameterValue (ParamID::scale);
    pRef = apvts.getRawParameterValue (ParamID::referenceHz);
    pRange = apvts.getRawParameterValue (ParamID::range);
    pStabilizer = apvts.getRawParameterValue (ParamID::stabilizer);
    pSustain = apvts.getRawParameterValue (ParamID::sustain);
    pHq = apvts.getRawParameterValue (ParamID::hq);
    pDoubler = apvts.getRawParameterValue (ParamID::doubler);
    pDblWidth = apvts.getRawParameterValue (ParamID::doublerWidth);
    pDblBlend = apvts.getRawParameterValue (ParamID::doublerBlend);
    pStrike = apvts.getRawParameterValue (ParamID::strike);
    pStrikeAmount = apvts.getRawParameterValue (ParamID::strikeAmount);
    pStrikeLength = apvts.getRawParameterValue (ParamID::strikeLength);
    for (int pc = 0; pc < 12; ++pc)
        pNotes[pc] = apvts.getRawParameterValue (ParamID::note (pc));
    chain.setTelemetryQueue (&telemetry);
    startTimerHz (10);
}

LyraTuneProcessor::~LyraTuneProcessor() { stopTimer(); }

void LyraTuneProcessor::prepareToPlay (double sr, int)
{
    // Allocate for the lowest range so switching Range never allocates on the audio thread.
    chain.prepare (sr, kRangeMinHz[0]);
    setLatencySamples (lyra::TunerChain::latencyFor (sr, rangeMinHz (pRange->load()), pHq->load() > 0.5f));
    preparedRate = sr;
}

void LyraTuneProcessor::timerCallback()
{
    // shortcut: latency is re-reported up to 100 ms after a Range/HQ change; DSP switches immediately.
    if (const double sr = preparedRate.load(); sr > 0)
        if (const int lat = lyra::TunerChain::latencyFor (sr, rangeMinHz (pRange->load()), pHq->load() > 0.5f); lat != getLatencySamples())
            setLatencySamples (lat);
}

bool LyraTuneProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto out = layouts.getMainOutputChannelSet();
    return (out == juce::AudioChannelSet::mono() || out == juce::AudioChannelSet::stereo())
        && layouts.getMainInputChannelSet() == out;
}

void LyraTuneProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&) { process (buffer, false); }
void LyraTuneProcessor::processBlockBypassed (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&) { process (buffer, true); }

void LyraTuneProcessor::process (juce::AudioBuffer<float>& buffer, bool forceBypass) noexcept
{
    juce::ScopedNoDenormals noDenormals;
    const int numCh = std::min (lyra::TunerChain::kMaxChannels, getTotalNumInputChannels());
    for (auto ch = numCh; ch < buffer.getNumChannels(); ++ch)
        buffer.clear (ch, 0, buffer.getNumSamples());

    lyra::TunerChain::Settings s;
    s.correction.speedMs = pSpeed->load();
    s.correction.amount = pAmount->load() / 100.0;
    s.correction.referenceHz = pRef->load();
    s.correction.mask = currentMask();
    s.correction.stabilizerMs = stabilizerMs (pStabilizer->load());
    s.correction.sustain = pSustain->load() / 100.0;
    s.minFreq = rangeMinHz (pRange->load());
    s.hq = pHq->load() > 0.5f;
    s.doubler.enabled = pDoubler->load() > 0.5f;
    s.doubler.width = pDblWidth->load() / 100.0f;
    s.doubler.blend = pDblBlend->load() / 100.0f;
    s.strike = pStrike->load() > 0.5f;
    s.strikeDepth = pStrikeAmount->load() / 100.0;
    s.strikeLength = pStrikeLength->load() / 1000.0;
    s.bypass = forceBypass || pBypass->load() > 0.5f;
    chain.setSettings (s);
    chain.process (buffer.getArrayOfWritePointers(), numCh, buffer.getNumSamples());
}

lyra::PitchClassMask LyraTuneProcessor::currentMask() const noexcept
{
    const auto type = lyra::ScaleType (juce::jlimit (0, 3, int (pScale->load())));
    lyra::PitchClassMask custom = 0;
    for (int pc = 0; pc < 12; ++pc)
        custom |= lyra::PitchClassMask ((pNotes[pc]->load() > 0.5f ? 1u : 0u) << pc);
    return lyra::scaleMask (type, juce::jlimit (0, 11, int (pRoot->load())), custom);
}

double LyraTuneProcessor::latencyMsFor (int rangeIndex, bool hq) const noexcept
{
    const double sr = preparedRate.load() > 0 ? preparedRate.load() : 48000.0;
    return 1000.0 * lyra::TunerChain::latencyFor (sr, rangeMinHz (float (rangeIndex)), hq) / sr;
}

void LyraTuneProcessor::resetAllParameters()
{
    undoManager.beginNewTransaction ("Reset All");
    for (auto* p : getParameters())
        if (auto* rp = dynamic_cast<juce::RangedAudioParameter*> (p); rp != nullptr && p != bypassParam)
        {
            rp->beginChangeGesture();
            rp->setValueNotifyingHost (rp->getDefaultValue());
            rp->endChangeGesture();
        }
}

void LyraTuneProcessor::switchAB()
{
    abSnapshot[abSlot] = apvts.copyState();
    abSlot ^= 1;
    if (abSnapshot[abSlot].isValid())
        apvts.replaceState (abSnapshot[abSlot].createCopy());
}

void LyraTuneProcessor::copyToOtherAB()
{
    abSnapshot[abSlot ^ 1] = apvts.copyState();
}


// 0.6.0 saved LyraStrike without Amount/Length (it was fixed at 4 % / 25 ms). Missing parameters load
// at their defaults, which are now 7.5 % / 80 ms, so fill in the old values to keep those projects' sound.
static juce::ValueTree migrateState (juce::ValueTree t)
{
    if (t.getChildWithProperty ("id", ParamID::strike).isValid() && ! t.getChildWithProperty ("id", ParamID::strikeAmount).isValid())
    {
        t.appendChild (juce::ValueTree ("PARAM", { { "id", ParamID::strikeAmount }, { "value", 4.0 } }), nullptr);
        t.appendChild (juce::ValueTree ("PARAM", { { "id", ParamID::strikeLength }, { "value", 25.0 } }), nullptr);
    }
    return t;
}

// State format v1: <LYRA version="1"><STATE .../><AB slot=".."><A/><B/></AB><UI .../></LYRA>.
// Also accepts the pre-v1 format (a bare <STATE>). Unknown/missing pieces fall back to defaults.
void LyraTuneProcessor::getStateInformation (juce::MemoryBlock& dest)
{
    juce::XmlElement root ("LYRA");
    root.setAttribute ("version", 1);
    if (auto st = apvts.copyState().createXml())
        root.addChildElement (st.release());

    auto* ab = root.createNewChildElement ("AB");
    ab->setAttribute ("slot", abSlot);
    for (int i = 0; i < 2; ++i)
        if (abSnapshot[i].isValid())
            if (auto x = abSnapshot[i].createXml())
                ab->createNewChildElement (i == 0 ? "A" : "B")->addChildElement (x.release());

    auto* u = root.createNewChildElement ("UI");
    u->setAttribute ("heatmaps", ui.heatmaps);
    u->setAttribute ("detected", ui.detectedNotes);
    u->setAttribute ("tooltips", ui.tooltips);
    u->setAttribute ("scale", ui.scale);
    copyXmlToBinary (root, dest);
}

void LyraTuneProcessor::setStateInformation (const void* data, int size)
{
    const auto xml = getXmlFromBinary (data, size);
    if (xml == nullptr)
        return;
    const auto* st = xml->hasTagName ("LYRA") ? xml->getChildByName (apvts.state.getType()) : xml.get();
    if (st != nullptr && st->hasTagName (apvts.state.getType()))
        apvts.replaceState (migrateState (juce::ValueTree::fromXml (*st)));

    if (const auto* ab = xml->getChildByName ("AB"))
    {
        abSlot = juce::jlimit (0, 1, ab->getIntAttribute ("slot"));
        for (int i = 0; i < 2; ++i)
        {
            abSnapshot[i] = {};
            if (const auto* holder = ab->getChildByName (i == 0 ? "A" : "B"))
                if (const auto* snap = holder->getChildByName (apvts.state.getType()))
                    abSnapshot[i] = migrateState (juce::ValueTree::fromXml (*snap));
        }
    }
    if (const auto* u = xml->getChildByName ("UI"))
    {
        ui.heatmaps = u->getBoolAttribute ("heatmaps", true);
        ui.detectedNotes = u->getBoolAttribute ("detected", true);
        ui.tooltips = u->getBoolAttribute ("tooltips", true);
        ui.scale = juce::jlimit (0.4f, 1.0f, float (u->getDoubleAttribute ("scale", 0.7)));
    }
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new LyraTuneProcessor(); }

juce::AudioProcessorEditor* LyraTuneProcessor::createEditor() { return new LyraTuneEditor (*this); }
