#include "ReverbEffect.h"

#include <math.h>

#include "daisysp.h"
#include "dev/sdram.h"

using daisysp::DelayLine;

/** Delay memory.
 *
 *  Roughly half a megabyte, which rules out both internal SRAM and -- more
 *  importantly -- the RAM_D2_DMA region that the bootloader linker scripts
 *  shrink to 32K. It goes in the Seed's 64 MB of external SDRAM, declared at
 *  file scope because DSY_SDRAM_BSS is a placement attribute on a definition
 *  and cannot follow a member into a class. There is exactly one reverb, so the
 *  singleton this implies costs nothing.
 *
 *  Sizes are powers of two, comfortably above the longest tank any size setting
 *  asks for; lengths are clamped against them anyway before use.
 */
namespace
{
struct Tank
{
    DelayLine<float, 32768> predelay;

    DelayLine<float, 512>  dif1;
    DelayLine<float, 512>  dif2;
    DelayLine<float, 2048> dif3;
    DelayLine<float, 1024> dif4;

    DelayLine<float, 4096>  ap1l;
    DelayLine<float, 16384> del1l;
    DelayLine<float, 8192>  ap2l;
    DelayLine<float, 16384> del2l;

    DelayLine<float, 4096>  ap1r;
    DelayLine<float, 16384> del1r;
    DelayLine<float, 16384> ap2r;
    DelayLine<float, 16384> del2r;
};

Tank DSY_SDRAM_BSS tank;
} // namespace

/** Dattorro's lengths, in samples at his 29761 Hz reference rate. */
static constexpr float kRefRate = 29761.f;

static constexpr float kDif1 = 142.f, kDif2 = 107.f, kDif3 = 379.f,
                       kDif4 = 277.f;
static constexpr float kAp1L = 672.f, kDel1L = 4453.f, kAp2L = 1800.f,
                       kDel2L = 3720.f;
static constexpr float kAp1R = 908.f, kDel1R = 4217.f, kAp2R = 2656.f,
                       kDel2R = 3163.f;

/** Output taps, same reference rate. Seven taps per side, drawn from both
 *  halves of the tank so each output hears the other side's early energy --
 *  that cross-hearing is what makes the image wide rather than two reverbs
 *  panned apart. */
static constexpr float kTapL[7]
    = {266.f, 2974.f, 1913.f, 1996.f, 1066.f, 913.f, 378.f};
static constexpr float kTapR[7]
    = {353.f, 3627.f, 1228.f, 2673.f, 2111.f, 335.f, 121.f};

/** Tank scale per TOG_SW_2 position: room / plate / cavern. */
static constexpr float kSizeScale[3] = {0.5f, 1.0f, 1.6f};

/** Wet trim per size.
 *
 *  The seven output taps are at fixed positions in a tank whose length changes
 *  under them, so how much of their sum cancels changes with it. Left alone,
 *  flipping from plate to cavern jumped the wet level by 9 dB, which reads as a
 *  volume control rather than a size control. These bring the three within
 *  about 2 dB of each other across the TIME range. */
static constexpr float kSizeTrim[3] = {0.80f, 1.60f, 0.60f};

/** Decay coefficient at either end of the TIME knob. One pair for all three
 *  sizes, deliberately.
 *
 *  Reverb time is loop length divided by loop loss, and the size toggle already
 *  multiplies the loop length by 0.5, 1 and 1.6 -- so holding the coefficient
 *  still is what makes a cavern ring longer than a room, and raising it with
 *  size compounds the two. Measured: a ceiling that rose to 0.94 on the cavern
 *  left a tail still audible after sixteen seconds and built up to nearly three
 *  times the input under a sustained chord, loud enough to clip the codec. A
 *  flat 0.70 gives roughly 2.5, 5 and 8 seconds instead, and the tank never
 *  exceeds what goes into it. */
static constexpr float kDecayMax = 0.70f;
static constexpr float kDecayMin = 0.20f;

static constexpr float kPredelayMaxMs = 250.f;

/** Damping sweep: knob down is an open plate, knob up is a dark one. */
static constexpr float kDampMinHz = 16000.f, kDampMaxHz = 500.f;

/** Input high pass, to keep bass out of the tank where it turns to mud. */
static constexpr float kLowCutMinHz = 20.f, kLowCutMaxHz = 500.f;

/** Fixed input bandwidth limit, ahead of everything else. */
static constexpr float kBandwidthHz = 9000.f;

/** Tank input trim. The tank has gain of its own; this keeps a hot pedal input
 *  from driving it where the allpasses stop being transparent. */
