#include "Reverb.h"

#include <math.h>

#include "daisysp.h"
#include "dev/sdram.h"

using daisysp::DelayLine;
using daisysp::fclamp;
using daisysp::fonepole;

/** Delay memory.
 *
 *  A couple of hundred kilobytes, which rules out internal SRAM and -- more
 *  importantly -- the RAM_D2_DMA region that the boot linker scripts shrink to
 *  32K. It goes in the Seed's 64 MB of external SDRAM, declared at file scope
 *  because DSY_SDRAM_BSS is a placement attribute on a definition and cannot
 *  follow a member into a class. There is exactly one reverb, so the singleton
 *  this implies costs nothing.
 *
 *  Capacities are powers of two comfortably above the longest line asked for
 *  at any supported sample rate; the lengths set in Init() are clamped against
 *  them.
 *
 *  The lines are DaisySP's, whose Read() fetches both the sample it wants and
 *  the one after it so that it can interpolate to a fractional delay -- which
 *  none of these twelve lines needs, since none of them ever moves. Replacing
 *  them with an exact-integer line of our own was tried and measured on the
 *  board, and made no difference at all: the two samples are adjacent, so the
 *  second one is already in the cache line the first one pulled in. Left
 *  alone, because a library type that costs nothing extra beats forty lines
 *  of hand-rolled ring buffer.
 */
namespace
{
struct Tank
{
    DelayLine<float, 1024> in1, in2;
    DelayLine<float, 2048> in3, in4;

    DelayLine<float, 2048> ap_l;
    DelayLine<float, 8192> del_l;
    DelayLine<float, 8192> ap2_l;
    DelayLine<float, 8192> del2_l;

    DelayLine<float, 2048> ap_r;
    DelayLine<float, 8192> del_r;
    DelayLine<float, 8192> ap2_r;
    DelayLine<float, 8192> del2_r;
};

Tank DSY_SDRAM_BSS tank;

/** Dattorro's lengths, in samples at his 29761 Hz reference rate. */
constexpr float kRefRate = 29761.f;

constexpr float kIn1 = 142.f, kIn2 = 107.f, kIn3 = 379.f, kIn4 = 277.f;
constexpr float kApL = 672.f, kDelL = 4453.f, kAp2L = 1800.f, kDel2L = 3720.f;
constexpr float kApR = 908.f, kDelR = 4217.f, kAp2R = 2656.f, kDel2R = 3163.f;

/** Allpass coefficients. The input diffusers are gentler than the loop ones,
 *  which is what keeps a transient recognisable going in while still smearing
 *  once it is round the loop a few times. */
constexpr float kInDiffusion   = 0.70f;
constexpr float kLoopDiffusion = 0.50f;

/** Fixed input bandwidth limit, ahead of everything else: grains are already
 *  bright and a plate fed full band turns to hiss. */
constexpr float kBandwidthHz = 9000.f;

/** Set a line's length, clamped to what it can actually hold. */
template <size_t N>
void SetLength(DelayLine<float, N>& line, float ref_samples, float scale)
{
    const float len = ref_samples * scale;
    line.SetDelay(fclamp(len, 1.f, static_cast<float>(N - 1)));
}

/** One allpass stage, in place. */
template <size_t N>
inline float Allpass(DelayLine<float, N>& line, float in, float coeff)
{
    const float delayed = line.Read();
    const float v       = in - coeff * delayed;
    line.Write(v);
    return delayed + coeff * v;
}
} // namespace

void Reverb::Init(float sample_rate)
{
    sample_rate_ = sample_rate;

    tank.in1.Init();
    tank.in2.Init();
    tank.in3.Init();
    tank.in4.Init();
    tank.ap_l.Init();
    tank.del_l.Init();
    tank.ap2_l.Init();
    tank.del2_l.Init();
    tank.ap_r.Init();
    tank.del_r.Init();
    tank.ap2_r.Init();
    tank.del2_r.Init();

    const float scale = sample_rate_ / kRefRate;

    SetLength(tank.in1, kIn1, scale);
    SetLength(tank.in2, kIn2, scale);
    SetLength(tank.in3, kIn3, scale);
    SetLength(tank.in4, kIn4, scale);

    SetLength(tank.ap_l, kApL, scale);
    SetLength(tank.del_l, kDelL, scale);
    SetLength(tank.ap2_l, kAp2L, scale);
    SetLength(tank.del2_l, kDel2L, scale);

    SetLength(tank.ap_r, kApR, scale);
    SetLength(tank.del_r, kDelR, scale);
    SetLength(tank.ap2_r, kAp2R, scale);
    SetLength(tank.del2_r, kDel2R, scale);

    damp_z_l_     = 0.f;
    damp_z_r_     = 0.f;
    bandwidth_z_  = 0.f;
    cross_l_      = 0.f;
    cross_r_      = 0.f;

    SetTail(0.6f, 5000.f);
}

void Reverb::SetTail(float decay, float damp_hz)
{
    decay_ = fclamp(decay, 0.f, 0.88f);

    const float w = TWOPI_F * damp_hz / sample_rate_;
    damp_coeff_   = fclamp(w, 0.01f, 1.f);
}

void Reverb::Process(float in_l, float in_r, float& out_l, float& out_r)
{
    // Mono sum in: the stereo image comes out of the two loops, not out of
    // what went in, and feeding them separately only narrows the result.
    float x = 0.5f * (in_l + in_r);

    const float bw = fminf(1.f, TWOPI_F * kBandwidthHz / sample_rate_);
    fonepole(bandwidth_z_, x, bw);
    x = bandwidth_z_;

    x = Allpass(tank.in1, x, kInDiffusion);
    x = Allpass(tank.in2, x, kInDiffusion);
    x = Allpass(tank.in3, x, kInDiffusion);
    x = Allpass(tank.in4, x, kInDiffusion);

    // Each loop is fed by the input plus the *other* loop's last output. That
    // cross-feed is what makes one tank sound like one wide space rather than
    // two reverbs panned apart.
    float l = x + cross_r_ * decay_;
    float r = x + cross_l_ * decay_;

    l = Allpass(tank.ap_l, l, kLoopDiffusion);
    tank.del_l.Write(l);
    l = tank.del_l.Read();

    fonepole(damp_z_l_, l, damp_coeff_);
    l = damp_z_l_ * decay_;

    l = Allpass(tank.ap2_l, l, kLoopDiffusion);
    tank.del2_l.Write(l);
    l = tank.del2_l.Read();

    r = Allpass(tank.ap_r, r, kLoopDiffusion);
    tank.del_r.Write(r);
    r = tank.del_r.Read();

    fonepole(damp_z_r_, r, damp_coeff_);
    r = damp_z_r_ * decay_;

    r = Allpass(tank.ap2_r, r, kLoopDiffusion);
    tank.del2_r.Write(r);
    r = tank.del2_r.Read();

    cross_l_ = l;
    cross_r_ = r;

    out_l = l;
    out_r = r;
}
