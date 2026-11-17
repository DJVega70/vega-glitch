#include "PluginProcessor.h"
#include "PluginEditor.h"

namespace
{
enum { kGrainMix, kSize, kDensity, kScatter, kPitch, kSpray, kReverse, kFreeze, kWidth, kSeqOn, kRate, kRepeats, kChaos, kCrush, kMix, kOut };
const char* ids[] = { "grainMix", "size", "density", "scatter", "pitch", "spray", "reverseProb", "freeze", "width", "seqOn", "rate", "repeats", "chaos", "crush", "mix", "output" };
constexpr int numP = 16;
}

juce::StringArray VegaGlitchProcessor::fxNames() { return { "-", "STUTTER", "REVERSE", "TAPE STOP", "CRUSH", "PITCH UP", "PITCH DN", "GATE" }; }

juce::AudioProcessorValueTreeState::ParameterLayout VegaGlitchProcessor::layout()
{
    using namespace juce;
    std::vector<std::unique_ptr<RangedAudioParameter>> v;
    auto pct = [] (float x, int) { return String (roundToInt (x)) + "%"; };
    auto F = [&] (int id, const char* name, NormalisableRange<float> r, float def, std::function<String (float, int)> f)
    { v.push_back (std::make_unique<AudioParameterFloat> (ParameterID { ids[id], 1 }, name, r, def, AudioParameterFloatAttributes().withStringFromValueFunction (f))); };
    F (kGrainMix, "Grain Mix", NormalisableRange<float> (0, 100), 35, pct);
    F (kSize, "Grain Size", NormalisableRange<float> (10, 500, 0, 0.5f), 80, [] (float x, int) { return String (roundToInt (x)) + " ms"; });
    F (kDensity, "Density", NormalisableRange<float> (1, 60, 0, 0.5f), 14, [] (float x, int) { return String (x, 1) + " /s"; });
    F (kScatter, "Scatter", NormalisableRange<float> (0, 100), 40, pct);
    F (kPitch, "Grain Pitch", NormalisableRange<float> (-24, 24, 1), 0, [] (float x, int) { return (x > 0 ? "+" : "") + String (roundToInt (x)) + " st"; });
    F (kSpray, "Pitch Spray", NormalisableRange<float> (0, 12), 0, [] (float x, int) { return String (x, 1) + " st"; });
    F (kReverse, "Reverse", NormalisableRange<float> (0, 100), 20, pct);
    v.push_back (std::make_unique<AudioParameterBool> (ParameterID { ids[kFreeze], 1 }, "Freeze", false));
    F (kWidth, "Width", NormalisableRange<float> (0, 100), 70, pct);
    v.push_back (std::make_unique<AudioParameterBool> (ParameterID { ids[kSeqOn], 1 }, "Glitch Sequencer", true));
    v.push_back (std::make_unique<AudioParameterChoice> (ParameterID { ids[kRate], 1 }, "Step Rate", StringArray { "1/32", "1/16", "1/8", "1/4" }, 1));
    v.push_back (std::make_unique<AudioParameterChoice> (ParameterID { ids[kRepeats], 1 }, "Stutter Repeats", StringArray { "2", "3", "4", "6", "8", "16" }, 2));
    F (kChaos, "Chaos", NormalisableRange<float> (0, 100), 15, pct);
    F (kCrush, "Crush", NormalisableRange<float> (0, 100), 60, pct);
    F (kMix, "Mix", NormalisableRange<float> (0, 100), 100, pct);
    F (kOut, "Output", NormalisableRange<float> (-24, 12), 0, [] (float x, int) { return String (x, 1) + " dB"; });
    return { v.begin(), v.end() };
}

VegaGlitchProcessor::VegaGlitchProcessor()
    : AudioProcessor (BusesProperties().withInput ("Input", juce::AudioChannelSet::stereo(), true).withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "PARAMS", layout())
{
    for (int i = 0; i < numP; ++i) p[i] = apvts.getRawParameterValue (ids[i]);
    // a starter pattern: stutter the end of the bar, a reverse and a tape stop
    const int start[] { 0, 0, 0, 0, 0, 0, Reverse, 0, 0, 0, Stutter, 0, 0, Gate, Stutter, TapeStop };
    for (int i = 0; i < numSteps; ++i) pattern[(size_t) i].store (start[i]);
    for (auto& g : grainPos) g.store (-1.0f);
}

bool VegaGlitchProcessor::isBusesLayoutSupported (const BusesLayout& l) const
{
    const auto o = l.getMainOutputChannelSet();
    return o == juce::AudioChannelSet::stereo() && (l.getMainInputChannelSet() == o || l.getMainInputChannelSet() == juce::AudioChannelSet::mono());
}

void VegaGlitchProcessor::prepareToPlay (double s, int)
{
    sr = s;
    for (auto& r : ring) r.assign ((size_t) (s * 4.0), 0.0f);
    wpos = 0; nextGrainIn = 0; lastStepIndex = -1; stepFx = Off; fxGain = 1;
    for (auto& g : grains) g.active = false;
}

