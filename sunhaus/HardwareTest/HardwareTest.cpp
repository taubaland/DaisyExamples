// # Sunhaus Hardware Test
//
// Bring-up firmware for the Sunhaus pedal on a Daisy Seed. A small unison
// oscillator so every control is audible. Pin numbers are Daisy Seed D<n>.
//
// ## Controls
// | Control          | Pin      | Function                                          |
// |------------------|----------|---------------------------------------------------|
// | Volume pot       | 15       | Output gain, 0 .. 1                               |
// | Blend pot        | 16       | Adds oscillator voices: 1 (CCW) .. 7 (CW)         |
// | Encoder          | 20 / 19  | Frequency, one semitone per detent                |
// | Menu encoder     | 17 / 18  | Cycle waveform: sine, square, saw, triangle       |
// |                  |          | (hold the momentary switch: octave down/up, -3..+3) |
// | Menu enc. switch | 10       | Toggle mute                                       |
// | Momentary switch | 13       | Hold = shift for the menu encoder; LED solid on   |
// | LED              | 21       | Lit for 500 ms after any control changes,         |
// |                  |          | otherwise short 1 Hz heartbeat                    |
//
// Extra voices are detuned a few cents either side of the base note, so
// turning blend up thickens the sound.
//
// Serial log (USB, 115200, non-blocking) prints the control state and audio
// peak levels at 5 Hz.

#include "daisy_sunhaus.h"
#include "daisysp.h"

using namespace daisy;
using namespace daisysp;

DaisySunhaus hw;

static constexpr int kMaxVoices = 7;
static constexpr int kNumWaves  = 4;
static constexpr int kMinOctave = -3;
static constexpr int kMaxOctave = 3;
static constexpr int kMinNote   = 24; // C1
static constexpr int kMaxNote   = 96; // C7

static const uint8_t kWaves[kNumWaves] = {Oscillator::WAVE_SIN,
                                          Oscillator::WAVE_POLYBLEP_SQUARE,
                                          Oscillator::WAVE_POLYBLEP_SAW,
                                          Oscillator::WAVE_TRI};
static const char*   kWaveNames[kNumWaves]
    = {"sine", "square", "saw", "triangle"};

// Detune of each voice in cents: base, then alternating up/down
static const float kDetuneCents[kMaxVoices]
    = {0.f, 7.f, -7.f, 14.f, -14.f, 21.f, -21.f};

static Oscillator osc[kMaxVoices];

// Shared between main loop and audio callback
static volatile float g_gain   = 0.f;
static volatile float g_blend  = 0.f;
static volatile bool  g_mute   = false;

// Audio diagnostics (written by the audio callback, read by the log)
static volatile uint32_t g_cb_count = 0;
static volatile float    g_in_peak[2];
static volatile float    g_out_peak[2];

static float MidiToHz(float note)
{
    return mtof(note);
}

// Retune every voice for the given base note and octave offset
static void SetPitch(int note, int octave)
{
    const float n = note + 12.f * octave;
    for(int v = 0; v < kMaxVoices; v++)
        osc[v].SetFreq(MidiToHz(n + kDetuneCents[v] / 100.f));
}

static void AudioCallback(AudioHandle::InputBuffer  in,
                          AudioHandle::OutputBuffer out,
                          size_t                    size)
{
    const float gain  = g_mute ? 0.f : g_gain;
    // blend 0..1 -> 1..7 voices; a voice fades in over one "step" of blend
    const float voices = 1.f + g_blend * (kMaxVoices - 1);
    float       w[kMaxVoices];
    float       wsum = 0.f;
    for(int v = 0; v < kMaxVoices; v++)
    {
        float x = voices - v; // v-th voice weight: 0 below, 1 above
        w[v]    = x < 0.f ? 0.f : (x > 1.f ? 1.f : x);
        wsum += w[v];
    }
    const float norm = 0.35f / sqrtf(wsum); // keep level roughly constant

    float ipk[2] = {0.f, 0.f};
    float opk[2] = {0.f, 0.f};
    for(size_t i = 0; i < size; i++)
    {
        float s = 0.f;
        for(int v = 0; v < kMaxVoices; v++)
        {
            float o = osc[v].Process(); // always run, keeps phases consistent
            s += w[v] * o;
        }
        s *= norm * gain;
        out[0][i] = s;
        out[1][i] = s;

        ipk[0] = fmaxf(ipk[0], fabsf(in[0][i]));
        ipk[1] = fmaxf(ipk[1], fabsf(in[1][i]));
        opk[0] = fmaxf(opk[0], fabsf(s));
        opk[1] = opk[0];
    }
    // Peak hold, cleared by the log reader
    for(int c = 0; c < 2; c++)
    {
        if(ipk[c] > g_in_peak[c])
            g_in_peak[c] = ipk[c];
        if(opk[c] > g_out_peak[c])
            g_out_peak[c] = opk[c];
    }
    g_cb_count = g_cb_count + 1;
}

