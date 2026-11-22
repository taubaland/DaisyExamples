// # Djin, Skips and Fragments
//
// Sunhaus pedal firmware on a Daisy Seed: six "scripts" behind one shared
// front end. Script One (a cut-and-paste looper after the Montreal Assembly
// Count To Five) is written; Scripts 2-6 are pass-through placeholders.
//
// ## Controls
// | Control          | Pin     | Function                                       |
// |------------------|---------|------------------------------------------------|
// | Volume pot       | 15      | Output level of the effect                     |
// | Clean/Blend pot  | 16      | Clean (CCW) to fully processed (CW)            |
// | Main encoder     | 20 / 19 | Script specific (Script One: pitch, direction, |
// |                  |         | randomness)                                    |
// | Menu encoder     | 17 / 18 | Menu: choose the effect, 1-6 wrapping;         |
// |                  |         | in an effect: Length                           |
// | Menu enc. switch | 10      | Enter the menu / enter the shown effect /      |
// |                  |         | leave the effect, back to the menu             |
// | Footswitch       | 13      | Effect on / off while an effect runs           |
// | LED              | 21      | Status (software PWM; D21 has no timer)        |
//
// ## States
// SLEEP   Power-on state (unless kBootIntoEffect starts Effect 1 directly). The LED breathes (max 80%), audio is silent and
//         the encoders do nothing. Tap the menu switch: enter the menu.
// MENU    Audio silent. The LED is off and flashes to 100% N times every 3 s for
//         the shown effect N. Turn the menu encoder to pick one (1-6, wrapping);
//         tap the menu switch to enter it.
// RUN     The LED is lit at 40% and flashes to 100% N times every 3 s for effect
//         N. Menu encoder = Length, Main encoder = effect specific. Footswitch:
//         effect on / off (LED dims). Tap the menu switch: fade out and go back
//         to the menu.
//
// Output chain: input -> mono/stereo conform -> script -> clean/blend mix ->
// tape emulation -> compressor -> limiter (-0.2 dB) -> volume.

#include "daisy_sunhaus.h"
#include "daisysp.h"
#include "djin_fx.h"
#include "djin_input.h"
#include "djin_script.h"
#include "script_one.h"

using namespace daisy;

float DSY_SDRAM_BSS djin::ScriptOne::buf_[2][djin::ScriptOne::kBufLen];

DaisySunhaus hw;

static constexpr int kNumScripts = 6;
// Steady LED level in the menu and inside an effect; both flash to 100% N times
// every 3 s for effect N.
static constexpr float kMenuBase = 0.0f;
static constexpr float kRunBase  = 0.4f;
// Flicker the LED on every slice as well (off: only the effect-number blinks).
static constexpr bool   kGrainFlash = false;
static constexpr size_t kMaxBlock   = 128;
// The pedal's analog input stage (22k / 10k divider) takes about 10 dB off, and a
// guitar lands near -36 dBFS, so restore the level digitally. The limiter in the
// chain catches the peaks.
static constexpr float kInputGain = 6.f;
// Development convenience: start straight in Effect 1 instead of Sleep, so a
// reset (for example connecting the ST-Link) does not silence the pedal. Set to
// false for the power-on Sleep behaviour in the spec.
static constexpr bool kBootIntoEffect = true;
// Codec self-check: a live input is never exactly zero (an open jack still shows
// noise), so seconds of all-zero samples means the codec has stopped delivering.
// Recover by pulsing its reset and restarting the audio.
static constexpr float kInputDeadSeconds = 3.f;

static djin::ScriptOne         g_script_one;
static djin::PassthroughScript g_placeholder[kNumScripts - 1];
static djin::Script*           g_scripts[kNumScripts];

static djin::InputConform g_input;
static djin::TapeEmu      g_tape;
static djin::Compressor   g_comp;
static djin::Limiter      g_limiter;

