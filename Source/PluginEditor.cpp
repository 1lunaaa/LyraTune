#include "PluginEditor.h"
#include "Parameters.h"
#include <BinaryData.h>

namespace
{
constexpr float kHoverAlpha = 0.35f;
constexpr int kMaxCustomSide = 1400; // custom pictures are shrunk to this (keeps the saved copy small)

juce::Rectangle<int> toRect (const design::Box& b) { return { b.x, b.y, b.w, b.h }; }

juce::Image loadSprite (const void* data, int size)
{
    return juce::ImageFileFormat::loadFrom (data, size_t (size));
}

juce::File customImageFile()
{
    return juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory)
        .getChildFile ("LyraTune")
        .getChildFile ("custom_image.png");
}
} // namespace

LyraTuneEditor::LyraTuneEditor (LyraTuneProcessor& p)
    : AudioProcessorEditor (p), proc (p), state (p.apvts)
{
    baseImage = loadSprite (BinaryData::ui_base_png, BinaryData::ui_base_pngSize);
    scaleOff = loadSprite (BinaryData::ui_scale_off_png, BinaryData::ui_scale_off_pngSize);
    scaleOn = loadSprite (BinaryData::ui_scale_on_png, BinaryData::ui_scale_on_pngSize);
    keysOff = loadSprite (BinaryData::ui_keys_off_png, BinaryData::ui_keys_off_pngSize);
    keysOn = loadSprite (BinaryData::ui_keys_on_png, BinaryData::ui_keys_on_pngSize);
    sparkleOff = loadSprite (BinaryData::ui_sparkle_off_png, BinaryData::ui_sparkle_off_pngSize);
    sparkleOn = loadSprite (BinaryData::ui_sparkle_on_png, BinaryData::ui_sparkle_on_pngSize);

    if (const auto f = customImageFile(); f.existsAsFile())
        customImage = juce::ImageFileFormat::loadFrom (f);

    tooltipWindow = std::make_unique<juce::TooltipWindow> (this, 600);

    setOpaque (true);
    // Capture the saved size first: setResizeLimits() clamps the still-0x0 editor to the minimum size,
    // and resized() would then overwrite ui.scale with that minimum.
    const float initialScale = proc.ui.scale;
    setResizable (true, true);
    getConstrainer()->setFixedAspectRatio (1.0);
    setResizeLimits (kSize * 2 / 5, kSize * 2 / 5, kSize, kSize);
    setSize (juce::roundToInt (kSize * initialScale), juce::roundToInt (kSize * initialScale));
    startTimerHz (20);
}

LyraTuneEditor::~LyraTuneEditor()
{
    stopTimer();
    tooltipWindow.reset();
}

// ---------------------------------------------------------------- state helpers

int LyraTuneEditor::currentScale() const { return juce::jlimit (0, 3, int (state.getRawParameterValue (ParamID::scale)->load())); }
int LyraTuneEditor::currentRoot() const { return juce::jlimit (0, 11, int (state.getRawParameterValue (ParamID::root)->load())); }
bool LyraTuneEditor::isActive() const { return state.getRawParameterValue (ParamID::bypass)->load() < 0.5f; }

int LyraTuneEditor::stateKey() const { return (currentRoot() << 8) | (currentScale() << 4) | (isActive() ? 1 : 0); }

void LyraTuneEditor::setParam (const juce::String& id, float value)
{
    if (auto* p = state.getParameter (id))
    {
        p->beginChangeGesture();
        p->setValueNotifyingHost (p->convertTo0to1 (value));
        p->endChangeGesture();
    }
}

void LyraTuneEditor::timerCallback()
{
    // Follows changes that come from the host (automation, presets, undo...).
    const int k = stateKey();
    if (k != lastStateKey)
    {
        lastStateKey = k;
        repaint();
    }
}

// ---------------------------------------------------------------- layout / hit testing

void LyraTuneEditor::resized()
{
    proc.ui.scale = float (getWidth()) / float (kSize);
}

juce::Point<float> LyraTuneEditor::toCanvas (juce::Point<int> p) const
{
    const float s = float (kSize) / float (juce::jmax (1, getWidth()));
    return { float (p.x) * s, float (p.y) * s };
}

