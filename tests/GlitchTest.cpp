#include "../Source/PluginProcessor.h"
#include <iostream>
static int fails = 0;
static void check (bool ok, const juce::String& m) { std::cout << (ok ? "PASS " : "FAIL ") << m << "\n"; if (! ok) ++fails; }
static void set (VegaGlitchProcessor& p, const char* id, float v) { auto* q = p.apvts.getParameter (id); q->setValueNotifyingHost (q->convertTo0to1 (v)); }
const double fs = 48000; const int blk = 480;
struct Head : juce::AudioPlayHead { double ppq = 0; bool playing = true; juce::Optional<PositionInfo> getPosition() const override { PositionInfo i; i.setBpm (120); i.setPpqPosition (ppq); i.setIsPlaying (playing); return i; } };
static float noiseAt (int i) { juce::Random r (i * 7919 + 13); return r.nextFloat() - 0.5f; }
static std::vector<float> run (VegaGlitchProcessor& p, Head& h, int n, std::function<float (int)> gen, int offset = 0)
{
    std::vector<float> out; juce::AudioBuffer<float> b (2, blk); juce::MidiBuffer m;
    for (int pos = 0; pos < n; pos += blk)
    {
        h.ppq = (offset + pos) / 24000.0;
        for (int i = 0; i < blk; ++i) { const float x = gen (offset + pos + i); b.setSample (0, i, x); b.setSample (1, i, x); }
        p.processBlock (b, m);
        for (int i = 0; i < blk; ++i) out.push_back (b.getSample (0, i));
    }
    return out;
}
static double rms (const std::vector<float>& v, int a, int e) { double s = 0; for (int i = a; i < e; ++i) s += v[(size_t) i] * v[(size_t) i]; return std::sqrt (s / std::max (1, e - a)); }

