#include "frame_diagnostics.h"

#include <cassert>
#include <string>
#include <vector>

int main()
{
    std::vector<std::string> batches;
    auto now = FrameDiagnostics::Clock::time_point{};
    const auto clock = [&] { return now; };
    {
        FrameDiagnostics diagnostics([&](const std::string& text) { batches.push_back(text); }, clock);
        for (int i = 0; i < 100; ++i) {
            diagnostics.begin(false);
            diagnostics.emulationFrames = 1;
            diagnostics.submitted = true;
            now += std::chrono::milliseconds(10);
            diagnostics.finish();
        }
        assert(batches.size() == 1);
        assert(batches[0].find("draws=100 frames=100 max_frames=1") != std::string::npos);
        assert(batches[0].find("anomalies=0") != std::string::npos);
        assert(std::count(batches[0].begin(), batches[0].end(), '\n') == 1);

        // Repeated catch-up is counted in full, but only the worst and its
        // neighbours are emitted. A later, larger spike must replace it.
        for (int i = 0; i < 50; ++i) {
            diagnostics.begin(false);
            diagnostics.emulationFrames = i == 20 ? 4 : 2;
            diagnostics.submitted = true;
            {
                FrameDiagnostics::Scope timing(&diagnostics, FrameDiagnostics::Texture);
                now += std::chrono::milliseconds(i == 20 ? 40 : 1);
            }
            diagnostics.finish();
        }
        now += std::chrono::milliseconds(100);
        diagnostics.begin(true);
        diagnostics.submitted = true;
        diagnostics.finish();
        diagnostics.flush();
        assert(batches.size() == 2);
        assert(batches[1].find("draws=51 frames=102 max_frames=4") != std::string::npos);
        assert(batches[1].find("catchup=50 anomalies=50") != std::string::npos);
        assert(batches[1].find("texture_avg/max_ms=1.745/40.000") != std::string::npos);
        assert(batches[1].find("before draw=120 ") != std::string::npos);
        assert(batches[1].find("hitch draw=121 ") != std::string::npos);
        assert(batches[1].find("after draw=122 ") != std::string::npos);
        assert(std::count(batches[1].begin(), batches[1].end(), '\n') == 4);
        diagnostics.flush();
        assert(batches.size() == 2);
        // Failure at the window end is preserved without a fabricated after.
        diagnostics.begin(false);
        now += std::chrono::milliseconds(10);
        diagnostics.finish();

        std::vector<uint32_t> original(240 * 160, 0xFF123456);
        const auto saved = original;
        const auto* marked = diagnostics.markedPixels(original.data(), 240, 160);
        assert(marked != original.data());
        assert(original == saved);
        assert(marked[0] == original[0]);
        assert(marked[2 * 240 + 219] == 0xFF000000);
        assert(marked[3 * 240 + 221] == 0xFFFFFFFF);
        assert(marked[159 * 240 + 239] == original.back());
        assert(diagnostics.markedPixels(nullptr, 240, 160) == nullptr);
        assert(diagnostics.markedPixels(original.data(), 20, 8) == original.data());
    }
    assert(batches.size() == 3);
    assert(batches[2].find("not_submitted=1") != std::string::npos);
    assert(batches[2].find("hitch draw=152 ") != std::string::npos);
    assert(batches[2].find("[diag] after") == std::string::npos);
    {
        FrameDiagnostics unused([&](const std::string&) { assert(false); });
        FrameDiagnostics::Scope disabled(nullptr, FrameDiagnostics::Present);
    }
}
