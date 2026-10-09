#include "dewpoint_runtime.h"
#include <mgba/core/core.h>
#include <mgba/core/config.h>
#include <mgba/internal/gba/gba.h>
#include <cassert>
#include <cstdint>

int main()
{
    // Exercise the actual mGBA mixer with independent signals on both buses.
    for (int dmg : {0, 25, 50, 100}) {
        for (int pcm : {0, 25, 50, 100}) {
            GBAAudio audio{};
            audio.psg.style = GB_AUDIO_GBA;
            audio.psg.ch1.sample = 8;
            audio.psg.ch1Left = audio.psg.ch1Right = true;
            audio.psg.volumeLeft = audio.psg.volumeRight = 7;
            audio.volume = 2;
            audio.chALeft = audio.chBRight = true;
            audio.volumeChA = audio.volumeChB = true;
            audio.chA.samples[0] = 10;
            audio.chB.samples[0] = -6;
            audio.soundbias = 0x200;
            audio.masterVolume = 16;
            audio.dmgVolume = dmg;
            audio.pcmVolume = pcm;
            audio.sampleInterval = 512;
            GBAAudioSample(&audio, 512);
            assert(audio.currentSamples[0].left == (128 * dmg / 100 + 40 * pcm / 100) * 3);
            assert(audio.currentSamples[0].right == (128 * dmg / 100 - 24 * pcm / 100) * 3);
        }
    }
    mCore* core = mCoreCreate(mPLATFORM_GBA);
    assert(core && core->init(core));
    mCoreInitConfig(core, "sound-volume-test");
    GBA* board = static_cast<GBA*>(core->board);
    assert(board->audio.dmgVolume == 100 && board->audio.pcmVolume == 100);
    board->audio.dmgVolume = 23;
    board->audio.pcmVolume = 67;
    core->reset(core);
    assert(board->audio.dmgVolume == 23 && board->audio.pcmVolume == 67);
    mCoreConfigDeinit(&core->config);
    core->deinit(core);

    mGBAHelper gba;
    DewpointRuntime runtime(gba);
    assert(runtime.readRegister(23) == 100 && runtime.readRegister(24) == 100);
    runtime.writeRegister(21, 50);
    assert(runtime.readRegister(21) == UINT32_MAX);
    int calls = 0, savedDmg = 100, savedPcm = 100;
    bool fail = false;
    runtime.setSoundVolumeCallback([&](int dmg, int pcm) {
        ++calls;
        if (fail) return false;
        savedDmg = dmg;
        savedPcm = pcm;
        return true;
    }, 23, 67);
    // Loaded preferences are readable before any setter is called.
    assert(runtime.readRegister(23) == 23 && runtime.readRegister(24) == 67);
    assert(calls == 0);
    // Getter registers are read-only.
    runtime.writeRegister(23, 80);
    runtime.writeRegister(24, 90);
    assert(runtime.readRegister(23) == 23 && runtime.readRegister(24) == 67);
    assert(calls == 0);
    runtime.writeRegister(21, 0);
    assert(runtime.readRegister(21) == 0 && savedDmg == 0 && savedPcm == 67);
    assert(runtime.readRegister(23) == 0 && runtime.readRegister(24) == 67);
    runtime.writeRegister(22, 50);
    assert(runtime.readRegister(22) == 50 && savedDmg == 0 && savedPcm == 50);
    runtime.writeRegister(21, 101);
    assert(runtime.readRegister(21) == UINT32_MAX && calls == 2);
    runtime.writeRegister(22, UINT32_MAX);
    assert(runtime.readRegister(22) == UINT32_MAX && calls == 2);
    assert(runtime.readRegister(23) == 0 && runtime.readRegister(24) == 50);
    fail = true;
    runtime.writeRegister(21, 80);
    assert(runtime.readRegister(21) == UINT32_MAX && savedDmg == 0);
    assert(runtime.readRegister(23) == 0 && runtime.readRegister(24) == 50);
    fail = false;
    runtime.reset();
    assert(runtime.readRegister(23) == 0 && runtime.readRegister(24) == 50);
    runtime.writeRegister(22, 100);
    assert(runtime.readRegister(22) == 100 && savedDmg == 0 && savedPcm == 100);
    assert(runtime.readRegister(23) == 0 && runtime.readRegister(24) == 100);
    assert(calls == 4);
}