LyraTuneEditor::Hit LyraTuneEditor::hitAt (juce::Point<float> p) const
{
    for (int i = 0; i < 2; ++i)
        if (toRect (design::scaleCell[i]).toFloat().expanded (3.0f).contains (p))
            return { Hit::scale, i };

    for (int pc = 0; pc < 12; ++pc)
        if (toRect (design::keyCell[pc]).toFloat().expanded (3.0f).contains (p))
            return { Hit::key, pc };

    // Sparkles: only where the filled shape actually is (a little forgiving around the edge).
    const auto& sp = design::sparkleSprite;
    const int x = juce::roundToInt (p.x) - sp.x, y = juce::roundToInt (p.y) - sp.y;
    if (sparkleOn.isValid() && x >= 0 && y >= 0 && x < sparkleOn.getWidth() && y < sparkleOn.getHeight())
    {
        for (int dy = -4; dy <= 4; dy += 4)
            for (int dx = -4; dx <= 4; dx += 4)
            {
                const int px = x + dx, py = y + dy;
                if (px >= 0 && py >= 0 && px < sparkleOn.getWidth() && py < sparkleOn.getHeight()
                    && sparkleOn.getPixelAt (px, py).getAlpha() > 100)
                    return { Hit::power, 0 };
            }
    }
    return {};
}

// ---------------------------------------------------------------- painting

void LyraTuneEditor::paint (juce::Graphics& g)
{
    g.fillAll (juce::Colour (14, 13, 15));
    g.setImageResamplingQuality (juce::Graphics::highResamplingQuality);
    g.addTransform (juce::AffineTransform::scale (float (getWidth()) / float (kSize)));
    g.reduceClipRegion (0, 0, kSize, kSize);

    // Picture: Miku by default, or the picture Luna chose (scaled to cover the whole canvas).
    if (customImage.isValid())
    {
        const float sc = juce::jmax (float (kSize) / float (customImage.getWidth()), float (kSize) / float (customImage.getHeight()));
        const float w = float (customImage.getWidth()) * sc, h = float (customImage.getHeight()) * sc;
        g.drawImageTransformed (customImage, juce::AffineTransform::scale (sc).translated ((float (kSize) - w) * 0.5f, (float (kSize) - h) * 0.5f));
    }
    else
    {
        g.drawImageAt (baseImage, 0, 0);
    }

    // Draws the "painted" version of a sprite, but only inside one cell.
    auto paintCell = [&] (const juce::Image& on, const design::Box& sprite, const design::Box& cell, float alpha) {
        juce::Graphics::ScopedSaveState ss (g);
        g.reduceClipRegion (toRect (cell));
        g.setOpacity (alpha);
        g.drawImageAt (on, sprite.x, sprite.y);
    };

    const int scale = currentScale(), root = currentRoot();
    const bool keyed = scale == int (lyra::ScaleType::Major) || scale == int (lyra::ScaleType::Minor);

    // Major | Minor
    g.drawImageAt (scaleOff, design::scaleSprite.x, design::scaleSprite.y);
    for (int i = 0; i < 2; ++i)
    {
        const bool selected = (i == 0 && scale == int (lyra::ScaleType::Major)) || (i == 1 && scale == int (lyra::ScaleType::Minor));
        if (selected)
            paintCell (scaleOn, design::scaleSprite, design::scaleCell[i], 1.0f);
        else if (hover.kind == Hit::scale && hover.index == i)
            paintCell (scaleOn, design::scaleSprite, design::scaleCell[i], kHoverAlpha);
    }

    // Keys
    g.drawImageAt (keysOff, design::keysSprite.x, design::keysSprite.y);
    for (int pc = 0; pc < 12; ++pc)
    {
        if (keyed && pc == root)
            paintCell (keysOn, design::keysSprite, design::keyCell[pc], 1.0f);
        else if (hover.kind == Hit::key && hover.index == pc)
            paintCell (keysOn, design::keysSprite, design::keyCell[pc], kHoverAlpha);
    }

    // Sparkles: filled = on, outline = off.
    if (isActive())
    {
        g.drawImageAt (sparkleOn, design::sparkleSprite.x, design::sparkleSprite.y);
    }
    else
    {
        g.drawImageAt (sparkleOff, design::sparkleSprite.x, design::sparkleSprite.y);
        if (hover.kind == Hit::power)
        {
            g.setOpacity (kHoverAlpha);
            g.drawImageAt (sparkleOn, design::sparkleSprite.x, design::sparkleSprite.y);
        }
    }
}

// ---------------------------------------------------------------- mouse

void LyraTuneEditor::mouseMove (const juce::MouseEvent& e)
{
    const auto h = hitAt (toCanvas (e.getPosition()));
    if (! (h == hover))
    {
        hover = h;
        setMouseCursor (h.kind == Hit::none ? juce::MouseCursor::NormalCursor : juce::MouseCursor::PointingHandCursor);
        repaint();
    }
}

