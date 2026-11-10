#pragma once
#include "PluginProcessor.h"
#include "UI/DesignLayout.h"

// Luna's own interface: a flat 980 x 980 picture with a Major/Minor switch, one octave of keys
// (pick the key of the song) and the sparkles, which fill up like a meter and set the retune speed.
// Everything is drawn from PNG sprites (Resources/ui), so the look can be changed by swapping the pictures.
//
// - Click a key            -> sets the key (root note). From chromatic it also switches to Major.
// - Click Major / Minor    -> sets the scale. Clicking the selected one again = chromatic (all notes).
// - Drag on the sparkles   -> fills them from the bottom: full = 0 ms (hard tune), empty = 400 ms (very soft).
//                             Double-click = full again. Mouse wheel also works.
// - Double-click the picture, or right-click anywhere -> change the picture (stored on this computer).
class LyraTuneEditor : public juce::AudioProcessorEditor, public juce::TooltipClient, private juce::Timer
{
public:
    static constexpr int kSize = design::kSize;

    explicit LyraTuneEditor (LyraTuneProcessor&);
    ~LyraTuneEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;
    void mouseMove (const juce::MouseEvent&) override;
    void mouseExit (const juce::MouseEvent&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    void mouseDoubleClick (const juce::MouseEvent&) override;
    void mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails&) override;
    juce::String getTooltip() override;

private:
    struct Hit
    {
        enum Kind { none, scale, key, level } kind = none;
        int index = 0; // scale: 0 Major / 1 Minor; key: pitch class 0..11
        bool operator== (const Hit& o) const noexcept { return kind == o.kind && index == o.index; }
    };

    void timerCallback() override;
    Hit hitAt (juce::Point<float> canvasPos) const;
    juce::Point<float> toCanvas (juce::Point<int> p) const;
    void setParam (const juce::String& id, float value);
    int currentScale() const;
    int currentRoot() const;
    float speedMs() const;
    float level() const;                // sparkle fill 0..1 (1 = full = 0 ms)
    void setLevel (float level01);      // moves the speed parameter (inside a gesture, if one is open)
    float levelAtCanvasY (float y) const;
    void showImageMenu();
    void chooseImage();
    void useImageFile (const juce::File&);
    void resetImage();
    int stateKey() const;

    LyraTuneProcessor& proc;
    juce::AudioProcessorValueTreeState& state;

    juce::Image baseImage, customImage;
    juce::Image scaleOff, scaleOn, keysOff, keysOn, sparkleOff, sparkleOn;

    Hit hover;
    juce::RangedAudioParameter* draggedParam = nullptr; // set while the sparkles are being dragged
    int lastStateKey = -1;
    std::unique_ptr<juce::FileChooser> chooser;
    std::unique_ptr<juce::TooltipWindow> tooltipWindow;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (LyraTuneEditor)
};