static constexpr float kInputTrim = 0.5f;

/** Allpass modulation: a few samples, slowly, at two unrelated rates. */
static constexpr float kModDepth = 8.f;
static constexpr float kModRateL = 0.71f, kModRateR = 1.13f;

static constexpr float kTwoPi = 6.28318530718f;

/** One-pole coefficient for a given corner frequency. */
static inline float PoleCoeff(float sample_rate, float hz)
{
    return fminf(1.f, kTwoPi * hz / sample_rate);
}

static inline float Exponential(float knob, float lo, float hi)
{
    return lo * powf(hi / lo, knob);
}

/** Schroeder allpass over a delay line, with a fractional delay so the
 *  modulated sections can move without stepping. */
template <typename Line>
static inline float ApProcess(Line& line, float delay, float coeff, float x)
{
    const float read  = line.Read(delay);
    const float write = x + coeff * read;
    line.Write(write);
    return read - coeff * write;
}

void ReverbEffect::Init(float sample_rate)
{
    sample_rate_ = sample_rate;

    // SDRAM is not cleared by the startup code, so every line has to be reset
    // explicitly or the tank starts full of whatever was last in the chip.
    tank.predelay.Init();
    tank.dif1.Init();
    tank.dif2.Init();
    tank.dif3.Init();
    tank.dif4.Init();
    tank.ap1l.Init();
    tank.del1l.Init();
    tank.ap2l.Init();
    tank.del2l.Init();
    tank.ap1r.Init();
    tank.del1r.Init();
    tank.ap2r.Init();
    tank.del2r.Init();

    lowcut_z_ = band_z_ = damp_l_z_ = damp_r_z_ = 0.f;
    lfo_l_phase_                                = 0.f;
    lfo_r_phase_                                = 0.25f;

    bandwidth_coeff_ = PoleCoeff(sample_rate_, kBandwidthHz);
    unit_            = sample_rate_ / kRefRate;
}

const EffectDesc& ReverbEffect::Desc() const
{
    static const EffectDesc kDesc = {
        "Reverb",
        1.0f, 0.0f, 1.0f, // purple
        "size: room / plate / cavern",
        {{"time"}, {"damping"}, {"pre-delay"},
         {"diffusion"}, {"low cut"}, {"mix"}},
    };
    return kDesc;
}

void ReverbEffect::UpdateCoeffs(const float* params, int toggle, size_t size)
{
    const int size_index = toggle;

    unit_      = (sample_rate_ / kRefRate) * kSizeScale[size_index];
    size_trim_ = kSizeTrim[size_index];

    decay_ = kDecayMin
             + params[0] * (kDecayMax - kDecayMin);

    damp_coeff_ = PoleCoeff(sample_rate_,
                            Exponential(params[1],
                                        kDampMinHz,
                                        kDampMaxHz));

    lowcut_coeff_ = PoleCoeff(sample_rate_,
                              Exponential(params[4],
                                          kLowCutMinHz,
                                          kLowCutMaxHz));

    predelay_ = params[2] * kPredelayMaxMs
                * sample_rate_ * 0.001f;
    if(predelay_ > 32760.f)
        predelay_ = 32760.f;
    if(predelay_ < 1.f)
        predelay_ = 1.f;

    // Diffusion moves the input diffusers and the tank allpasses together:
    // fully down is a handful of discrete echoes, fully up is Dattorro's own
    // coefficients and a smooth wash.
    const float d = params[3];
    in_diff1_     = 0.25f + 0.50f * d;
    in_diff2_     = 0.20f + 0.425f * d;
    dec_diff1_    = 0.20f + 0.50f * d;
    dec_diff2_    = 0.15f + 0.35f * d;

    mix_ = params[5];

    // Advance the modulation once per block. A few samples of movement at under
    // 2 Hz does not need sample-rate resolution.
    const float block_seconds = static_cast<float>(size) / sample_rate_;

    lfo_l_phase_ += kModRateL * block_seconds;
    lfo_r_phase_ += kModRateR * block_seconds;
    lfo_l_phase_ -= floorf(lfo_l_phase_);
    lfo_r_phase_ -= floorf(lfo_r_phase_);

    mod_l_ = kModDepth * sinf(kTwoPi * lfo_l_phase_);
    mod_r_ = kModDepth * sinf(kTwoPi * lfo_r_phase_);
}

