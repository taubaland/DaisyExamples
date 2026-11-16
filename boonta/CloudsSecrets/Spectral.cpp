#include "Spectral.h"

#include <math.h>
#include <string.h>

#include "dev/sdram.h"

namespace
{
constexpr int      N    = Spectral::kFftSize;
constexpr int      K    = Spectral::kBins;
constexpr int      HOP  = Spectral::kHop;
constexpr int      S    = Spectral::kSlots;
constexpr uint32_t RING = 4096;
constexpr uint32_t RMASK = RING - 1;

constexpr float kPi    = 3.14159265358979f;
constexpr float kTwoPi = 6.28318530717959f;

/** Everything big lives in SDRAM: snapshot slots, the per-bin state that
 *  survives from one frame to the next, and the two ring buffers. Roughly a
 *  third of a megabyte, so internal RAM is out of the question under the
 *  bootloader's shrunken layout. File scope for the same reason as the
 *  recording buffer in Effect.cpp. */
struct Store
{
    float slot_m[S][2][K];
    float slot_f[S][2][K];

    float prev_phase[2][K];  /**< analysis phase of the previous frame */
    float synth_phase[2][K]; /**< running synthesis phase */
    float held_m[2][K];      /**< what density has kept from frame to frame */
    float held_f[2][K];

    float an_m[2][K]; /**< this frame, freshly analysed */
    float an_f[2][K];
    float a_m[2][K]; /**< scratch for the remapping stages */
    float a_f[2][K];
    float b_m[2][K];
    float b_f[2][K];

    float syn_re[2][K]; /**< the frame being built, before it is packed */
    float syn_im[2][K];

    float fft_re[N]; /**< the transform's working arrays */
    float fft_im[N];