int main(void)
{
    hw.Init();
    hw.seed.StartLog(false);

    int      note        = 57; // A3
    int      octave      = 0;
    int      wave_idx    = 0;
    int32_t  enc_count   = 0; // net detents, for the log
    int32_t  menu_count  = 0;
    uint32_t press_count = 0;
    uint32_t mom_count   = 0;
    uint32_t last_activity = 0;
    uint32_t last_log      = 0;
    float    last_gain = 0.f, last_blend = 0.f;

    for(int v = 0; v < kMaxVoices; v++)
    {
        osc[v].Init(hw.AudioSampleRate());
        osc[v].SetAmp(1.f);
        osc[v].SetWaveform(kWaves[wave_idx]);
    }
    SetPitch(note, octave);

    hw.StartAdc();
    hw.StartAudio(AudioCallback);

    while(1)
    {
        const uint32_t now = System::GetNow();

        hw.ProcessAllControls();

        // --- Pots ---
        float gain  = hw.GetKnobValue(DaisySunhaus::KNOB_VOLUME);
        float blend = hw.GetKnobValue(DaisySunhaus::KNOB_CLEAN_BLEND);
        g_gain      = gain;
        g_blend     = blend;
        if(fabsf(gain - last_gain) > 0.02f || fabsf(blend - last_blend) > 0.02f)
        {
            last_gain     = gain;
            last_blend    = blend;
            last_activity = now;
        }

        // --- Main encoder: frequency, semitone steps ---
        int dir = hw.GetEncoderIncrement(DaisySunhaus::ENC_1);
        if(dir != 0)
        {
            note += dir;
            if(note < kMinNote)
                note = kMinNote;
            if(note > kMaxNote)
                note = kMaxNote;
            SetPitch(note, octave);
            enc_count += dir;
            last_activity = now;
        }

        // --- Menu encoder: waveform (octave while momentary is held) ---
        int inc = hw.GetEncoderIncrement(DaisySunhaus::ENC_MENU);
        if(inc != 0)
        {
            if(hw.MomentaryPressed())
            {
                octave += inc;
                if(octave < kMinOctave)
                    octave = kMinOctave;
                if(octave > kMaxOctave)
                    octave = kMaxOctave;
                SetPitch(note, octave);
            }
            else
            {
                wave_idx = (wave_idx + (inc > 0 ? 1 : kNumWaves - 1)) % kNumWaves;
                for(int v = 0; v < kMaxVoices; v++)
                    osc[v].SetWaveform(kWaves[wave_idx]);
            }
            menu_count += inc;
            last_activity = now;
        }

        // --- Menu switch: toggle mute ---
        if(hw.MenuSwitchRisingEdge())
        {
            g_mute = !g_mute;
            press_count++;
            last_activity = now;
        }

        // --- Momentary switch ---
        if(hw.MomentaryRisingEdge())
        {
            mom_count++;
            last_activity = now;
        }

        // --- LED ---
        bool on = (hw.MomentaryPressed() || (now - last_activity < 500))
                      ? true
                      : ((now % 1000) < 100);
        hw.SetLed(on);

        // --- Log ---
        if(now - last_log >= 200)
        {
            last_log = now;
            const float n = note + 12.f * octave;
            hw.seed.PrintLine(
                "gain=%d blend=%d voices=%d note=%d oct=%d hz=%d wave=%s mute=%d "
                "enc=%ld menu=%ld press=%lu mom=%lu encAB=%d%d cb=%lu "
                "inL=%d inR=%d out=%d",
                (int)(gain * 1000),
                (int)(blend * 1000),
                1 + (int)(blend * (kMaxVoices - 1) + 0.5f),
                note,
                octave,
                (int)MidiToHz(n),
                kWaveNames[wave_idx],
                (int)g_mute,
                (long)enc_count,
                (long)menu_count,
                (unsigned long)press_count,
                (unsigned long)mom_count,
                (int)hw.enc_main.RawA(),
                (int)hw.enc_main.RawB(),
                (unsigned long)g_cb_count,
                (int)(g_in_peak[0] * 1000),
                (int)(g_in_peak[1] * 1000),
                (int)(g_out_peak[0] * 1000));
            g_in_peak[0] = g_in_peak[1] = g_out_peak[0] = g_out_peak[1] = 0.f;
        }

        System::Delay(1);
    }
}
