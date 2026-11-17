#pragma once
#include "PluginProcessor.h"
#include "VegaUI.h"

class VegaGlitchEditor : public juce::AudioProcessorEditor, private juce::Timer
{
public:
    explicit VegaGlitchEditor (VegaGlitchProcessor&);
    ~VegaGlitchEditor() override;
    void paint (juce::Graphics&) override {}
    void resized() override;
    static constexpr int W = 1100, H = 600;
    class Main;
private:
    void timerCallback() override;
    VegaGlitchProcessor& proc;
    VegaLookAndFeel lnf;
    std::unique_ptr<Main> main;
    juce::ComponentBoundsConstrainer constrainer;
};