    float ring_in[2][RING];
    float ring_out[2][RING];
};

Store DSY_SDRAM_BSS g_store;

/** The transform's arrays are in the store too, behind pointers. Internal RAM
 *  would be nicer for the hottest loop here, but under the bootloader's
 *  layout the stack shares it with everything else and a first build of
 *  this ran it down to a few kilobytes. The tables below, ten kilobytes,
 *  stay in internal RAM. */
float* const g_re = g_store.fft_re;
float* const g_im = g_store.fft_im;

/** Tables, which are read-only after Init() and small enough to live in
 *  internal RAM. */
float    g_cos[N / 2];
float    g_sin[N / 2];
float    g_win[N];
uint16_t g_rev[N];
bool     g_tables_ready = false;

void BuildTables()
{
    for(int i = 0; i < N / 2; i++)
    {
        g_cos[i] = cosf(kTwoPi * static_cast<float>(i) / N);
        g_sin[i] = sinf(kTwoPi * static_cast<float>(i) / N);
    }

    // Periodic Hann. At four-fold overlap the squares of two of them sum to a
    // constant 1.5, which is what the output scale at the end of StageIfft()
    // divides out.
    for(int i = 0; i < N; i++)
        g_win[i] = 0.5f - 0.5f * cosf(kTwoPi * static_cast<float>(i) / N);

    int bits = 0;
    while((1 << bits) < N)
        bits++;

    for(int i = 0; i < N; i++)
    {
        int r = 0;
        for(int b = 0; b < bits; b++)
            if(i & (1 << b))
                r |= 1 << (bits - 1 - b);
        g_rev[i] = static_cast<uint16_t>(r);
    }

    g_tables_ready = true;
}

/** In-place radix-2 forward transform. The inverse is the same call with the
 *  two arrays swapped, which exchanges real and imaginary parts on the way in
 *  and out; that is a conjugate in disguise and the 1/N is the caller's. */
void Fft(float* re, float* im)
{
    for(int i = 0; i < N; i++)
    {
        const int j = g_rev[i];
        if(j > i)
        {
            const float tr = re[i];
            re[i]          = re[j];
            re[j]          = tr;
            const float ti = im[i];
            im[i]          = im[j];
            im[j]          = ti;
        }
    }

    for(int len = 2; len <= N; len <<= 1)
    {
        const int half = len >> 1;
        const int step = N / len;

        for(int i = 0; i < N; i += len)
        {
            for(int j = 0; j < half; j++)
            {
                const float wr = g_cos[j * step];
                const float wi = -g_sin[j * step];

                const int   a  = i + j;
                const int   b  = a + half;
                const float xr = re[b] * wr - im[b] * wi;
                const float xi = re[b] * wi + im[b] * wr;

                re[b] = re[a] - xr;
                im[b] = im[a] - xi;
                re[a] += xr;
                im[a] += xi;
            }
        }
    }
}

// --- cheap transcendentals ------------------------------------------------
//
// A frame touches about two thousand bins, and libm's sinf and atan2f at a
// hundred-odd cycles each would be most of the frame. These are good to about
// 1e-4, which is far below anything the mangling stages do to the signal.

/** sin for x in [-pi, pi]. Folded to +-pi/2, then a Taylor series that is
 *  accurate to 2e-4 at its worst, at the fold. */
inline float FastSin(float x)
{
    if(x > 1.5707963f)
        x = kPi - x;
    else if(x < -1.5707963f)
        x = -kPi - x;

    const float x2 = x * x;
    return x
           * (1.f
              + x2 * (-0.16666667f + x2 * (0.0083333333f - x2 * 0.00019841270f)));
}

/** Wrap to [-pi, pi]. */
inline float Wrap(float x)
{
    return x - kTwoPi * roundf(x * (1.f / kTwoPi));
}

inline void SinCos(float x, float& s, float& c)
{
    s = FastSin(x);

    float y = x + 1.5707963f;
    if(y > kPi)
        y -= kTwoPi;
    c = FastSin(y);
}

inline float FastAtan2(float y, float x)
{
    const float ax = fabsf(x);
    const float ay = fabsf(y);
    const float mx = ax > ay ? ax : ay;
    const float mn = ax > ay ? ay : ax;

    if(mx < 1e-20f)
        return 0.f;

    const float a = mn / mx;
    const float s = a * a;
    float       r = ((-0.0464964749f * s + 0.15931422f) * s - 0.327622764f) * s * a + a;

    if(ay > ax)
        r = 1.5707963f - r;
    if(x < 0.f)
        r = kPi - r;
    if(y < 0.f)
        r = -r;
    return r;
}

union FloatBits
{
    float    f;
    uint32_t u;
};

/** log2, good to a few thousandths of an octave. x must be positive. */
inline float FastLog2(float x)
{
    FloatBits v;
    v.f = x;
    const int e = static_cast<int>((v.u >> 23) & 255u) - 127;

    FloatBits m;
    m.u = (v.u & 0x007FFFFFu) | 0x3F800000u;

    const float p = (-0.34484843f * m.f + 2.02466578f) * m.f - 0.67487759f;
    return static_cast<float>(e) + p;
}

inline float FastExp2(float x)
{
    if(x < -60.f)
        return 0.f;
    if(x > 60.f)
        x = 60.f;

    const float fl = floorf(x);
    const float f  = x - fl;

    const float p
        = 1.f + f * (0.69314718f + f * (0.24022651f + f * (0.05550411f + f * 0.00961813f)));

    FloatBits v;
    v.u = static_cast<uint32_t>(static_cast<int>(fl) + 127) << 23;
    return p * v.f;
}

inline float Lerp(float a, float b, float t)
{
    return a + (b - a) * t;
}

/** Bin k's own frequency, radians per sample. */
inline float Omega(int k)
{
    return kTwoPi * static_cast<float>(k) / N;
}
} // namespace

void Spectral::Init()
{
    if(!g_tables_ready)
        BuildTables();

    memset(&g_store, 0, sizeof(g_store));

    write_count_ = 0;
    next_frame_  = N; // the first frame is the first full window
    frame_end_   = 0;
    stage_       = STAGE_IDLE;
    have_prev_   = false;
    lock_        = false;
    params_      = Params();
    job_         = Params();
}

uint32_t Spectral::Rng()
{
    rng_ ^= rng_ << 13;
    rng_ ^= rng_ >> 17;
    rng_ ^= rng_ << 5;
    return rng_;
}

float Spectral::RandF()
{
    return static_cast<float>(Rng() >> 8) * (1.f / 16777216.f);
}

void Spectral::Tick(float in_l, float in_r, float& out_l, float& out_r)
{
    g_store.ring_in[0][write_count_ & RMASK] = in_l;
    g_store.ring_in[1][write_count_ & RMASK] = in_r;

    // Read the sample that is kLatency old and clear it behind us, so the
    // overlap-add in StageIfft() always accumulates into silence.
    const uint32_t r = (write_count_ - static_cast<uint32_t>(kLatency)) & RMASK;
    out_l            = g_store.ring_out[0][r];
    out_r            = g_store.ring_out[1][r];
    g_store.ring_out[0][r] = 0.f;
    g_store.ring_out[1][r] = 0.f;

    write_count_++;
}

