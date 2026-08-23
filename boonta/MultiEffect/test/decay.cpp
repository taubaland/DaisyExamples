// Measure the reverb's tail: how long it takes to fall 60 dB, per size, and
// how much it builds up under a sustained input.
#include <cmath>
#include <cstdio>
#include <vector>

#include "Chain.h"
#include "PedalState.h"

static constexpr float kSr    = 48000.f;
static constexpr float kTwoPi = 6.28318530718f;

static float Sine(size_t n, float hz)
{
    return std::sin(kTwoPi * hz * (float)n / kSr);
}

int main()
{
    const char* names[3] = {"room  ", "plate ", "cavern"};

    printf("\nsize   time  charged  build   tail rms at 1/2/4/8/16 s        RT60\n");
    printf("-------------------------------------------------------------------\n");

    for(int pos = 0; pos < 3; pos++)
    {
        for(int t = 0; t <= 2; t++)
        {
            const float time_knob = t * 0.5f;

            PedalState s;
            s.Reset();
            s.SetBypass(false);
            for(int i = 0; i < PedalState::kKnobCount; i++)
                s.SetKnob(PedalState::PAGE_EQ, i, 0.5f);
            s.SetKnob(PedalState::PAGE_META, PedalState::META_IN_GAIN, 0.5f);
            s.SetKnob(PedalState::PAGE_META, PedalState::META_OUT_LEVEL, 0.5f);
            s.SetKnob(PedalState::PAGE_META, PedalState::META_MIX, 1.f);
            s.SetKnob(PedalState::PAGE_META, PedalState::META_EQ_AMOUNT, 1.f);
            s.SetKnob(PedalState::PAGE_META, PedalState::META_DRIVE_AMOUNT, 0.f);
            s.SetKnob(PedalState::PAGE_META, PedalState::META_REVERB_AMOUNT, 1.f);
            s.SetKnob(PedalState::PAGE_REVERB, PedalState::REVERB_TIME, time_knob);
            s.SetKnob(PedalState::PAGE_REVERB, PedalState::REVERB_DAMPING, 0.f);
            s.SetKnob(PedalState::PAGE_REVERB, PedalState::REVERB_DIFFUSION, 1.f);
            s.SetKnob(PedalState::PAGE_REVERB, PedalState::REVERB_MIX, 1.f);
            s.SetToggle(PedalState::TOGGLE_REVERB_SIZE, (PedalState::TogglePos)pos);

            Chain chain;
            chain.Init(kSr);

            const size_t kB = 48;
            std::vector<float> il(kB), ir(kB), ol(kB), orr(kB);
            const float* in[2]  = {il.data(), ir.data()};
            float*       out[2] = {ol.data(), orr.data()};

            // Charge for 3 s with a sustained chord, tracking the peak.
            double sumsq = 0;
            size_t cnt = 0;
            float  build_peak = 0.f;
            size_t n = 0;
            for(; n < 144000; n += kB)
            {
                for(size_t i = 0; i < kB; i++)
                {
                    il[i] = 0.7f * (Sine(n + i, 220.f) + Sine(n + i, 330.f)) * 0.5f;
                    ir[i] = il[i];
                }
                chain.Process(s, in, out, kB);
                for(size_t i = 0; i < kB; i++)
                {
                    build_peak = std::max(build_peak, std::fabs(ol[i]));
                    if(n > 120000)
                    {
                        sumsq += (double)ol[i] * ol[i];
                        cnt++;
                    }
                }
            }
            const float charged = (float)std::sqrt(sumsq / cnt);

            // Silence, sampling RMS in 0.1 s windows at the marks.
            const size_t marks[5] = {48000, 96000, 192000, 384000, 768000};
            float        tail[5]  = {0, 0, 0, 0, 0};
            float        rt60     = -1.f;

            double wsum = 0;
            size_t wcnt = 0;
            int    mi   = 0;

            for(size_t k = 0; k < 768000 + 4800; k += kB)
            {
                for(size_t i = 0; i < kB; i++)
                    il[i] = ir[i] = 0.f;
                chain.Process(s, in, out, kB);

                if(mi < 5 && k >= marks[mi] && k < marks[mi] + 4800)
                {
                    for(size_t i = 0; i < kB; i++)
                    {
                        wsum += (double)ol[i] * ol[i];
                        wcnt++;
                    }
                }
                if(mi < 5 && k >= marks[mi] + 4800)
                {
                    tail[mi] = wcnt ? (float)std::sqrt(wsum / wcnt) : 0.f;
                    wsum = 0;
                    wcnt = 0;
                    mi++;
                }

                if(rt60 < 0.f)
                {
                    float pk = 0.f;
                    for(size_t i = 0; i < kB; i++)
                        pk = std::max(pk, std::fabs(ol[i]));
                    if(pk < charged * 0.001f)
                        rt60 = (float)k / kSr;
                }
            }

            printf("%s  %.1f  %7.4f  %5.2fx  %.4f %.4f %.4f %.4f %.4f  %6s\n",
                   names[pos],
                   time_knob,
                   charged,
                   build_peak / 0.7f,
                   tail[0], tail[1], tail[2], tail[3], tail[4],
                   rt60 < 0.f ? ">16 s" : [&] {
                       static char b[16];
                       snprintf(b, sizeof b, "%.1f s", rt60);
                       return b;
                   }());
        }
    }
    printf("\n");
    return 0;
}
