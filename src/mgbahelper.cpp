/**
 * The MIT License (MIT)
 *
 * Copyright (c) 2026 SUZUKI PLAN.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 */
#include "mgbahelper.h"

#include <cstring>
#include <new>
#include <vector>

#include <mgba/core/config.h>
#include <mgba/core/core.h>
#include <mgba/internal/gba/input.h>
#include <mgba-util/audio-buffer.h>
#include <mgba-util/audio-resampler.h>
#include <mgba-util/interpolator.h>
#include <mgba-util/vfs.h>

namespace
{
constexpr unsigned OUTPUT_SAMPLE_RATE = 44100;
constexpr size_t AUDIO_BUFFER_FRAMES = 2048;
constexpr size_t AUDIO_CHANNELS = 2;
} // namespace

struct mGBAHelper::Impl {
    mCore* core;
    mAudioBuffer resampledAudio;
    mAudioResampler resampler;
    bool coreInitialized;
    bool configInitialized;
    bool audioInitialized;

    Impl()
        : core(nullptr), coreInitialized(false), configInitialized(false), audioInitialized(false)
    {
    }

    ~Impl()
    {
        if (audioInitialized) {
            mAudioResamplerDeinit(&resampler);
            mAudioBufferDeinit(&resampledAudio);
        }
        if (core) {
            if (configInitialized) {
                mCoreConfigDeinit(&core->config);
            }
            if (coreInitialized) {
                core->deinit(core);
            }
        }
    }

    void initializeAudio()
    {
        mAudioBufferInit(&resampledAudio, AUDIO_BUFFER_FRAMES, AUDIO_CHANNELS);
        mAudioResamplerInit(&resampler, mINTERPOLATOR_COSINE);
        mAudioResamplerSetDestination(&resampler, &resampledAudio, OUTPUT_SAMPLE_RATE);
        audioInitialized = true;
    }

    void resetAudio()
    {
        mAudioBufferClear(&resampledAudio);
        mAudioResamplerDeinit(&resampler);
        mAudioResamplerInit(&resampler, mINTERPOLATOR_COSINE);
        mAudioResamplerSetDestination(&resampler, &resampledAudio, OUTPUT_SAMPLE_RATE);
    }
};

mGBAHelper::mGBAHelper()
    : impl(nullptr), keyState{}
{
    std::memset(vram, 0, sizeof(vram));
}

mGBAHelper::~mGBAHelper()
{
    delete impl;
}

bool mGBAHelper::load(const void* data, size_t size)
{
    if (!data || !size) {
        return false;
    }

    Impl* next = new (std::nothrow) Impl();
    if (!next) {
        return false;
    }

    next->core = mCoreCreate(mPLATFORM_GBA);
    if (!next->core || !next->core->init(next->core)) {
        delete next;
        return false;
    }
    next->coreInitialized = true;

    mCoreInitConfig(next->core, "mgbahelper");
    next->configInitialized = true;
    mCoreConfigSetOverrideIntValue(&next->core->config, "audioBuffers", AUDIO_BUFFER_FRAMES);
    mCoreConfigSetOverrideIntValue(&next->core->config, "audioSync", 0);
    mCoreConfigSetOverrideIntValue(&next->core->config, "videoSync", 0);
    mCoreConfigSetOverrideIntValue(&next->core->config, "volume", 0x100);
    mCoreLoadForeignConfig(next->core, &next->core->config);
    next->core->setVideoBuffer(next->core, reinterpret_cast<mColor*>(vram), GBA_VRAM_WIDTH);

    VFile* rom = VFileMemChunk(data, size);
    if (!rom || !next->core->isROM(rom)) {
        if (rom) {
            rom->close(rom);
        }
        delete next;
        return false;
    }
    rom->seek(rom, 0, SEEK_SET);
    if (!next->core->loadROM(next->core, rom)) {
        rom->close(rom);
        delete next;
        return false;
    }

    next->initializeAudio();
    next->core->reset(next->core);

    delete impl;
    impl = next;
    soundQueue.clear();
    dequeuedSound.clear();
    std::memset(vram, 0, sizeof(vram));
    impl->core->setVideoBuffer(impl->core, reinterpret_cast<mColor*>(vram), GBA_VRAM_WIDTH);
    return true;
}

void mGBAHelper::reset()
{
    if (!impl) {
        return;
    }

    impl->core->reset(impl->core);
    impl->resetAudio();
    soundQueue.clear();
    dequeuedSound.clear();
    std::memset(vram, 0, sizeof(vram));
}

void mGBAHelper::tick()
{
    if (!impl) {
        return;
    }

    uint32_t keys = 0;
    keys |= static_cast<uint32_t>(keyState.a) << GBA_KEY_A;
    keys |= static_cast<uint32_t>(keyState.b) << GBA_KEY_B;
    keys |= static_cast<uint32_t>(keyState.start) << GBA_KEY_START;
    keys |= static_cast<uint32_t>(keyState.right) << GBA_KEY_RIGHT;
    keys |= static_cast<uint32_t>(keyState.left) << GBA_KEY_LEFT;
    keys |= static_cast<uint32_t>(keyState.up) << GBA_KEY_UP;
    keys |= static_cast<uint32_t>(keyState.down) << GBA_KEY_DOWN;
    keys |= static_cast<uint32_t>(keyState.r) << GBA_KEY_R;
    keys |= static_cast<uint32_t>(keyState.l) << GBA_KEY_L;
    impl->core->setKeys(impl->core, keys);
    impl->core->runFrame(impl->core);

    mAudioBuffer* source = impl->core->getAudioBuffer(impl->core);
    mAudioResamplerSetSource(&impl->resampler, source, impl->core->audioSampleRate(impl->core), true);
    mAudioResamplerProcess(&impl->resampler);

    const size_t frames = mAudioBufferAvailable(&impl->resampledAudio);
    if (!frames) {
        return;
    }

    std::vector<int16_t> samples(frames * AUDIO_CHANNELS);
    const size_t readFrames = mAudioBufferRead(&impl->resampledAudio, samples.data(), frames);
    const size_t sampleCount = readFrames * AUDIO_CHANNELS;
    if (sampleCount >= GBA_SOUND_QUEUE_SIZE) {
        soundQueue.clear();
        soundQueue.reserve(GBA_SOUND_QUEUE_SIZE);
        const size_t first = sampleCount - GBA_SOUND_QUEUE_SIZE;
        soundQueue.insert(soundQueue.end(), samples.begin() + first, samples.begin() + sampleCount);
        return;
    }

    const size_t required = soundQueue.size() + sampleCount;
    if (required > GBA_SOUND_QUEUE_SIZE) {
        const size_t discard = required - GBA_SOUND_QUEUE_SIZE;
        soundQueue.erase(soundQueue.begin(), soundQueue.begin() + discard);
    }
    soundQueue.insert(soundQueue.end(), samples.begin(), samples.begin() + sampleCount);
}

uint16_t* mGBAHelper::dequeSound(size_t* size)
{
    if (!size) {
        return nullptr;
    }

    dequeuedSound.swap(soundQueue);
    soundQueue.clear();
    *size = dequeuedSound.size() * sizeof(uint16_t);
    return dequeuedSound.empty() ? nullptr : dequeuedSound.data();
}
