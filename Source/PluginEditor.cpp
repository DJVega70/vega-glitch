#include "PluginEditor.h"

namespace
{
class GrainView : public juce::Component
{
public:
    explicit GrainView (VegaGlitchProcessor& p) : proc (p) {}
    void paint (juce::Graphics& g) override
    {
        vegaui::drawScreen (g, getLocalBounds().toFloat());
        auto r = getLocalBounds().toFloat().reduced (12.0f, 10.0f);
        g.setFont (VegaFonts::ui (9.5f, true)); g.setColour (VegaColours::textDim);
        g.drawText ("GRAIN CLOUD  -  LAST 2 SECONDS", r.removeFromTop (14).toNearestInt(), juce::Justification::left);
        const bool frozen = proc.apvts.getRawParameterValue ("freeze")->load() > 0.5f;
        if (frozen) { g.setColour (VegaColours::gold); g.drawText ("FROZEN", getLocalBounds().reduced (12, 10), juce::Justification::topRight); }
        g.setColour (juce::Colour (0xff262626)); g.drawHorizontalLine ((int) r.getCentreY(), r.getX(), r.getRight());
        rng.setSeed (7);
        for (int k = 0; k < VegaGlitchProcessor::maxGrains; ++k)
        {
            const float pos = proc.grainPos[(size_t) k].load();
            if (pos < 0) continue;
            const float x = r.getX() + pos * r.getWidth(), y = r.getY() + r.getHeight() * (0.15f + 0.7f * rng.nextFloat());
            const float sz = 5.0f + 9.0f * proc.apvts.getRawParameterValue ("size")->load() / 500.0f;
            g.setColour (VegaColours::accent.withAlpha (0.25f)); g.fillEllipse (x - sz, y - sz, sz * 2, sz * 2);
            g.setColour (VegaColours::accent); g.fillEllipse (x - 2, y - 2, 4, 4);
        }
        g.setColour (VegaColours::gold); g.fillRect (r.getRight() - 2, r.getY(), 2.0f, r.getHeight());
    }
private:
    VegaGlitchProcessor& proc; juce::Random rng;
};

class StepRow : public juce::Component
{
public:
    explicit StepRow (VegaGlitchProcessor& p) : proc (p) {}
    void paint (juce::Graphics& g) override
    {
        const float w = getWidth() / (float) VegaGlitchProcessor::numSteps;
        const int cur = proc.uiStep.load();
        auto names = VegaGlitchProcessor::fxNames();
        static const juce::uint32 cols[] { 0xff2a2a2a, 0xff48e1e8, 0xffa58bff, 0xffe8b448, 0xffe8708a, 0xff7ee87a, 0xff6fb7ff, 0xffffa25e };
        for (int i = 0; i < VegaGlitchProcessor::numSteps; ++i)
        {
            auto r = juce::Rectangle<float> (i * w, 0, w, (float) getHeight()).reduced (2.5f);
            const int fx = proc.pattern[(size_t) i].load();
            g.setColour (juce::Colour (fx == 0 ? ((i / 4) % 2 ? 0xff1c1c1c : 0xff222222) : cols[fx]).withAlpha (fx == 0 ? 1.0f : 0.8f));
            g.fillRoundedRectangle (r, 4.0f);
            if (i == cur) { g.setColour (juce::Colours::white); g.drawRoundedRectangle (r, 4.0f, 2.0f); }
            g.setColour (fx == 0 ? VegaColours::textDim : juce::Colours::black);
            g.setFont (VegaFonts::ui (9.5f, true));
            g.drawFittedText (fx == 0 ? juce::String (i + 1) : names[fx], r.reduced (2).toNearestInt(), juce::Justification::centred, 2);
        }
    }
    void mouseDown (const juce::MouseEvent& e) override
    {
        const int i = juce::jlimit (0, VegaGlitchProcessor::numSteps - 1, (int) (e.position.x / (getWidth() / (float) VegaGlitchProcessor::numSteps)));
        if (e.mods.isPopupMenu())
        {
            juce::PopupMenu m; auto names = VegaGlitchProcessor::fxNames();
            for (int k = 0; k < names.size(); ++k) m.addItem (1 + k, k == 0 ? juce::String ("Off") : names[k], true, proc.pattern[(size_t) i].load() == k);
            m.showMenuAsync ({}, [this, i] (int r) { if (r > 0) { proc.pattern[(size_t) i].store (r - 1); repaint(); } });
            return;
        }
        proc.pattern[(size_t) i].store ((proc.pattern[(size_t) i].load() + 1) % VegaGlitchProcessor::NumFx);
        repaint();
    }
private:
    VegaGlitchProcessor& proc;
};
}