void ReverbEffect::Process(const float* params,
                           int          toggle,
                           float*       left,
                           float*       right,
                           size_t       size)
{
    UpdateCoeffs(params, toggle, size);

    // Every length and tap, scaled once per block and clamped so no size
    // setting can ask a line for more than it holds.
    const float unit = unit_;
    auto        len  = [unit](float reference, float max) {
        const float l = reference * unit;
        return l > max - 2.f ? max - 2.f : (l < 1.f ? 1.f : l);
    };

    const float l_dif1 = len(kDif1, 512.f), l_dif2 = len(kDif2, 512.f);
    const float l_dif3 = len(kDif3, 2048.f), l_dif4 = len(kDif4, 1024.f);

    const float l_ap1l = len(kAp1L, 4096.f), l_del1l = len(kDel1L, 16384.f);
    const float l_ap2l = len(kAp2L, 8192.f), l_del2l = len(kDel2L, 16384.f);
    const float l_ap1r = len(kAp1R, 4096.f), l_del1r = len(kDel1R, 16384.f);
    const float l_ap2r = len(kAp2R, 16384.f), l_del2r = len(kDel2R, 16384.f);

    float tap_l[7], tap_r[7];
    for(int i = 0; i < 7; i++)
    {
        tap_l[i] = len(kTapL[i], 8192.f);
        tap_r[i] = len(kTapR[i], 8192.f);
    }

    for(size_t i = 0; i < size; i++)
    {
        const float dry_l = left[i];
        const float dry_r = right[i];

        // A plate is one plate. Sum to mono in, take stereo out of the taps.
        float x = (dry_l + dry_r) * 0.5f * kInputTrim;

        tank.predelay.Write(x);
        x = tank.predelay.Read(predelay_);

        // Low cut, as a one-pole highpass: track the lows, subtract them.
        lowcut_z_ += lowcut_coeff_ * (x - lowcut_z_);
        x -= lowcut_z_;

        // Bandwidth limit into the tank.
        band_z_ += bandwidth_coeff_ * (x - band_z_);
        x = band_z_;

        x = ApProcess(tank.dif1, l_dif1, in_diff1_, x);
        x = ApProcess(tank.dif2, l_dif2, in_diff1_, x);
        x = ApProcess(tank.dif3, l_dif3, in_diff2_, x);
        x = ApProcess(tank.dif4, l_dif4, in_diff2_, x);

        // Read both cross-feeds before writing anything, so each half sees the
        // other half as it was one lap ago rather than half-updated.
        const float fb_l = tank.del2l.Read(l_del2l);
        const float fb_r = tank.del2r.Read(l_del2r);

        // Left half of the tank.
        float a = ApProcess(tank.ap1l, l_ap1l + mod_l_, -dec_diff1_, x + fb_r);
        tank.del1l.Write(a);
        float b = tank.del1l.Read(l_del1l);
        damp_l_z_ += damp_coeff_ * (b - damp_l_z_);
        b       = damp_l_z_ * decay_;
        float c = ApProcess(tank.ap2l, l_ap2l, dec_diff2_, b);
        tank.del2l.Write(c);

        // Right half.
        float d = ApProcess(tank.ap1r, l_ap1r + mod_r_, -dec_diff1_, x + fb_l);
        tank.del1r.Write(d);
        float e = tank.del1r.Read(l_del1r);
        damp_r_z_ += damp_coeff_ * (e - damp_r_z_);
        e       = damp_r_z_ * decay_;
        float f = ApProcess(tank.ap2r, l_ap2r, dec_diff2_, e);
        tank.del2r.Write(f);

        const float tap_gain = 0.6f * size_trim_;

        const float wet_l
            = tap_gain
              * (tank.del1r.Read(tap_l[0]) + tank.del1r.Read(tap_l[1])
                 - tank.ap2r.Read(tap_l[2]) + tank.del2r.Read(tap_l[3])
                 - tank.del1l.Read(tap_l[4]) - tank.ap2l.Read(tap_l[5])
                 - tank.del2l.Read(tap_l[6]));

        const float wet_r
            = tap_gain
              * (tank.del1l.Read(tap_r[0]) + tank.del1l.Read(tap_r[1])
                 - tank.ap2l.Read(tap_r[2]) + tank.del2l.Read(tap_r[3])
                 - tank.del1r.Read(tap_r[4]) - tank.ap2r.Read(tap_r[5])
                 - tank.del2r.Read(tap_r[6]));

        left[i]  = dry_l + mix_ * (wet_l - dry_l);
        right[i] = dry_r + mix_ * (wet_r - dry_r);
    }
}