void Spectral::Service(size_t block)
{
    // Stages per call: enough that a block bigger than a hop still finishes a
    // frame per hop. At the pedal's 48-sample block this is one.
    const size_t per_call
        = (static_cast<size_t>(STAGE_LAST) * block + HOP - 1) / HOP;
    const size_t calls = per_call < 1 ? 1 : per_call;

    for(size_t n = 0; n < calls; n++)
    {
        if(stage_ == STAGE_IDLE)
        {
            // Not before the window has finished arriving, and not unless the
            // previous frame is done; being late is harmless because the
            // frame's place in time comes from next_frame_, not from now.
            if(write_count_ < next_frame_)
                return;

            frame_end_  = next_frame_;
            next_frame_ += HOP;
            job_        = params_;
            stage_      = STAGE_FFT;
        }

        RunStage();
    }
}

void Spectral::RunStage()
{
    switch(stage_)
    {
        case STAGE_FFT: StageFft(); break;
        case STAGE_ANALYSE: StageAnalyse(); break;
        case STAGE_MANGLE: StageMangle(); break;
        case STAGE_SYNTH: StageSynth(); break;
        case STAGE_IFFT: StageIfft(); break;
        default: break;
    }

    stage_++;
    if(stage_ >= STAGE_LAST)
        stage_ = STAGE_IDLE;
}

void Spectral::StageFft()
{
    // Frozen frames come from the snapshot slots, so there is nothing to
    // transform and the analysis memory goes stale.
    if(job_.frozen)
    {
        have_prev_ = false;
        return;
    }

    const uint32_t start = frame_end_ - static_cast<uint32_t>(N);
    for(int j = 0; j < N; j++)
    {
        const uint32_t idx = (start + static_cast<uint32_t>(j)) & RMASK;
        g_re[j]            = g_store.ring_in[0][idx] * g_win[j];
        g_im[j]            = g_store.ring_in[1][idx] * g_win[j];
    }

    Fft(g_re, g_im);
}

void Spectral::StageAnalyse()
{
    if(job_.frozen)
        return;

    // Two real signals went through one complex transform: left as the real
    // part, right as the imaginary part. Pull them apart using the symmetry
    // of the spectrum of a real signal.
    for(int k = 0; k < K; k++)
    {
        const int   nk = (N - k) & (N - 1);
        const float xr = g_re[k], xi = g_im[k];
        const float nr = g_re[nk], ni = g_im[nk];

        const float lr = 0.5f * (xr + nr);
        const float li = 0.5f * (xi - ni);
        const float rr = 0.5f * (xi + ni);
        const float ri = -0.5f * (xr - nr);

        const float re_[2] = {lr, rr};
        const float im_[2] = {li, ri};

        for(int c = 0; c < 2; c++)
        {
            const float mag   = sqrtf(re_[c] * re_[c] + im_[c] * im_[c]);
            const float phase = FastAtan2(im_[c], re_[c]);
            const float om    = Omega(k);

            float f = om;
            if(have_prev_)
            {
                const float d = Wrap(phase - g_store.prev_phase[c][k] - om * HOP);
                f             = om + d * (1.f / HOP);
            }

            g_store.prev_phase[c][k] = phase;
            g_store.an_m[c][k]       = mag;
            g_store.an_f[c][k]       = f;
        }
    }

    have_prev_ = true;

    // POSITION picks which slot this frame is kept in.
    float pos = job_.position;
    pos       = pos < 0.f ? 0.f : (pos > 1.f ? 1.f : pos);
    const int slot = static_cast<int>(pos * (S - 1) + 0.5f);

    memcpy(g_store.slot_m[slot], g_store.an_m, sizeof(g_store.an_m));
    memcpy(g_store.slot_f[slot], g_store.an_f, sizeof(g_store.an_f));
}

