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
        FrameDiagnostics diagnostics([](const std::string&) {}, clock);
        const auto frame = [&](int microseconds, bool paused = false, bool submitted = true) {
            diagnostics.begin(paused);
            now += std::chrono::microseconds(microseconds);
            diagnostics.submitted = submitted;
            diagnostics.finish();
        };
        frame(100000); // First frame has no usable interval.
        assert(!diagnostics.isSlowPresentation());
        frame(20000); // Exactly 50 fps is not below the threshold.
        assert(!diagnostics.isSlowPresentation());
        frame(20001);
        assert(diagnostics.isSlowPresentation());
        std::vector<uint32_t> source(240 * 160, 0xFF123456);
        const auto* marked = diagnostics.markedPixels(source.data(), 240, 160);
        assert(marked[151 * 240 + 223] == 0xFF000000U);
        assert(marked[152 * 240 + 225] == 0xFF4040FFU);
        assert(source[152 * 240 + 225] == 0xFF123456U);
        frame(16667); // Recover on the next successful interval.
        assert(!diagnostics.isSlowPresentation());
        marked = diagnostics.markedPixels(source.data(), 240, 160);
        assert(marked[152 * 240 + 225] == source[152 * 240 + 225]);
        frame(40000);
        assert(diagnostics.isSlowPresentation());
        diagnostics.begin(true); // Hide immediately on entering pause.
        assert(!diagnostics.isSlowPresentation());
        frame(100000, true);
        frame(100000); // Resume does not measure the pause as a hitch.
        assert(!diagnostics.isSlowPresentation());
        frame(40000);
        assert(diagnostics.isSlowPresentation());
        frame(40000, false, false); // Failed/minimized presentation resets it.
        assert(!diagnostics.isSlowPresentation());
        frame(100000);
        assert(!diagnostics.isSlowPresentation());
    }
    {
        std::vector<std::string> transitions;
        FrameDiagnostics diagnostics([&](const std::string& text) {
            if (text.find("[diag] LAG ") == 0) { transitions.push_back(text); }
        }, clock);
        std::vector<uint32_t> source(240 * 160, 0xFF123456);
        const auto draw = [&](int ms, bool paused = false, bool success = true) {
            diagnostics.begin(paused);
            diagnostics.markedPixels(source.data(), 240, 160);
            now += std::chrono::milliseconds(ms);
            diagnostics.submitted = success;
            diagnostics.finish();
        };
        draw(16);
        draw(16);
        assert(transitions.empty()); // Initial OFF is not a transition.
        draw(30); // Detect lag, but the submitted image still has no mark.
        assert(transitions.empty());
        draw(30);
        assert(transitions.size() == 1);
        assert(transitions.back().find("LAG ON draw=4 ") != std::string::npos);
        draw(30);
        draw(16); // Recovery is reflected on the following image.
        assert(transitions.size() == 1);
        draw(16);
        assert(transitions.size() == 2);
        assert(transitions.back().find("LAG OFF draw=7 ") != std::string::npos);
        draw(16);
        draw(30);
        draw(30, false, false); // Failed attempt to display ON must not log.
        assert(transitions.size() == 2);
        draw(16);
        draw(30);
        draw(30);
        assert(transitions.size() == 3);
        draw(16, true, false); // Failed paused draw must not log OFF.
        assert(transitions.size() == 3);
        draw(16, true);
        assert(transitions.size() == 4);
        assert(transitions.back().find("LAG OFF ") != std::string::npos);
        draw(16, true);
        draw(16);
        assert(transitions.size() == 4);
    }
    {
        FrameDiagnostics unused([&](const std::string&) { assert(false); });
        FrameDiagnostics::Scope disabled(nullptr, FrameDiagnostics::Present);
    }
}
