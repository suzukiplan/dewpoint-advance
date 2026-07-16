/**
 * Dewpoint Advance Runtime (Platform Independed)
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

#include <array>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <limits>
#include <new>
#include <vector>

#include <mgba/core/config.h>
#include <mgba/core/core.h>
#include <mgba/core/cpu.h>
#include <mgba/internal/arm/arm.h>
#include <mgba/internal/gba/gba.h>
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
constexpr uint32_t DEWPOINT_BASE = 0x04801000;
constexpr uint32_t DEWPOINT_REGISTER_COUNT = 16;
constexpr uint32_t DEWPOINT_END = DEWPOINT_BASE + DEWPOINT_REGISTER_COUNT * sizeof(uint32_t);
constexpr uint32_t DEWPOINT_COMPONENT_ID = 0x44505754; // "DPWT"

class AnalogEffect
{
  private:
    static constexpr size_t CHANNEL_COUNT = 2;
    static constexpr float HP_ALPHA = 0.9971f;
    static constexpr float LP_ALPHA = 0.62f;
    static constexpr float ASYM_POS_GAIN = 1.000f;
    static constexpr float ASYM_NEG_GAIN = 0.996f;
    static constexpr float ASYM_POS_CURVE = 0.006f;
    static constexpr float ASYM_NEG_CURVE = 0.009f;
    static constexpr float POST_LP_ALPHA = 0.94f;
    static constexpr float NOTCH_FREQUENCY_HZ = 4900.0f;
    static constexpr float NOTCH_Q = 9.0f;
    static constexpr float NOTCH_MIX = 0.020f;
    static constexpr float SATURATOR_DRIVE = 1.006f;
    static constexpr float OUTPUT_GAIN = 0.940f;
    static constexpr float PI = 3.14159265358979323846f;

    struct NotchCoefficients {
        float b0;
        float b1;
        float b2;
        float a1;
        float a2;
    } notch;

    std::array<float, CHANNEL_COUNT> hpLastInput;
    std::array<float, CHANNEL_COUNT> hpLastOutput;
    std::array<float, CHANNEL_COUNT> lpLastOutput;
    std::array<float, CHANNEL_COUNT> postLpLastOutput;
    std::array<float, CHANNEL_COUNT> notchInput1;
    std::array<float, CHANNEL_COUNT> notchInput2;
    std::array<float, CHANNEL_COUNT> notchOutput1;
    std::array<float, CHANNEL_COUNT> notchOutput2;

    static float clampUnit(float value)
    {
        if (value < -1.0f) {
            return -1.0f;
        }
        if (value > 1.0f) {
            return 1.0f;
        }
        return value;
    }

    float applyNotch(size_t channel, float value)
    {
        const float filtered = (notch.b0 * value) + (notch.b1 * notchInput1[channel]) + (notch.b2 * notchInput2[channel]) - (notch.a1 * notchOutput1[channel]) - (notch.a2 * notchOutput2[channel]);

        notchInput2[channel] = notchInput1[channel];
        notchInput1[channel] = value;
        notchOutput2[channel] = notchOutput1[channel];
        notchOutput1[channel] = filtered;

        return (value * (1.0f - NOTCH_MIX)) + (filtered * NOTCH_MIX);
    }

    float apply(size_t channel, float sample)
    {
        float value = sample - hpLastInput[channel] + (HP_ALPHA * hpLastOutput[channel]);
        hpLastInput[channel] = sample;
        hpLastOutput[channel] = value;

        if (value >= 0.0f) {
            value = (value * ASYM_POS_GAIN) + (ASYM_POS_CURVE * value * value);
        } else {
            value = (value * ASYM_NEG_GAIN) - (ASYM_NEG_CURVE * value * value);
        }
        value = clampUnit(value);

        float& lowPass = lpLastOutput[channel];
        lowPass += LP_ALPHA * (value - lowPass);
        value = lowPass;

        const float driven = value * SATURATOR_DRIVE;
        value = driven / (1.0f + ((SATURATOR_DRIVE - 1.0f) * std::fabs(driven)));

        float& postLowPass = postLpLastOutput[channel];
        postLowPass += POST_LP_ALPHA * (value - postLowPass);
        value = applyNotch(channel, postLowPass) * OUTPUT_GAIN;
        return clampUnit(value);
    }

  public:
    AnalogEffect()
        : notch{}
    {
        const float omega = 2.0f * PI * NOTCH_FREQUENCY_HZ / static_cast<float>(OUTPUT_SAMPLE_RATE);
        const float alpha = std::sin(omega) / (2.0f * NOTCH_Q);
        const float cosOmega = std::cos(omega);
        const float a0 = 1.0f + alpha;
        notch.b0 = 1.0f / a0;
        notch.b1 = (-2.0f * cosOmega) / a0;
        notch.b2 = 1.0f / a0;
        notch.a1 = (-2.0f * cosOmega) / a0;
        notch.a2 = (1.0f - alpha) / a0;
        reset();
    }

    void reset()
    {
        hpLastInput.fill(0.0f);
        hpLastOutput.fill(0.0f);
        lpLastOutput.fill(0.0f);
        postLpLastOutput.fill(0.0f);
        notchInput1.fill(0.0f);
        notchInput2.fill(0.0f);
        notchOutput1.fill(0.0f);
        notchOutput2.fill(0.0f);
    }

    void process(int16_t* samples, size_t frames)
    {
        for (size_t frame = 0; frame < frames; ++frame) {
            for (size_t channel = 0; channel < CHANNEL_COUNT; ++channel) {
                const size_t index = frame * CHANNEL_COUNT + channel;
                const float input = static_cast<float>(samples[index]) / 32768.0f;
                float output = apply(channel, input) * 32768.0f;
                if (output < -32768.0f) {
                    output = -32768.0f;
                } else if (output > 32767.0f) {
                    output = 32767.0f;
                }
                samples[index] = static_cast<int16_t>(std::lround(output));
            }
        }
    }
};

bool isDewpointAddress(uint32_t address)
{
    return address >= DEWPOINT_BASE && address < DEWPOINT_END && (address & 3) == 0;
}
} // namespace

struct mGBAHelper::Impl {
    struct BridgeComponent {
        mCPUComponent d;
        Impl* owner;
    } bridgeComponent;

    mCore* core;
    ARMMemory originalMemory;
    DewpointBridge* dewpointBridge;
    mAudioBuffer resampledAudio;
    mAudioResampler resampler;
    AnalogEffect analogEffect;
    bool coreInitialized;
    bool configInitialized;
    bool audioInitialized;

    Impl()
        : bridgeComponent{}, core(nullptr), originalMemory{}, dewpointBridge(nullptr), coreInitialized(false),
          configInitialized(false), audioInitialized(false)
    {
        bridgeComponent.d.id = DEWPOINT_COMPONENT_ID;
        bridgeComponent.owner = this;
    }

    ~Impl()
    {
        uninstallBridge();
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

    static Impl* findBridge(ARMCore* cpu)
    {
        if (!cpu || cpu->numComponents <= CPU_COMPONENT_MISC_1) {
            return nullptr;
        }
        mCPUComponent* component = cpu->components[CPU_COMPONENT_MISC_1];
        if (!component || component->id != DEWPOINT_COMPONENT_ID) {
            return nullptr;
        }
        return reinterpret_cast<BridgeComponent*>(component)->owner;
    }

    static void addAccessCycles(ARMCore* cpu, int* cycleCounter, int cycles)
    {
        if (cycleCounter) {
            *cycleCounter += cpu->memory.stall ? cpu->memory.stall(cpu, cycles) : cycles;
        }
    }

    static uint32_t load32(ARMCore* cpu, uint32_t address, int* cycleCounter)
    {
        Impl* self = findBridge(cpu);
        if (!self) {
            return 0;
        }
        if (!isDewpointAddress(address)) {
            return self->originalMemory.load32(cpu, address, cycleCounter);
        }

        // Match ordinary I/O reads: polling this bridge must prevent idle-loop removal.
        reinterpret_cast<GBA*>(cpu->master)->haltPending = false;
        addAccessCycles(cpu, cycleCounter, 2);
        if (!self->dewpointBridge) {
            return 0;
        }
        return self->dewpointBridge->readRegister((address - DEWPOINT_BASE) >> 2);
    }

    static void store32(ARMCore* cpu, uint32_t address, int32_t value, int* cycleCounter)
    {
        Impl* self = findBridge(cpu);
        if (!self) {
            return;
        }
        if (!isDewpointAddress(address)) {
            self->originalMemory.store32(cpu, address, value, cycleCounter);
            return;
        }

        addAccessCycles(cpu, cycleCounter, 1);
        if (self->dewpointBridge) {
            self->dewpointBridge->writeRegister((address - DEWPOINT_BASE) >> 2, static_cast<uint32_t>(value));
        }
    }

    bool installBridge()
    {
        ARMCore* cpu = static_cast<ARMCore*>(core->cpu);
        if (cpu->numComponents <= CPU_COMPONENT_MISC_1 || cpu->components[CPU_COMPONENT_MISC_1]) {
            return false;
        }
        originalMemory = cpu->memory;
        cpu->components[CPU_COMPONENT_MISC_1] = &bridgeComponent.d;
        cpu->memory.load32 = load32;
        cpu->memory.store32 = store32;
        return true;
    }

    void uninstallBridge()
    {
        if (!core || !core->cpu) {
            return;
        }
        ARMCore* cpu = static_cast<ARMCore*>(core->cpu);
        if (cpu->memory.load32 == load32) {
            cpu->memory.load32 = originalMemory.load32;
        }
        if (cpu->memory.store32 == store32) {
            cpu->memory.store32 = originalMemory.store32;
        }
        if (cpu->numComponents > CPU_COMPONENT_MISC_1 && cpu->components[CPU_COMPONENT_MISC_1] == &bridgeComponent.d) {
            cpu->components[CPU_COMPONENT_MISC_1] = nullptr;
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
        analogEffect.reset();
    }
};

mGBAHelper::mGBAHelper()
    : impl(nullptr), dewpointBridge(nullptr), keyState{}
{
    std::memset(vram, 0, sizeof(vram));
}

mGBAHelper::~mGBAHelper()
{
    saveSram();
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

    if (!sramPath.empty()) {
        std::ifstream input(sramPath, std::ios::binary | std::ios::ate);
        if (input) {
            const std::streamsize sramSize = input.tellg();
            if (sramSize < 0 || static_cast<uintmax_t>(sramSize) > std::numeric_limits<size_t>::max()) {
                delete next;
                return false;
            }

            std::vector<uint8_t> sram(static_cast<size_t>(sramSize));
            input.seekg(0);
            if (sramSize > 0 && !input.read(reinterpret_cast<char*>(sram.data()), sramSize)) {
                delete next;
                return false;
            }

            VFile* save = VFileMemChunk(sram.data(), sram.size());
            if (!save || !next->core->loadSave(next->core, save)) {
                if (save) {
                    save->close(save);
                }
                delete next;
                return false;
            }
        } else {
            std::error_code error;
            const bool exists = std::filesystem::exists(sramPath, error);
            if (error || exists) {
                delete next;
                return false;
            }
        }
    }

    next->initializeAudio();
    next->core->reset(next->core);
    next->dewpointBridge = dewpointBridge;
    if (!next->installBridge()) {
        delete next;
        return false;
    }

    delete impl;
    impl = next;
    soundQueue.clear();
    dequeuedSound.clear();
    std::memset(vram, 0, sizeof(vram));
    impl->core->setVideoBuffer(impl->core, reinterpret_cast<mColor*>(vram), GBA_VRAM_WIDTH);
    return true;
}

void mGBAHelper::setSramPath(const std::string& path)
{
    sramPath = path;
}

bool mGBAHelper::saveSram()
{
    if (!impl || sramPath.empty()) {
        return false;
    }

    void* data = nullptr;
    const size_t size = impl->core->savedataClone(impl->core, &data);
    if (!data || !size) {
        std::free(data);
        return false;
    }

    std::ofstream output(sramPath, std::ios::binary | std::ios::trunc);
    if (!output || size > static_cast<size_t>(std::numeric_limits<std::streamsize>::max())) {
        std::free(data);
        return false;
    }

    output.write(static_cast<const char*>(data), static_cast<std::streamsize>(size));
    output.flush();
    const bool succeeded = static_cast<bool>(output);
    std::free(data);
    return succeeded;
}

void mGBAHelper::reset()
{
    if (!impl) {
        return;
    }

    impl->core->reset(impl->core);
    if (dewpointBridge) {
        dewpointBridge->reset();
    }
    impl->resetAudio();
    soundQueue.clear();
    dequeuedSound.clear();
    std::memset(vram, 0, sizeof(vram));
}

void mGBAHelper::setDewpointBridge(DewpointBridge* bridge)
{
    dewpointBridge = bridge;
    if (impl) {
        impl->dewpointBridge = bridge;
    }
}

bool mGBAHelper::writeGuestMemory(uint32_t address, const void* data, size_t size)
{
    if (!impl || (!data && size)) {
        return false;
    }
    const uint64_t end = static_cast<uint64_t>(address) + size;
    const bool inEwram = address >= 0x02000000 && end <= 0x02040000;
    const bool inIwram = address >= 0x03000000 && end <= 0x03008000;
    if ((!inEwram && !inIwram) || end > UINT32_MAX + uint64_t{1}) {
        return false;
    }

    const uint8_t* source = static_cast<const uint8_t*>(data);
    for (size_t i = 0; i < size; ++i) {
        impl->core->busWrite8(impl->core, address + static_cast<uint32_t>(i), source[i]);
    }
    return true;
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
    keys |= static_cast<uint32_t>(keyState.select) << GBA_KEY_SELECT;
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
    impl->analogEffect.process(samples.data(), readFrames);
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