void Spectral::StageMangle()
{
    // --- which frame ----------------------------------------------------
    //
    // Live, unless frozen, in which case a point between two stored slots.
    // a_* is the working frame from here on; the remapping stages go back and
    // forth between a_* and b_*.
    float(*m)[K] = g_store.a_m;
    float(*f)[K] = g_store.a_f;
    float(*om)[K] = g_store.b_m;
    float(*of)[K] = g_store.b_f;

    if(!job_.frozen)
    {
        memcpy(m, g_store.an_m, sizeof(g_store.an_m));
        memcpy(f, g_store.an_f, sizeof(g_store.an_f));
    }
    else
    {
        float pos = job_.position;
        pos       = pos < 0.f ? 0.f : (pos > 1.f ? 1.f : pos);
        const float u  = pos * (S - 1);
        int         i0 = static_cast<int>(u);
        if(i0 > S - 2)
            i0 = S - 2;
        const float fr = u - static_cast<float>(i0);

        for(int c = 0; c < 2; c++)
        {
            for(int k = 0; k < K; k++)
            {
                m[c][k] = Lerp(g_store.slot_m[i0][c][k],
                               g_store.slot_m[i0 + 1][c][k],
                               fr);
                f[c][k] = Lerp(g_store.slot_f[i0][c][k],
                               g_store.slot_f[i0 + 1][c][k],
                               fr);
            }
        }
    }

    // --- SIZE: slide the spectrum up, or fold it over -------------------
    const float t = (job_.size - 0.5f) * 2.f;

    if(t < -0.02f)
    {
        const int shift = static_cast<int>(-t * (K / 2));
        const float shift_f = static_cast<float>(shift) * Omega(1);

        for(int c = 0; c < 2; c++)
        {
            for(int k = 0; k < K; k++)
            {
                const int src = k - shift;
                if(src < 0)
                {
                    om[c][k] = 0.f;
                    of[c][k] = Omega(k);
                }
                else
                {
                    om[c][k] = m[c][src];
                    of[c][k] = f[c][src] + shift_f;
                }
            }
        }

        float(*tm)[K] = m;
        m             = om;
        om            = tm;
        float(*tf)[K] = f;
        f             = of;
        of            = tf;
    }
    else if(t > 0.02f)
    {
        // Blend the spectrum with its mirror image. The mirrored levels take
        // each bin's own centre frequency: they came from somewhere else in
        // the spectrum and carry no phase history that fits here.
        for(int c = 0; c < 2; c++)
        {
            for(int k = 0; k < K; k++)
            {
                const int   rk = K - 1 - k;
                const float a  = m[c][k] * (1.f - t);
                const float b  = m[c][rk] * t;

                om[c][k] = a + b;
                of[c][k] = (a >= b) ? f[c][k] : Omega(k);
            }
        }

        float(*tm)[K] = m;
        m             = om;
        om            = tm;
        float(*tf)[K] = f;
        f             = of;
        of            = tf;
    }

    // --- PITCH: resample the bins ---------------------------------------
    const float ratio = job_.ratio;
    if(fabsf(ratio - 1.f) > 1e-3f)
    {
        const float inv = 1.f / ratio;

        for(int c = 0; c < 2; c++)
        {
            for(int k = 0; k < K; k++)
            {
                const float sp = static_cast<float>(k) * inv;
                const int   i  = static_cast<int>(sp);

                if(i >= K - 1)
                {
                    om[c][k] = 0.f;
                    of[c][k] = Omega(k);
                    continue;
                }

                const float fr = sp - static_cast<float>(i);
                om[c][k]       = Lerp(m[c][i], m[c][i + 1], fr);
                of[c][k]       = Lerp(f[c][i], f[c][i + 1], fr) * ratio;
            }
        }

        float(*tm)[K] = m;
        m             = om;
        om            = tm;
        float(*tf)[K] = f;
        f             = of;
        of            = tf;
    }

    // --- DENSITY: what carries over from one frame to the next ----------
    const float d = job_.density;

    // With nothing remapped, nothing frozen and nothing carried over, the
    // frame is the analysis frame and its own phases are the right ones.
    // Using them exactly, rather than re-deriving them from frequencies, is
    // what makes a neutral setting transparent instead of slightly phasey.
    lock_ = !job_.frozen && fabsf(t) <= 0.02f && fabsf(ratio - 1.f) <= 1e-3f
            && fabsf(d - 0.5f) <= 0.02f;

    const float skip_p = d < 0.5f ? (0.5f - d) * 2.f * 0.98f : 0.f;
    const float keep   = d > 0.5f ? (d - 0.5f) * 2.f * 0.92f : 0.f;

    for(int c = 0; c < 2; c++)
    {
        for(int k = 0; k < K; k++)
        {
            if(skip_p > 0.f && RandF() < skip_p)
                continue; // this bin keeps what it had

            g_store.held_m[c][k]
                = g_store.held_m[c][k] * keep + m[c][k] * (1.f - keep);
            g_store.held_f[c][k] = f[c][k];
        }
    }
}

