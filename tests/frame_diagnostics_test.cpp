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
        std::vector<std::string> transitions;
        FrameDiagnostics diagnostics([&](const std::string& text) {
            if (text.find("[diag] LAG ") == 0) { transitions.push_back(text); }
        }, clock);
        std::vector<uint32_t> source(240 * 160, 0xFF123456);
        const auto draw = [&](int ms, unsigned frames, bool paused = false, bool success = true) {
            diagnostics.begin(paused);
            diagnostics.markedPixels(source.data(), 240, 160);
            now += std::chrono::milliseconds(ms);
            diagnostics.emulationFrames = frames;
            diagnostics.submitted = success;
            diagnostics.finish();
        };
        // Alternating short/long presentation intervals total 60 game fps.
        for (int i = 0; i < 20; ++i) {
            draw(25, 1);
            draw(25, 2);
        }
        assert(!diagnostics.isLagging());
        assert(transitions.empty());
        // Fewer than 50 draws is fine when catch-up maintains 50 game fps.
        for (int i = 0; i < 25; ++i) { draw(40, 2); }
        assert(!diagnostics.isLagging());
        // The decision changes only after the full second, even after a hitch.
        draw(900, 44);
        assert(!diagnostics.isLagging());
        draw(100, 5); // 49 frames in one second.
        assert(diagnostics.isLagging());
        assert(transitions.empty()); // Marker is submitted on the next draw.
        const auto* marked = diagnostics.markedPixels(source.data(), 240, 160);
        assert(marked[152 * 240 + 225] == 0xFF4040FFU);
        assert(source[152 * 240 + 225] == 0xFF123456U);
        draw(20, 1);
        assert(transitions.size() == 1);
        assert(transitions.back().find("LAG ON ") != std::string::npos);
        for (int i = 0; i < 49; ++i) { draw(20, 1); }
        assert(!diagnostics.isLagging()); // Exactly 50 fps recovers.
        assert(transitions.size() == 1);
        draw(20, 1, false, false); // Failed OFF submission is not logged.
        assert(transitions.size() == 1);
        draw(20, 1);
        assert(transitions.size() == 2);
        assert(transitions.back().find("LAG OFF ") != std::string::npos);
        draw(1960, 58); // 60 frames over two seconds is 30 fps, not 60 fps.
        assert(diagnostics.isLagging());
        draw(20, 1);
        assert(transitions.size() == 3);
        draw(5000, 0, true);
        assert(!diagnostics.isLagging());
        assert(transitions.size() == 4);
        draw(5000, 0, true);
        for (int i = 0; i < 50; ++i) { draw(20, 1); }
        assert(!diagnostics.isLagging());
        assert(transitions.size() == 4); // Pause duration is excluded.
    }
    {
        FrameDiagnostics unused([&](const std::string&) { assert(false); });
        FrameDiagnostics::Scope disabled(nullptr, FrameDiagnostics::Present);
    }
}
