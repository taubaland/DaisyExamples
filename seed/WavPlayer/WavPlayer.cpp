// # WavPlayer
// ## Description
// Simple WAV file player for DaisyPod.
// Scans the SD Card root for .wav files and streams them.
// Use the encoder to navigate between files.
// Button 1: Restart current file
// Button 2: Toggle looping
//
#include <stdio.h>
#include <string.h>
#include "daisy_pod.h"

using namespace daisy;

static constexpr size_t kTransferSize  = 16384;
static constexpr size_t kMaxWavFiles   = 32;

static DaisyPod                  hw;
static SdmmcHandler              sdcard;
static FatFSInterface            fsi;
static WavPlayer<kTransferSize>  player;
static FileTable<kMaxWavFiles>   file_table;

static size_t current_file_idx = 0;

// Open a WAV file by index from the file table
static void OpenFile(size_t idx)
{
    if(file_table.GetNumFiles() == 0)
        return;
    if(idx >= file_table.GetNumFiles())
        idx = file_table.GetNumFiles() - 1;
    current_file_idx = idx;
    player.Open(file_table.GetFileName(current_file_idx));
    player.SetPlaying(true);
}

void AudioCallback(AudioHandle::InterleavingInputBuffer  in,
                   AudioHandle::InterleavingOutputBuffer out,
                   size_t                                size)
{
    hw.ProcessDigitalControls();

    // Change file with encoder
    int32_t inc = hw.encoder.Increment();
    if(inc > 0 && current_file_idx < file_table.GetNumFiles() - 1)
        OpenFile(current_file_idx + 1);
    else if(inc < 0 && current_file_idx > 0)
        OpenFile(current_file_idx - 1);

    // Button 1: restart current file
    if(hw.button1.RisingEdge())
        player.Restart();

    // Button 2: toggle looping
    if(hw.button2.RisingEdge())
        player.SetLooping(!player.GetLooping());

    for(size_t i = 0; i < size; i += 2)
    {
        float samps[2] = {0.f, 0.f};
        player.Stream(samps, 2);
        out[i]     = samps[0] * 0.5f;
        out[i + 1] = samps[1] * 0.5f;
    }
}

int main(void)
{
    hw.Init();

    SdmmcHandler::Config sd_cfg;
    sd_cfg.Defaults();
    sdcard.Init(sd_cfg);
    fsi.Init(FatFSInterface::Config::MEDIA_SD);
    f_mount(&fsi.GetSDFileSystem(), "/", 1);

    // Scan SD root for WAV files
    file_table.Fill("/", ".wav");

    if(file_table.GetNumFiles() > 0)
    {
        player.Init(file_table.GetFileName(0));
        player.SetLooping(true);
        player.SetPlaying(true);
    }

    hw.SetAudioBlockSize(4);
    hw.StartAudio(AudioCallback);

    for(;;)
    {
        // Perform disk I/O to keep the streaming buffer full
        player.Prepare();
    }
}
