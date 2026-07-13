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
