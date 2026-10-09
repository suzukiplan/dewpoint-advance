#include "audio_prebuffer.h"

#include <cassert>
#include <iostream>

int main()
{
    AudioPrebuffer prebuffer;
    constexpr uint32_t frequency = 44100;
    constexpr uint32_t frameBytes = 4;
    assert(prebuffer.targetBytes(frequency, frameBytes) == 8820);

    // Simulate repeated 80 ms presentation stalls. A fixed 50 ms queue
    // underruns on every iteration; recovery must retain enough PCM for the
    // next stall rather than restarting at the same insufficient target.
    const uint32_t consumed = frequency * 80 / 1000 * frameBytes;
    int underruns = 0;
    for (int presentation = 0; presentation < 20; ++presentation) {
        uint32_t queued = prebuffer.targetBytes(frequency, frameBytes);
        if (queued <= consumed) {
            ++underruns;
            prebuffer.onUnderrun();
        }
    }
    assert(underruns == 1);
    assert(prebuffer.targetBytes(frequency, frameBytes) == 17640);

    prebuffer.onUnderrun();
    assert(prebuffer.targetBytes(frequency, frameBytes) == 26460);
    for (int stall = 0; stall < 100; ++stall) {
        prebuffer.onUnderrun();
        assert(prebuffer.targetBytes(frequency, frameBytes) % frameBytes == 0);
        assert(prebuffer.targetBytes(frequency, frameBytes) <= 35280);
    }
    assert(prebuffer.targetBytes(frequency, frameBytes) == 35280);
    // The capped target plus one emulated frame and the DirectSound silence
    // guard must fit the one-second ring, allowing refill to make progress.
    assert(prebuffer.targetBytes(frequency, frameBytes) + 3000 + 88200 < 176400);
    assert(AudioPrebuffer().targetBytes(frequency, frameBytes) == 8820);
    std::cout << "audio_prebuffer_test passed\n";
}