class VegaGlitchEditor::Main : public juce::Component
{
public:
    explicit Main (VegaGlitchProcessor& p) : proc (p), a (p.apvts), view (p), steps (p),
        grainMix (a, "grainMix", "GRAINS", true), size (a, "size", "Size"), density (a, "density", "Density"), scatter (a, "scatter", "Scatter"),
        pitch (a, "pitch", "Pitch"), spray (a, "spray", "Spray"), reverse (a, "reverseProb", "Reverse"), width (a, "width", "Width"), freeze (a, "freeze", "Freeze"),
        seqOn (a, "seqOn", "Sequencer"), rate (a, "rate", "Step Rate"), repeats (a, "repeats", "Stutter"), chaos (a, "chaos", "CHAOS", true), crush (a, "crush", "Crush"),
        mix (a, "mix", "Mix"), output (a, "output", "Output")
    {
        for (auto* c : std::initializer_list<juce::Component*> { &view, &steps, &grainMix, &size, &density, &scatter, &pitch, &spray, &reverse, &width, &freeze,
                                                                 &seqOn, &rate, &repeats, &chaos, &crush, &mix, &output, &meter })
            addAndMakeVisible (c);
        randomBtn.setButtonText ("RANDOMIZE"); clearBtn.setButtonText ("CLEAR");
        randomBtn.onClick = [this] { proc.randomizePattern (rng); steps.repaint(); };
        clearBtn.onClick = [this] { for (auto& s : proc.pattern) s.store (0); steps.repaint(); };
        addAndMakeVisible (randomBtn); addAndMakeVisible (clearBtn);
    }
    void refresh() { view.repaint(); steps.repaint(); meter.setLevels (proc.outL.exchange (0.0f), proc.outR.exchange (0.0f)); }
    void paint (juce::Graphics& g) override
    {
        g.fillAll (VegaColours::background);
        vegaui::drawTopBar (g, W, "GLITCH  VP-21");
        vegaui::drawBody (g, { 0, 35, W, H - 65 });
        g.setFont (VegaFonts::ui (10.0f, true)); g.setColour (VegaColours::textDim);
        g.drawText ("GLITCH SEQUENCER  -  click a step to cycle, right-click to choose", 14, 216, 500, 14, juce::Justification::left);
        g.setColour (VegaColours::line); g.drawHorizontalLine (300, 0, (float) W);
        vegaui::drawSection (g, { 0, 304, 560, 266 }, "GRANULAR", false);
        vegaui::drawSection (g, { 560, 304, 360, 266 }, "GLITCH");
        vegaui::drawSection (g, { 920, 304, W - 920, 266 }, "OUTPUT");
        vegaui::drawBottomBar (g, { 0, H - 30, W, 30 }, "THE SEQUENCER FOLLOWS YOUR PROJECT  -  PRESS PLAY IN FL STUDIO TO HEAR THE GLITCHES");
    }
    void resized() override
    {
        view.setBounds (12, 44, 990, 164); meter.setBounds (1012, 40, 76, 170);
        steps.setBounds (12, 234, W - 220, 58); randomBtn.setBounds (W - 200, 236, 104, 26); clearBtn.setBounds (W - 90, 236, 78, 26);
        grainMix.setBounds (14, 322, 130, 160); size.setBounds (150, 322, 80, 112); density.setBounds (232, 322, 80, 112); scatter.setBounds (314, 322, 80, 112);
        reverse.setBounds (396, 322, 80, 112); width.setBounds (478, 322, 80, 112);
        pitch.setBounds (150, 446, 80, 112); spray.setBounds (232, 446, 80, 112); freeze.setBounds (330, 460, 70, 80);
        chaos.setBounds (570, 322, 130, 160); seqOn.setBounds (712, 330, 70, 80); rate.setBounds (790, 336, 120, 46);
        repeats.setBounds (712, 470, 120, 46); crush.setBounds (836, 446, 80, 112);
        mix.setBounds (930, 322, 80, 112); output.setBounds (1012, 322, 80, 112);
    }
private:
    VegaGlitchProcessor& proc; juce::AudioProcessorValueTreeState& a;
    GrainView view; StepRow steps;
    vegaui::Knob grainMix, size, density, scatter, pitch, spray, reverse, width; vegaui::Toggle freeze, seqOn; vegaui::Choice rate, repeats; vegaui::Knob chaos, crush, mix, output;
    vegaui::StereoMeter meter; juce::TextButton randomBtn, clearBtn; juce::Random rng;
};

VegaGlitchEditor::VegaGlitchEditor (VegaGlitchProcessor& p) : AudioProcessorEditor (&p), proc (p)
{
    setLookAndFeel (&lnf);
    main = std::make_unique<Main> (p);
    addAndMakeVisible (*main);
    constrainer.setFixedAspectRatio ((double) W / H);
    constrainer.setSizeLimits (W / 2, H / 2, W * 2, H * 2);
    setConstrainer (&constrainer);
    setResizable (true, true);
    const float s = juce::jlimit (0.5f, 2.0f, proc.uiScale.load());
    setSize (juce::roundToInt (W * s), juce::roundToInt (H * s));
    startTimerHz (30);
}
VegaGlitchEditor::~VegaGlitchEditor() { stopTimer(); setConstrainer (nullptr); main.reset(); setLookAndFeel (nullptr); }
void VegaGlitchEditor::timerCallback() { main->refresh(); }
void VegaGlitchEditor::resized()
{
    main->setBounds (0, 0, W, H);
    const float s = (float) getWidth() / (float) W;
    main->setTransform (juce::AffineTransform::scale (s));
    proc.uiScale.store (s);
}
juce::AudioProcessorEditor* VegaGlitchProcessor::createEditor() { return new VegaGlitchEditor (*this); }
