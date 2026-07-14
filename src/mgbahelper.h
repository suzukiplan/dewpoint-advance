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
#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

#define GBA_VRAM_WIDTH 240
#define GBA_VRAM_HEIGHT 160
#define GBA_SOUND_QUEUE_SIZE 65536

class mGBAHelper
{
  private:
    struct Impl;

    Impl* impl;
    uint32_t vram[GBA_VRAM_WIDTH * GBA_VRAM_HEIGHT];
    std::vector<uint16_t> soundQueue;
    std::vector<uint16_t> dequeuedSound;

  public:
    struct KeyState {
        bool up;     // true: pushing, false: released
        bool down;   // true: pushing, false: released
        bool left;   // true: pushing, false: released
        bool right;  // true: pushing, false: released
        bool a;      // true: pushing, false: released
        bool b;      // true: pushing, false: released
        bool start;  // true: pushing, false: released
        bool select; // true: pushing, false: released
        bool l;      // true: pushing, false: released
        bool r;      // true: pushing, false: released
    } keyState;

    mGBAHelper();
    ~mGBAHelper();

    mGBAHelper(const mGBAHelper&) = delete;
    mGBAHelper& operator=(const mGBAHelper&) = delete;

    /**
     * @brief Load GBA ROM
     * @param data GBA ROM
     * @param size Size of GBA ROM
     * @return true: succeed, false: failed
     */
    bool load(const void* data, size_t size);

    /**
     * @brief Reset
     */
    void reset();

    /**
     * @brief Execute 1 frame
     */
    void tick();

    /**
     * @brief Get VRAM
     * @return VRAM pointer
     */
    inline uint32_t* getVram() { return vram; }

    /**
     * @brief Get VRAM width
     * @return VRAM width
     */
    inline int getVramWidth() { return GBA_VRAM_WIDTH; }

    /**
     * @brief Get VRAM height
     * @return VRAM height
     */
    inline int getVramHeight() { return GBA_VRAM_HEIGHT; }

    /**
     * @brief Dequeue sound data
     * @param size Raw PCM size in bytes
     * @return Raw PCM data (44.1kHz, 16bit, 2ch). The pointer remains valid
     *         until the next call to dequeSound().
     */
    uint16_t* dequeSound(size_t* size);
};