int main()
{
    juce::ScopedJuceInitialiser_GUI init;
    Head head;
    auto make = [&] { auto* p = new VegaGlitchProcessor(); p->setPlayConfigDetails (2, 2, fs, blk); p->prepareToPlay (fs, blk); p->setPlayHead (&head); return std::unique_ptr<VegaGlitchProcessor> (p); };
    auto onlySeq = [] (VegaGlitchProcessor& p, int step, int fx) { for (auto& s : p.pattern) s.store (0); p.pattern[(size_t) step].store (fx); set (p, "grainMix", 0); set (p, "chaos", 0); };
    // 1/16 steps at 120 BPM = 6000 samples; step 4 starts at 24000
    {
        auto p = make(); onlySeq (*p, 4, VegaGlitchProcessor::Off);
        auto o = run (*p, head, 48000, noiseAt);
        double err = 0; for (int i = 0; i < 48000; ++i) err = std::max (err, (double) std::abs (o[(size_t) i] - noiseAt (i)));
        check (err < 1e-6, "empty pattern and no grains = untouched audio");
    }
    {
        auto p = make(); onlySeq (*p, 4, VegaGlitchProcessor::Stutter); set (*p, "repeats", 2);   // 4 repeats -> 1500-sample slice
        auto o = run (*p, head, 48000, noiseAt);
        double diff = 0, sig = 0;
        for (int i = 24000 + 1500 + 100; i < 30000 - 100; ++i) { if ((i - 24000) % 1500 < 120 || (i - 24000) % 1500 > 1380) continue; diff += std::pow (o[(size_t) i] - o[(size_t) (i - 1500)], 2); sig += o[(size_t) i] * o[(size_t) i]; }
        check (sig > 0 && diff / sig < 1e-6, "stutter repeats a 1/64 slice 4 times in the step");
    }
    {
        auto p = make(); onlySeq (*p, 4, VegaGlitchProcessor::Reverse);
        auto o = run (*p, head, 48000, noiseAt);
        double err = 0; for (int k = 10; k < 5990; ++k) err = std::max (err, (double) std::abs (o[(size_t) (24000 + k)] - noiseAt (24000 - 1 - k)));
        check (err < 1e-5, "reverse step plays the previous step backwards");
    }
    {
        auto p = make(); onlySeq (*p, 4, VegaGlitchProcessor::TapeStop);
        auto o = run (*p, head, 48000, [] (int i) { return 0.5f * (float) std::sin (2 * juce::MathConstants<double>::pi * 440 * i / fs); });
        auto zc = [&] (int a, int b) { int z = 0; for (int i = a + 1; i < b; ++i) z += (o[(size_t) i - 1] < 0) != (o[(size_t) i] < 0); return z; };
        check (zc (24000, 25500) > zc (27000, 28500) * 1.5 && rms (o, 29700, 30000) < 0.05, "tape stop slows the pitch down to silence");
    }
    {
        auto p = make(); onlySeq (*p, 4, VegaGlitchProcessor::PitchUp);
        auto o = run (*p, head, 48000, [] (int i) { return 0.5f * (float) std::sin (2 * juce::MathConstants<double>::pi * 300 * i / fs); });
        int z = 0; for (int i = 24201; i < 29800; ++i) z += (o[(size_t) i - 1] < 0) != (o[(size_t) i] < 0);
        const double f = z / 2.0 / (5600.0 / fs);
        check (std::abs (f - 600) < 30, "pitch-up step plays an octave higher (" + juce::String (f, 0) + " Hz)");
    }
    {   // grains at +12: zero-crossing rate doubles
        auto p = make(); for (auto& s : p->pattern) s.store (0); set (*p, "grainMix", 100); set (*p, "mix", 100); set (*p, "pitch", 12); set (*p, "spray", 0); set (*p, "reverseProb", 0); set (*p, "scatter", 10);
        auto o = run (*p, head, 96000, [] (int i) { return 0.5f * (float) std::sin (2 * juce::MathConstants<double>::pi * 200 * i / fs); });
        int z = 0; for (int i = 48001; i < 96000; ++i) z += (o[(size_t) i - 1] < 0) != (o[(size_t) i] < 0);
        const double f = z / 2.0 / 1.0;
        check (rms (o, 48000, 96000) > 0.05 && std::abs (f - 400) < 60, "grain cloud at +12 st sounds an octave up (" + juce::String (f, 0) + " Hz)");
    }
    {   // freeze: input stops, cloud keeps going
        auto p = make(); for (auto& s : p->pattern) s.store (0); set (*p, "grainMix", 100); set (*p, "scatter", 50);
        run (*p, head, 96000, noiseAt);
        set (*p, "freeze", 1);
        auto o = run (*p, head, 96000, [] (int) { return 0.0f; }, 96000);
        check (rms (o, 48000, 96000) > 0.02, "freeze holds the texture after the input stops (" + juce::String (rms (o, 48000, 96000), 3) + ")");
    }
    {   // everything on with chaos: finite and bounded
        auto p = make(); juce::Random r (3); p->randomizePattern (r); set (*p, "chaos", 100); set (*p, "grainMix", 60); set (*p, "density", 60); set (*p, "size", 500);
        auto o = run (*p, head, 48000 * 4, noiseAt);
        bool fin = true; float pk = 0; for (float v : o) { fin &= std::isfinite (v); pk = std::max (pk, std::abs (v)); }
        check (fin && pk < 4.0f, "full chaos stays finite (peak " + juce::String (pk, 2) + ")");
    }
    {
        auto p = make(); p->pattern[3].store (VegaGlitchProcessor::Crush); juce::MemoryBlock mb; p->getStateInformation (mb);
        auto q = make(); q->setStateInformation (mb.getData(), (int) mb.getSize());
        check (q->pattern[3].load() == VegaGlitchProcessor::Crush, "pattern saved with the project");
        q->setPlayHead (nullptr); p->setPlayHead (nullptr);
    }
    std::cout << (fails == 0 ? "ALL PASSED" : "FAILURES: " + std::to_string (fails)) << "\n";
    return fails == 0 ? 0 : 1;
}
