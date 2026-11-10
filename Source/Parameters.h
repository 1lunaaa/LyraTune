#pragma once
#include <juce_audio_processors/juce_audio_processors.h>

// Parameter IDs are part of saved projects/automation: never rename or reuse them.
namespace ParamID
{
inline constexpr auto bypass = "bypass";
inline constexpr auto speed = "speed";
inline constexpr auto amount = "amount";
inline constexpr auto root = "root";
inline constexpr auto scale = "scale";
inline constexpr auto referenceHz = "refHz";
inline constexpr auto range = "range";
inline constexpr auto stabilizer = "stabilizer";
inline constexpr auto sustain = "sustain";
inline constexpr auto hq = "hq";
inline constexpr auto doubler = "doubler";
inline constexpr auto doublerWidth = "dblWidth";
inline constexpr auto doublerBlend = "dblBlend";
inline constexpr auto strike = "strike"; // LyraStrike
inline constexpr auto strikeAmount = "strikeAmt";
inline constexpr auto strikeLength = "strikeLen";
inline juce::String note (int pitchClass) { return "note" + juce::String (pitchClass); } // note0 (C) .. note11 (B)
}

inline const juce::StringArray kNoteNames { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };
// Index order must match lyra::ScaleType. "Custom" uses the twelve noteN parameters.
// (Pre-release change: Custom appended in 0.1; choice normalisation changed, so v=2 for `scale`.)
inline const juce::StringArray kScaleNames { "Chromatic", "Major", "Minor", "Custom" };

// Range = lowest pitch the tuner tracks. It sets the shifter's latency, so it is deliberately NOT
// automatable: hosts re-align plugin delay compensation only when latency changes outside playback.
inline const juce::StringArray kRangeNames { "Low (70 Hz)", "Mid (100 Hz)", "High (150 Hz)" };
inline constexpr double kRangeMinHz[] { 70.0, 100.0, 150.0 };
inline double rangeMinHz (float index) { return kRangeMinHz[juce::jlimit (0, 2, int (index))]; }

// Note Stabilizer: confirmation time before a new note is tuned (0 = off). Adds no latency.
inline const juce::StringArray kStabilizerNames { "None", "Short", "Mid", "Long" };
inline constexpr double kStabilizerMs[] { 0.0, 40.0, 80.0, 200.0 };
inline double stabilizerMs (float index) { return kStabilizerMs[juce::jlimit (0, 3, int (index))]; }

inline juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout()
{
    using namespace juce;
    AudioProcessorValueTreeState::ParameterLayout layout;
    constexpr int v = 1; // version hint for parameters introduced in this release

    layout.add (std::make_unique<AudioParameterBool> (ParameterID { ParamID::bypass, v }, "Bypass", false));

    // -3..0 ms = aggressive mode (see CorrectionEngine); skewed so the musically busy 0-60 ms range gets most travel.
    NormalisableRange<float> speedRange (-3.0f, 400.0f, 0.1f);
    speedRange.setSkewForCentre (40.0f);
    layout.add (std::make_unique<AudioParameterFloat> (ParameterID { ParamID::speed, v }, "Speed", speedRange, 0.0f,
        AudioParameterFloatAttributes().withLabel ("ms").withStringFromValueFunction (
            [] (float x, int) { return String (x, x < 10.0f ? 1 : 0) + " ms"; })));

    layout.add (std::make_unique<AudioParameterFloat> (ParameterID { ParamID::amount, v }, "Amount",
        NormalisableRange<float> (0.0f, 100.0f, 0.1f), 100.0f,
        AudioParameterFloatAttributes().withLabel ("%").withStringFromValueFunction (
            [] (float x, int) { return String (x, 0) + " %"; })));

    layout.add (std::make_unique<AudioParameterChoice> (ParameterID { ParamID::root, v }, "Root", kNoteNames, 0));
    layout.add (std::make_unique<AudioParameterChoice> (ParameterID { ParamID::scale, 2 }, "Scale", kScaleNames, 0));
    for (int pc = 0; pc < 12; ++pc)
        layout.add (std::make_unique<AudioParameterBool> (ParameterID { ParamID::note (pc), 2 },
                                                          "Note " + kNoteNames[pc], true));

    layout.add (std::make_unique<AudioParameterFloat> (ParameterID { ParamID::referenceHz, v }, "Reference",
        NormalisableRange<float> (420.0f, 460.0f, 0.1f), 440.0f,
        AudioParameterFloatAttributes().withLabel ("Hz").withStringFromValueFunction (
            [] (float x, int) { return String (x, 1) + " Hz"; })));

    layout.add (std::make_unique<AudioParameterChoice> (ParameterID { ParamID::stabilizer, 3 }, "Note Stabilizer", kStabilizerNames, 0));
    layout.add (std::make_unique<AudioParameterFloat> (ParameterID { ParamID::sustain, 3 }, "Sustain",
        NormalisableRange<float> (-100.0f, 100.0f, 1.0f), 0.0f,
        AudioParameterFloatAttributes().withStringFromValueFunction (
            [] (float x, int) { return (x > 0.5f ? "+" : "") + String (roundToInt (x)); })));

    // HQ changes latency (+2 ms lookahead): not automatable, same reasoning as Range.
    layout.add (std::make_unique<AudioParameterBool> (ParameterID { ParamID::hq, 4 }, "HQ Mode", false,
        AudioParameterBoolAttributes().withAutomatable (false)));
    layout.add (std::make_unique<AudioParameterBool> (ParameterID { ParamID::doubler, 4 }, "Doubler", false));
    auto pct = AudioParameterFloatAttributes().withLabel ("%").withStringFromValueFunction ([] (float x, int) { return String (roundToInt (x)) + " %"; });
    layout.add (std::make_unique<AudioParameterFloat> (ParameterID { ParamID::doublerWidth, 4 }, "Doubler Width", NormalisableRange<float> (0.0f, 100.0f, 1.0f), 70.0f, pct));
    layout.add (std::make_unique<AudioParameterFloat> (ParameterID { ParamID::doublerBlend, 4 }, "Doubler Blend", NormalisableRange<float> (0.0f, 100.0f, 1.0f), 50.0f, pct));

    // LyraStrike: formant accent on note changes. Adds no latency.
    layout.add (std::make_unique<AudioParameterBool> (ParameterID { ParamID::strike, 5 }, "LyraStrike", false));
    layout.add (std::make_unique<AudioParameterFloat> (ParameterID { ParamID::strikeAmount, 6 }, "LyraStrike Amount",
        NormalisableRange<float> (1.0f, 25.0f, 0.5f, 0.5f), 7.5f, // skewed: most travel in the subtle 1-8 % range
        AudioParameterFloatAttributes().withLabel ("%").withStringFromValueFunction (
            [] (float x, int) { return juce::String::fromUTF8 ("\xc2\xb1") + String (x, x < 10.0f && x != std::floor (x) ? 1 : 0) + " %"; })));
    layout.add (std::make_unique<AudioParameterFloat> (ParameterID { ParamID::strikeLength, 6 }, "LyraStrike Length",
        NormalisableRange<float> (25.0f, 100.0f, 1.0f), 80.0f,
        AudioParameterFloatAttributes().withLabel ("ms").withStringFromValueFunction (
            [] (float x, int) { return String (roundToInt (x)) + " ms"; })));

    layout.add (std::make_unique<AudioParameterChoice> (ParameterID { ParamID::range, v }, "Range", kRangeNames, 0,
        AudioParameterChoiceAttributes().withAutomatable (false)));
    return layout;
}