// Main loop -> audio callback
static volatile djin::Script* g_active = nullptr;
static volatile bool          g_run    = false; // script audio on
static volatile bool          g_fx_on  = true;  // effect (vs clean bypass)
static volatile float    g_fade_s     = 0.02f; // fade-out time when g_run drops
static volatile float    g_volume     = 0.f;
static volatile float    g_blend      = 1.f;
static volatile float    g_led        = 0.f; // 0..1 LED level
static volatile uint32_t g_cb_count   = 0;
static volatile bool     g_input_dead = false;
static volatile uint32_t g_recoveries = 0;

// Software PWM for the LED (D21 has no timer channel). It must run from a real
// timebase: the audio callback executes its samples back to back in microseconds,
// so PWM driven from it would only ever show one value per millisecond.
// A 32 kHz timer interrupt gives 32 steps per 1 kHz PWM period, with first-order
// error diffusion across periods so dim levels get fine resolution.
// Level -> duty: gain, then gamma (lower gamma is brighter).
static constexpr float kLedGain   = 1.0f;
static constexpr float kLedGamma  = 1.0f;
static constexpr int   kLedSteps  = 32;
static constexpr float kLedTickHz = 32000.f;
static volatile float  g_led_duty = 0.f; // 0..1, set by the main loop
static int             g_led_k    = 0;
static int             g_led_on   = 0;
static float           g_led_err  = 0.f;
static TimerHandle     g_led_tim;

// Runs at 32 kHz: the LED PWM, plus fast sampling of both encoders (this
// board's menu encoder passes its middle state for under a millisecond).
static void FastTick(void*)
{
    hw.enc_main.Poll();
    hw.enc_menu.Poll();
    if(g_led_k == 0)
    {
        const float want = g_led_duty * kLedSteps + g_led_err;
        g_led_on         = (int)want;
        g_led_err        = want - g_led_on;
    }
    hw.led.Write(g_led_k < g_led_on);
    if(++g_led_k >= kLedSteps)
        g_led_k = 0;
}

static inline float Slew(float cur, float target, float up, float down)
{
    if(cur < target)
        return fminf(cur + up, target);
    return fmaxf(cur - down, target);
}

static void AudioCallback(AudioHandle::InputBuffer  in,
                          AudioHandle::OutputBuffer out,
                          size_t                    size)
{
    static float    xl[kMaxBlock], xr[kMaxBlock], wl[kMaxBlock], wr[kMaxBlock];
    static float    run_gain = 0.f, fx_mix = 1.f;
    static uint32_t zero_run = 0;

    if(size > kMaxBlock)
        size = kMaxBlock;
    const float    sr         = hw.AudioSampleRate();
    const uint32_t zero_limit = (uint32_t)(kInputDeadSeconds * sr);

    for(size_t i = 0; i < size; i++)
    {
        xl[i] = in[0][i] * kInputGain;
        xr[i] = in[1][i] * kInputGain;
        // Exactly-zero input on both channels, sample after sample.
        if(in[0][i] == 0.f && in[1][i] == 0.f)
        {
            if(++zero_run >= zero_limit)
            {
                g_input_dead = true;
                zero_run     = 0;
            }
        }
        else
            zero_run = 0;
    }
    g_input.Process(xl, xr, size);

    djin::Script* script  = (djin::Script*)g_active;
    const bool    run     = g_run;
    const bool    fx_on   = g_fx_on;
    const float   blend   = g_blend;
    const float   vol     = g_volume * g_volume;
    const float   up      = 1.f / (0.02f * sr);
    const float   down    = 1.f / (fmaxf(g_fade_s, 0.005f) * sr);
    const float   fx_step = 1.f / (0.02f * sr);

    const bool process = script && (run || run_gain > 0.f);
    if(process)
        script->Process(xl, xr, wl, wr, size);

    for(size_t i = 0; i < size; i++)
    {
        run_gain = Slew(run_gain, run ? 1.f : 0.f, up, down);
        fx_mix   = Slew(fx_mix, fx_on ? 1.f : 0.f, fx_step, fx_step);

        float l = 0.f, r = 0.f;
        if(process)
        {
            // Clean/blend, then the shared chain, then volume.
            float ml = xl[i] + blend * (wl[i] - xl[i]);
            float mr = xr[i] + blend * (wr[i] - xr[i]);
            g_tape.Process(ml, mr);
            g_comp.Process(ml, mr);
            g_limiter.Process(ml, mr);
            ml *= vol;
            mr *= vol;
            l = (fx_mix * ml + (1.f - fx_mix) * xl[i]) * run_gain;
            r = (fx_mix * mr + (1.f - fx_mix) * xr[i]) * run_gain;
        }
        out[0][i] = l;
        out[1][i] = r;
    }
    g_cb_count = g_cb_count + 1;
}

