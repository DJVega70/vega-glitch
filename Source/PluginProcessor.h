#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_dsp/juce_dsp.h>

class VegaGlitchProcessor : public juce::AudioProcessor
{
public:
    VegaGlitchProcessor();
    bool isBusesLayoutSupported (const BusesLayout&) const override;
    void prepareToPlay (double, int) override;
    void releaseResources() override {}
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }
    const juce::String getName() const override { return "Vega Productions Glitch"; }
    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return false; }
    double getTailLengthSeconds() const override { return 1.0; }
    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return {}; }
    void changeProgramName (int, const juce::String&) override {}
    void getStateInformation (juce::MemoryBlock&) override;
    void setStateInformation (const void*, int) override;

    static juce::AudioProcessorValueTreeState::ParameterLayout layout();
    juce::AudioProcessorValueTreeState apvts;

    enum Fx { Off, Stutter, Reverse, TapeStop, Crush, PitchUp, PitchDown, Gate, NumFx };
    static juce::StringArray fxNames();
    static constexpr int numSteps = 16;
    std::array<std::atomic<int>, numSteps> pattern {};
    void randomizePattern (juce::Random&);

    // UI feedback
    std::atomic<int> uiStep { -1 }, uiStepFx { 0 };
    static constexpr int maxGrains = 48;
    std::array<std::atomic<float>, maxGrains> grainPos {};      // 0..1 position in the 2 s window, <0 = idle
    std::atomic<float> outL { 0 }, outR { 0 }, uiScale { 1.0f };
    double bpm = 120;

private:
    float readRing (int ch, double pos) const;
    struct Grain { bool active = false; double pos = 0, inc = 1; int age = 0, len = 1; float panL = 1, panR = 1; };
    std::array<Grain, maxGrains> grains;
    std::vector<float> ring[2]; int wpos = 0; double sr = 48000;
    double nextGrainIn = 0; juce::Random rng;
    // sequencer state
    long long lastStepIndex = -1; int stepFx = Off; int stepStartW = 0; double stepPos = 0, stepLen = 1; int stepRepeats = 4;
    float crushHold[2] {}; int crushCnt = 0; float fxGain = 1;
    double freeRunPpq = 0;
    std::atomic<float>* p[20] {};
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (VegaGlitchProcessor)
};