void Spectral::StageSynth()
{
    const float tex = job_.texture;

    // Random phase kicks, only past 90% density.
    const float jitter
        = job_.density > 0.9f ? (job_.density - 0.9f) * 10.f * kPi : 0.f;

    // In the store, not on the stack or in internal RAM: eight kilobytes is a
    // lot to ask of either.
    float(*re_)[K] = g_store.syn_re;
    float(*im_)[K] = g_store.syn_im;

    for(int c = 0; c < 2; c++)
    {
        float* fm = g_store.b_m[c]; // the frame as it will sound
        memcpy(fm, g_store.held_m[c], sizeof(float) * K);

        float peak = 0.f;
        for(int k = 0; k < K; k++)
            if(fm[k] > peak)
                peak = fm[k];

        // --- TEXTURE ----------------------------------------------------
        if(tex < 0.48f && peak > 1e-3f)
        {
            // Quantise levels in the log domain: coarser as the knob goes
            // left, like a codec starved of bits.
            const float step = (0.5f - tex) * 2.f * 4.f; // octaves per step
            for(int k = 0; k < K; k++)
            {
                if(fm[k] < peak * 1e-4f)
                {
                    fm[k] = 0.f;
                    continue;
                }
                const float lm = FastLog2(fm[k]);
                fm[k]          = FastExp2(roundf(lm / step) * step);
            }
        }
        else if(tex > 0.52f && peak > 1e-3f)
        {
            // Flatten: raise each level's distance from the peak to a power
            // below one, so the strongest partials come down and the weakest
            // come up. A floor keeps silence from being boosted into hiss.
            //
            // Raising the quiet bins adds a great deal of energy, so the
            // frame is rescaled to carry what it carried before: the knob
            // changes the spectrum's shape, not the pedal's level.
            const float e = 1.f - (tex - 0.5f) * 2.f * 0.85f;

            float before = 0.f;
            float after  = 0.f;
            for(int k = 0; k < K; k++)
            {
                before += fm[k] * fm[k];

                const float rel = fm[k] / peak;
                if(rel < 1e-4f)
                {
                    fm[k] = 0.f;
                    continue;
                }
                fm[k] = peak * FastExp2(e * FastLog2(rel));
                after += fm[k] * fm[k];
            }

            if(after > 0.f)
            {
                const float g = sqrtf(before / after);
                for(int k = 0; k < K; k++)
                    fm[k] *= g;
            }
        }

        // --- phase accumulation and polar to rectangular ---------------
        for(int k = 0; k < K; k++)
        {
            float ph;
            if(lock_)
            {
                // This frame's own phase. It is also left in the accumulator,
                // so that a knob moved off neutral carries on from phases
                // that are right rather than from zero.
                ph = g_store.prev_phase[c][k];
            }
            else
            {
                ph = g_store.synth_phase[c][k] + g_store.held_f[c][k] * HOP;
            }
            if(jitter > 0.f)
                ph += jitter * (RandF() * 2.f - 1.f);
            ph = Wrap(ph);
            g_store.synth_phase[c][k] = ph;

            float s, co;
            SinCos(ph, s, co);
            re_[c][k] = fm[k] * co;
            im_[c][k] = fm[k] * s;
        }

        // DC and Nyquist have to be real for the output to be, and DC
        // is better left out of a pedal's output anyway.
        re_[c][0]     = 0.f;
        im_[c][0]     = 0.f;
        im_[c][K - 1] = 0.f;
    }

    // Pack: Y = L + iR, with each of L and R extended to the negative
    // frequencies by conjugate symmetry.
    for(int k = 0; k < K; k++)
    {
        const float lr = re_[0][k], li = im_[0][k];
        const float rr = re_[1][k], ri = im_[1][k];

        g_re[k] = lr - ri;
        g_im[k] = li + rr;

        if(k != 0 && k != K - 1)
        {
            g_re[N - k] = lr + ri;
            g_im[N - k] = rr - li;
        }
    }
}

void Spectral::StageIfft()
{
    // Inverse: same transform with the arrays swapped.
    Fft(g_im, g_re);

    // 1/N for the transform, 1/1.5 for the squared-Hann overlap.
    constexpr float scale = 1.f / (N * 1.5f);

    const uint32_t start = frame_end_ - static_cast<uint32_t>(N);
    for(int j = 0; j < N; j++)
    {
        const uint32_t idx = (start + static_cast<uint32_t>(j)) & RMASK;
        const float    w   = g_win[j] * scale;

        g_store.ring_out[0][idx] += g_re[j] * w;
        g_store.ring_out[1][idx] += g_im[j] * w;
    }
}