void LyraTuneEditor::mouseExit (const juce::MouseEvent&)
{
    hover = {};
    repaint();
}

void LyraTuneEditor::mouseDown (const juce::MouseEvent& e)
{
    if (e.mods.isPopupMenu())
    {
        showImageMenu();
        return;
    }

    const auto h = hitAt (toCanvas (e.getPosition()));
    switch (h.kind)
    {
        case Hit::scale:
        {
            const int wanted = h.index == 0 ? int (lyra::ScaleType::Major) : int (lyra::ScaleType::Minor);
            // Clicking the selected scale again turns the key off: chromatic = every note allowed.
            setParam (ParamID::scale, currentScale() == wanted ? float (lyra::ScaleType::Chromatic) : float (wanted));
            break;
        }
        case Hit::key:
        {
            setParam (ParamID::root, float (h.index));
            const int sc = currentScale();
            if (sc != int (lyra::ScaleType::Major) && sc != int (lyra::ScaleType::Minor))
                setParam (ParamID::scale, float (lyra::ScaleType::Major));
            break;
        }
        case Hit::power:
            setParam (ParamID::bypass, isActive() ? 1.0f : 0.0f);
            break;
        case Hit::none:
            break;
    }
    lastStateKey = -1; // repaint on the next tick even if the host is slow to echo the change
    repaint();
}

void LyraTuneEditor::mouseDoubleClick (const juce::MouseEvent& e)
{
    if (hitAt (toCanvas (e.getPosition())).kind == Hit::none)
        chooseImage();
}

juce::String LyraTuneEditor::getTooltip()
{
    switch (hover.kind)
    {
        case Hit::scale:
            return hover.index == 0 ? "Major scale. Click again for chromatic (all notes)."
                                    : "Minor scale. Click again for chromatic (all notes).";
        case Hit::key:
            return "Key of the song: " + kNoteNames[hover.index];
        case Hit::power:
            return isActive() ? "Autotune ON. Click to turn it off." : "Autotune OFF. Click to turn it on.";
        case Hit::none:
            break;
    }
    return "Double-click: change the picture";
}

// ---------------------------------------------------------------- custom picture

void LyraTuneEditor::showImageMenu()
{
    juce::PopupMenu m;
    m.addItem (1, "Change picture...");
    m.addItem (2, "Back to Miku", customImage.isValid());
    m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (this).withParentComponent (this),
                     [sp = juce::Component::SafePointer<LyraTuneEditor> (this)] (int r) {
                         if (sp == nullptr) return;
                         if (r == 1) sp->chooseImage();
                         if (r == 2) sp->resetImage();
                     });
}

void LyraTuneEditor::chooseImage()
{
    chooser = std::make_unique<juce::FileChooser> ("Choose a picture",
                                                   juce::File::getSpecialLocation (juce::File::userPicturesDirectory),
                                                   "*.png;*.jpg;*.jpeg;*.gif;*.bmp");
    chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
                          [sp = juce::Component::SafePointer<LyraTuneEditor> (this)] (const juce::FileChooser& fc) {
                              if (sp == nullptr) return;
                              const auto f = fc.getResult();
                              if (f.existsAsFile())
                                  sp->useImageFile (f);
                          });
}

void LyraTuneEditor::useImageFile (const juce::File& f)
{
    auto img = juce::ImageFileFormat::loadFrom (f);
    if (! img.isValid())
    {
        juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::WarningIcon, "Lyra Tune", "That picture could not be opened.");
        return;
    }

    const int longest = juce::jmax (img.getWidth(), img.getHeight());
    if (longest > kMaxCustomSide)
    {
        const float k = float (kMaxCustomSide) / float (longest);
        img = img.rescaled (juce::jmax (1, juce::roundToInt (float (img.getWidth()) * k)),
                            juce::jmax (1, juce::roundToInt (float (img.getHeight()) * k)), juce::Graphics::highResamplingQuality);
    }

    // Keep our own copy, so the picture still works if the original file is moved or deleted,
    // and every instance of the plugin shows the same picture.
    const auto store = customImageFile();
    if (store.getParentDirectory().createDirectory().wasOk())
    {
        store.deleteFile();
        if (juce::FileOutputStream out (store); out.openedOk())
            juce::PNGImageFormat().writeImageToStream (img, out);
    }

    customImage = img;
    repaint();
}

void LyraTuneEditor::resetImage()
{
    customImageFile().deleteFile();
    customImage = {};
    repaint();
}