void VegaGlitchProcessor::randomizePattern (juce::Random& r)
{
    for (int i = 0; i < numSteps; ++i)
    {
        const float x = r.nextFloat();
        pattern[(size_t) i].store (x < 0.55f ? Off : 1 + r.nextInt (NumFx - 1));
    }
}

float VegaGlitchProcessor::readRing (int ch, double pos) const
{
    const auto& b = ring[ch]; const int n = (int) b.size();
    while (pos < 0) pos += n; while (pos >= n) pos -= n;
    const int i = (int) pos; const float f = (float) (pos - i);
    return b[(size_t) i] + (b[(size_t) ((i + 1) % n)] - b[(size_t) i]) * f;
}

void VegaGlitchProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals nd;
    const int n = buffer.getNumSamples();
    if (n == 0) return;
    if (getTotalNumInputChannels() == 1) buffer.copyFrom (1, 0, buffer, 0, 0, n);
    float* L = buffer.getWritePointer (0); float* R = buffer.getWritePointer (1);
    const int rn = (int) ring[0].size();

    double ppq = 0; bool playing = false;
    if (auto* ph = getPlayHead()) if (auto pos = ph->getPosition())
    {
        if (auto b = pos->getBpm()) bpm = juce::jlimit (20.0, 400.0, *b);
        if (pos->getIsPlaying()) if (auto q = pos->getPpqPosition()) { ppq = *q; playing = true; }
    }
    const double spb = sr * 60.0 / bpm;

    // ---- parameters
    const float grainMix = p[kGrainMix]->load() * 0.01f, chaos = p[kChaos]->load() * 0.01f, mix = p[kMix]->load() * 0.01f;
    const int sizeN = std::max (64, (int) (p[kSize]->load() * 0.001 * sr));
    const double density = p[kDensity]->load();
    const float scatter = p[kScatter]->load() * 0.01f, spray = p[kSpray]->load(), revProb = p[kReverse]->load() * 0.01f, width = p[kWidth]->load() * 0.01f;
    const double basePitch = std::pow (2.0, p[kPitch]->load() / 12.0);
    const bool freeze = p[kFreeze]->load() > 0.5f, seqOn = p[kSeqOn]->load() > 0.5f;
    static const double rates[] { 0.125, 0.25, 0.5, 1.0 };
    static const int repeatsTab[] { 2, 3, 4, 6, 8, 16 };
    const double stepBeats = rates[juce::jlimit (0, 3, (int) p[kRate]->load())];
    const int crushBits = juce::roundToInt (12.0f - p[kCrush]->load() * 0.09f), crushHoldN = 1 + (int) (p[kCrush]->load() * 0.2f);
    const float q = 1.0f / (float) (1 << std::max (2, crushBits - 1));
    const float outG = juce::Decibels::decibelsToGain (p[kOut]->load());

    float pk[2] {};
    for (int i = 0; i < n; ++i)
    {
        const float inL = L[i], inR = R[i];
        if (! freeze) { ring[0][(size_t) wpos] = inL; ring[1][(size_t) wpos] = inR; }

        // ---- glitch sequencer
        float gl = inL, gr = inR;
        if (seqOn && playing)
        {
            const double beat = ppq + i / spb;
            const long long s = (long long) std::floor (beat / stepBeats);
            if (s != lastStepIndex)
            {
                lastStepIndex = s;
                const int idx = (int) (s % numSteps);
                stepFx = pattern[(size_t) idx].load();
                if (chaos > 0 && rng.nextFloat() < chaos * 0.5f) stepFx = rng.nextFloat() < 0.3f ? Off : 1 + rng.nextInt (NumFx - 1);
                stepRepeats = repeatsTab[juce::jlimit (0, 5, (int) p[kRepeats]->load())];
                if (chaos > 0 && rng.nextFloat() < chaos) stepRepeats = repeatsTab[rng.nextInt (6)];
                stepLen = stepBeats * spb; stepPos = 0; stepStartW = wpos;
                uiStep.store (idx); uiStepFx.store (stepFx);
            }
            const double t = stepPos / std::max (1.0, stepLen);
            switch (stepFx)
            {
                case Stutter:
                {   // loop the first 1/repeats of the step, with a tiny fade at each seam
                    const double slice = std::max (32.0, stepLen / stepRepeats);
                    const double k = std::fmod (stepPos, slice);
                    const double rp = stepStartW + k;
                    const float env = (float) std::min (1.0, std::min (k, slice - k) / (0.002 * sr));
                    gl = readRing (0, rp) * env; gr = readRing (1, rp) * env;
                    break;
                }
                case Reverse: { const double rp = stepStartW - 1 - stepPos; gl = readRing (0, rp); gr = readRing (1, rp); break; }
                case TapeStop:
                {   // read position advances with a speed that falls from 1 to 0
                    const double pos = stepLen * (t - 0.5 * t * t);
                    const float lv = (float) std::min (1.0, (1.0 - t) * 4.0);
                    gl = readRing (0, stepStartW + pos) * lv; gr = readRing (1, stepStartW + pos) * lv;
                    break;
                }
                case Crush:
                    if (--crushCnt <= 0) { crushHold[0] = std::round (inL / q) * q; crushHold[1] = std::round (inR / q) * q; crushCnt = crushHoldN; }
                    gl = crushHold[0]; gr = crushHold[1];
                    break;
                case PitchUp: { const double rp = stepStartW - stepLen + stepPos * 2.0; gl = readRing (0, rp); gr = readRing (1, rp); break; }
                case PitchDown: { const double rp = stepStartW - stepLen + stepPos * 0.5; gl = readRing (0, rp); gr = readRing (1, rp); break; }
                case Gate: { const double chop = std::fmod (stepPos, stepLen / 4.0) / (stepLen / 4.0); const float g = chop < 0.5 ? 1.0f : 0.0f; fxGain += (g - fxGain) * 0.05f; gl *= fxGain; gr *= fxGain; break; }
                default: break;
            }
            if (stepFx != Gate) fxGain = 1.0f;
            stepPos += 1.0;
        }
        else if (! playing) { lastStepIndex = -1; uiStep.store (-1); }

        // ---- granular cloud (reads the recent input)
        float wl = 0, wr = 0;
        if (grainMix > 0)
        {
            nextGrainIn -= 1.0;
            if (nextGrainIn <= 0)
            {
                nextGrainIn = sr / density * (0.6 + 0.8 * rng.nextDouble());
                for (auto& g : grains)
                    if (! g.active)
                    {
                        const double sprayRatio = std::pow (2.0, (rng.nextFloat() * 2.0f - 1.0f) * spray / 12.0);
                        const bool rev = rng.nextFloat() < revProb;
                        g.inc = basePitch * sprayRatio * (rev ? -1.0 : 1.0);
                        g.len = sizeN;
                        const double back = sizeN * std::abs (g.inc) + 64 + scatter * rng.nextDouble() * sr * 1.8;   // never read ahead of the write head
                        g.pos = wpos - back + (rev ? sizeN * std::abs (g.inc) : 0.0);
                        const float pan = (rng.nextFloat() * 2.0f - 1.0f) * width;
                        g.panL = std::sqrt (0.5f * (1.0f - pan)); g.panR = std::sqrt (0.5f * (1.0f + pan));
                        g.age = 0; g.active = true;
                        break;
                    }
            }
            for (auto& g : grains)
            {
                if (! g.active) continue;
                const float w = 0.5f - 0.5f * std::cos (juce::MathConstants<float>::twoPi * g.age / g.len);
                const float m = 0.5f * (readRing (0, g.pos) + readRing (1, g.pos)) * w;
                wl += m * g.panL; wr += m * g.panR;
                g.pos += g.inc;
                if (++g.age >= g.len) g.active = false;
            }
            const float norm = 1.0f / std::sqrt (std::max (1.0f, (float) (density * sizeN / sr)));
            wl *= norm * 1.4f; wr *= norm * 1.4f;
        }
        if (! freeze) if (++wpos >= rn) wpos = 0;

        const float yl = gl * (1.0f - grainMix) + wl * grainMix, yr = gr * (1.0f - grainMix) + wr * grainMix;
        const float oL = (inL + (yl - inL) * mix) * outG, oR = (inR + (yr - inR) * mix) * outG;
        L[i] = oL; R[i] = oR;
        pk[0] = std::max (pk[0], std::abs (oL)); pk[1] = std::max (pk[1], std::abs (oR));
    }
    // UI: grain positions relative to the 2 s window behind the write head
    for (int k = 0; k < maxGrains; ++k)
    {
        const auto& g = grains[(size_t) k];
        double behind = wpos - g.pos; while (behind < 0) behind += rn;
        grainPos[(size_t) k].store (g.active ? (float) juce::jlimit (0.0, 1.0, 1.0 - behind / (sr * 2.0)) : -1.0f);
    }
    outL.store (std::max (outL.load(), pk[0])); outR.store (std::max (outR.load(), pk[1]));
}

void VegaGlitchProcessor::getStateInformation (juce::MemoryBlock& d)
{
    auto s = apvts.copyState(); s.setProperty ("uiScale", uiScale.load(), nullptr);
    juce::String pat; for (auto& x : pattern) pat << x.load();
    s.setProperty ("pattern", pat, nullptr);
    if (auto x = s.createXml()) copyXmlToBinary (*x, d);
}
void VegaGlitchProcessor::setStateInformation (const void* data, int size)
{
    if (auto x = getXmlFromBinary (data, size))
    {
        auto s = juce::ValueTree::fromXml (*x);
        if (! s.hasType (apvts.state.getType())) return;
        uiScale.store ((float) s.getProperty ("uiScale", 1.0f));
        const auto pat = s.getProperty ("pattern").toString();
        if (pat.length() == numSteps) for (int i = 0; i < numSteps; ++i) pattern[(size_t) i].store (juce::jlimit (0, NumFx - 1, (int) (pat[i] - '0')));
        apvts.replaceState (s);
    }
}
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new VegaGlitchProcessor(); }
