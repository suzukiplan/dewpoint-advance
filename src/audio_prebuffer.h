#pragma once

#include <cstdint>

// Presentation and emulation share a thread. Retain extra headroom after an
// underrun so repeated capture/presentation stalls do not repeatedly stop PCM.
class AudioPrebuffer
{
  public:
    static constexpr uint32_t MAX_DURATION_MS = 200;

    uint32_t targetBytes(uint32_t frequency, uint32_t frameBytes) const
    {
        return frequency * durationMs / 1000 * frameBytes;
    }

    void onUnderrun()
    {
        if (durationMs < MAX_DURATION_MS) {
            durationMs += 50;
        }
    }

  private:
    uint32_t durationMs = 50;
};