// --- LED patterns (3 s cycle: N blinks in the first 1.5 s, then dark) -------
static bool BlinkOn(uint32_t t_ms, int n)
{
    const uint32_t ph = t_ms % 3000;
    if(ph >= 1500)
        return false;
    const uint32_t slot = 1500 / n;
    const uint32_t on   = slot / 2 < 150 ? slot / 2 : 150;
    return (ph % slot) < on;
}

enum class Mode
{
    SLEEP,
    MENU,
    RUN
};

int main(void)
{
    hw.Init();
    hw.seed.StartLog(false);
    const float sr = hw.AudioSampleRate();

    g_scripts[0] = &g_script_one;
    for(int i = 1; i < kNumScripts; i++)
        g_scripts[i] = &g_placeholder[i - 1];
    for(int i = 0; i < kNumScripts; i++)
        g_scripts[i]->Init(sr);
    g_input.Init(sr);
    g_tape.Init(sr);
    g_comp.Init(sr);
    g_limiter.Init(sr);

    hw.enc_main.EnableFastPoll();
    hw.enc_menu.EnableFastPoll();
    hw.StartAdc();
    hw.StartAudio(AudioCallback);

    TimerHandle::Config tcfg;
    tcfg.periph     = TimerHandle::Config::Peripheral::TIM_5;
    tcfg.dir        = TimerHandle::Config::CounterDir::UP;
    tcfg.enable_irq = true;
    // The reload value is preloaded, so SetPeriod after Init would only take
    // effect after the first 32-bit wrap (about 18 s). Read the timer clock, then
    // initialise again with the real period.
    g_led_tim.Init(tcfg);
    tcfg.period = g_led_tim.GetFreq() / (uint32_t)kLedTickHz;
    g_led_tim.DeInit();
    g_led_tim.Init(tcfg);
    g_led_tim.SetCallback(FastTick);
    g_led_tim.Start();

    Mode     mode       = Mode::SLEEP;
    int      cur        = 0; // loaded script index, 0..5
    int      sel        = 0; // effect shown in the menu
    uint32_t menu_start = 0; // restarts the menu blink count
    uint32_t mode_since = System::GetNow();
    uint32_t run_since  = 0;
    bool     sw_down    = false;
    float    flash      = 0.f;
    uint32_t last_log   = 0;

    auto load = [&](int idx, bool reset_fx) {
        cur = idx;
        g_scripts[cur]->Activate();
        g_active = g_scripts[cur];
        if(reset_fx)
            g_fx_on = true;
        g_fade_s  = 0.02f;
        g_run     = true;
        mode      = Mode::RUN;
        run_since = System::GetNow();
    };
    if(kBootIntoEffect)
        load(0, true);

    while(1)
    {
        const uint32_t now = System::GetNow();
        hw.ProcessAllControls();

        g_volume = hw.GetKnobValue(DaisySunhaus::KNOB_VOLUME);
        g_blend  = hw.GetKnobValue(DaisySunhaus::KNOB_CLEAN_BLEND);

        // Codec self-check: pulse the AK4556 reset and restart the audio.
        if(g_input_dead)
        {
            g_input_dead = false;
            if(hw.seed.CheckBoardVersion()
               == DaisySeed::BoardVersion::DAISY_SEED)
                hw.seed.codec.Init(Pin(PORTB, 11));
            hw.StopAudio();
            hw.DelayMs(5);
            hw.StartAudio(AudioCallback);
            g_recoveries = g_recoveries + 1;
        }

        const int main_inc = hw.GetEncoderIncrement(DaisySunhaus::ENC_1);
        const int menu_inc = hw.GetEncoderIncrement(DaisySunhaus::ENC_MENU);

        // Menu switch: a tap, detected on release.
        bool       short_tap = false;
        const bool down      = hw.MenuSwitchPressed();
        if(down && !sw_down)
            sw_down = true;
        else if(!down && sw_down)
        {
            sw_down   = false;
            short_tap = true;
        }

        switch(mode)
        {
            case Mode::SLEEP:
                if(short_tap)
                {
                    sel        = cur; // start on the last effect used
                    menu_start = now;
                    mode       = Mode::MENU;
                }
                break;

            case Mode::MENU:
                if(menu_inc != 0)
                {
                    sel = (sel + (menu_inc > 0 ? 1 : kNumScripts - 1))
                          % kNumScripts;
                    menu_start = now; // restart the blink count from the top
                }
                if(short_tap)
                    load(sel, true); // enter the effect
                break;

            case Mode::RUN:
                if(main_inc != 0)
                    g_scripts[cur]->MainEncoder(main_inc);
                if(menu_inc != 0)
                    g_scripts[cur]->LengthEncoder(menu_inc);
                if(hw.MomentaryRisingEdge())
                    g_fx_on = !g_fx_on;
                if(g_scripts[cur]->TakeFlash())
                    flash = 1.f;
                if(short_tap)
                {
                    // Leave the effect: fade out and go back to the menu.
                    g_fade_s   = 0.5f;
                    g_run      = false;
                    sel        = cur;
                    menu_start = now;
                    mode       = Mode::MENU;
                }
                break;
        }
        if(mode != Mode::RUN)
            g_scripts[cur]->TakeFlash();

        // --- LED ---
        float led = 0.f;
        switch(mode)
        {
            case Mode::SLEEP:
            {
                const float ph = (float)((now - mode_since) % 5000) / 5000.f;
                led            = 0.8f * (0.5f - 0.5f * cosf(6.2831853f * ph));
                break;
            }
            case Mode::MENU:
                // Base level, flashing up to 100% N times per 3 s for effect N.
                led = BlinkOn(now - menu_start, sel + 1) ? 1.f : kMenuBase;
                break;
            case Mode::RUN:
            {
                // Base level while an effect runs, flashing to 100% N times for
                // effect N on entry and then every 3 s.
                const uint32_t t = now - run_since;
                if(!g_fx_on)
                    led = 0.15f; // bypassed: dim steady light
                else
                {
                    led = BlinkOn(t, cur + 1) ? 1.f : kRunBase;
                    if(kGrainFlash)
                        led = fmaxf(led, kRunBase + (1.f - kRunBase) * flash);
                }
                break;
            }
        }
        flash *= 0.96f; // about 25 ms decay at the 1 kHz loop rate
        g_led      = led;
        g_led_duty = powf(fminf(fmaxf(led * kLedGain, 0.f), 1.f), kLedGamma);

        // --- Log ---
        if(now - last_log >= 500)
        {
            last_log = now;
            hw.seed.PrintLine(
                "mode=%d script=%d fx=%d in=%d main=%d len=%d cb=%lu rec=%lu",
                (int)mode,
                cur + 1,
                (int)g_fx_on,
                (int)g_input.GetMode(),
                cur == 0 ? g_script_one.MainPos() : 0,
                cur == 0 ? g_script_one.LenStep() : 0,
                (unsigned long)g_cb_count,
                (unsigned long)g_recoveries);
        }

        System::Delay(1);
    }
}
