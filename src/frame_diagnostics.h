#ifndef FRAME_DIAGNOSTICS_H
#define FRAME_DIAGNOSTICS_H

#include <algorithm>
#include <array>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <functional>
#include <string>
#include <utility>
#include <vector>

class FrameDiagnostics
{
  public:
    using Clock = std::chrono::steady_clock;
    using Now = std::function<Clock::time_point()>;
    using Sink = std::function<void(const std::string&)>;
    enum Phase { Emulation, Texture, Present, Wait, Render, PhaseCount };

    class Scope
    {
      public:
        Scope(FrameDiagnostics* diagnostics, Phase phase)
            : diagnostics(diagnostics), phase(phase),
              start(diagnostics ? diagnostics->now() : Clock::time_point{}) {}
        ~Scope()
        {
            if (diagnostics) {
                diagnostics->milliseconds[phase] +=
                    std::chrono::duration<double, std::milli>(diagnostics->now() - start).count();
            }
        }
        Scope(const Scope&) = delete;
        Scope& operator=(const Scope&) = delete;
      private:
        FrameDiagnostics* diagnostics;
        Phase phase;
        Clock::time_point start;
    };

    explicit FrameDiagnostics(Sink sink, Now now = [] { return Clock::now(); })
        : sink(std::move(sink)), now(std::move(now)), epoch(this->now()) {}
    ~FrameDiagnostics() { flush(); }
    FrameDiagnostics(const FrameDiagnostics&) = delete;
    FrameDiagnostics& operator=(const FrameDiagnostics&) = delete;

    void begin(bool isPaused)
    {
        milliseconds.fill(0);
        emulationFrames = 0;
        submitted = false;
        markerPrepared = false;
        paused = isPaused;
        if (paused) {
            slowEmulation = false;
            rateWindowActive = false;
            rateFrames = 0;
        } else if (!rateWindowActive) {
            rateWindowStart = now();
            rateWindowActive = true;
        }
    }

    void finish()
    {
        const auto completed = now();
        const double elapsed = std::chrono::duration<double, std::milli>(completed - epoch).count();
        Sample sample{++draws, elapsed, hasPrevious ? elapsed - previous.elapsed : 0,
                      emulationFrames, paused, submitted, milliseconds};
        // Log the marker in the submitted image, not the next interval's verdict.
        // A failed presentation must not commit a visibility transition.
        if (submitted && markerPrepared && preparedLag != displayedLag) {
            displayedLag = preparedLag;
            char line[160];
            std::snprintf(line, sizeof(line),
                "[diag] LAG %s draw=%llu t_ms=%.3f\n",
                displayedLag ? "ON" : "OFF",
                static_cast<unsigned long long>(draws), elapsed);
            sink(line);
        }
        // Count emulated frames, including catch-up, independently of presents.
        // A stalled loop may sample late; normalize by the actual elapsed time.
        if (!paused && rateWindowActive) {
            rateFrames += emulationFrames;
            const double durationMs = std::chrono::duration<double, std::milli>(
                completed - rateWindowStart).count();
            if (durationMs >= 1000.0) {
                slowEmulation = rateFrames * 1000.0 < 50.0 * durationMs;
                rateFrames = 0;
                rateWindowStart = completed;
            }
        }
        ++count;
        frames += emulationFrames;
        pausedDraws += paused;
        failedDraws += !submitted;
        catchupDraws += emulationFrames > 1;
        maxFrames = std::max(maxFrames, emulationFrames);
        maxInterval = std::max(maxInterval, sample.interval);
        for (size_t i = 0; i < PhaseCount; ++i) {
            totals[i] += milliseconds[i];
            maxima[i] = std::max(maxima[i], milliseconds[i]);
        }
        // Keep one representative hitch with its immediate neighbours. Counts
        // and per-phase maxima retain evidence of other hitches without flooding.
        const bool anomaly = !submitted || (!paused && (emulationFrames > 1 ||
            (hasPrevious && !previous.paused && sample.interval > 25.0) ||
            *std::max_element(milliseconds.begin(), milliseconds.end()) > 16.7));
        if (anomaly) {
            ++anomalies;
        }
        const double severity = std::max({sample.interval, milliseconds[Render],
            milliseconds[Emulation], emulationFrames * 16.7});
        if (anomaly && (!hasWorst || (!submitted && worst.submitted) ||
            (submitted == worst.submitted && severity > worstSeverity))) {
            worst = sample;
            before = previous;
            hasBefore = hasPrevious;
            hasWorst = true;
            hasAfter = false;
            worstSeverity = severity;
        } else if (hasWorst && sample.draw == worst.draw + 1) {
            after = sample;
            hasAfter = true;
        }
        previous = sample;
        hasPrevious = true;
        lastElapsed = elapsed;
        if (elapsed - windowStart >= 1000.0) {
            flush();
        }
    }

    void flush()
    {
        if (!count) {
            return;
        }
        char line[1024];
        std::snprintf(line, sizeof(line),
            "[diag] summary t_ms=%.3f span_ms=%.3f draws=%u frames=%llu max_frames=%u paused=%u not_submitted=%u catchup=%u anomalies=%u interval_max_ms=%.3f emulation_avg/max_ms=%.3f/%.3f texture_avg/max_ms=%.3f/%.3f present_avg/max_ms=%.3f/%.3f wait_avg/max_ms=%.3f/%.3f render_avg/max_ms=%.3f/%.3f\n",
            lastElapsed, lastElapsed - windowStart, count,
            static_cast<unsigned long long>(frames), maxFrames, pausedDraws,
            failedDraws, catchupDraws, anomalies, maxInterval,
            totals[Emulation] / count, maxima[Emulation],
            totals[Texture] / count, maxima[Texture],
            totals[Present] / count, maxima[Present],
            totals[Wait] / count, maxima[Wait],
            totals[Render] / count, maxima[Render]);
        std::string output = line;
        if (hasWorst) {
            if (hasBefore) { output += formatSample("before", before); }
            output += formatSample("hitch", worst);
            if (hasAfter) { output += formatSample("after", after); }
        }
        sink(output);
        windowStart = lastElapsed;
        count = pausedDraws = failedDraws = catchupDraws = anomalies = maxFrames = 0;
        frames = 0;
        maxInterval = worstSeverity = 0;
        totals.fill(0);
        maxima.fill(0);
        hasWorst = hasBefore = hasAfter = false;
    }

    // Only the display copy is marked; emulated VRAM and game state stay intact.
    const uint32_t* markedPixels(const uint32_t* source, int width, int height)
    {
        markerPrepared = source != nullptr;
        preparedLag = false;
        if (!source || width < 21 || height < 9) {
            return source;
        }
        pixels.assign(source, source + static_cast<size_t>(width) * height);
        // White DIAG lettering on a black panel in the game viewport's top right.
        constexpr unsigned glyphs[4][5] = {
            {6, 5, 5, 5, 6}, {7, 2, 2, 2, 7},
            {2, 5, 7, 5, 5}, {3, 4, 5, 5, 3},
        };
        drawMarker(width, 2, glyphs, 0xFFFFFFFFU);
        if (slowEmulation && height >= 18) {
            preparedLag = true;
            constexpr unsigned lagGlyphs[3][5] = {
                {4, 4, 4, 4, 7}, {2, 5, 7, 5, 5}, {3, 4, 5, 5, 3},
            };
            drawMarker(width, height - 9, lagGlyphs, 0xFF4040FFU);
        }
        return pixels.data();
    }

    bool isLagging() const { return slowEmulation; }

    unsigned emulationFrames = 0;
    bool submitted = false;

  private:
    template<size_t Letters>
    void drawMarker(int width, int top, const unsigned (&glyphs)[Letters][5], uint32_t color)
    {
        const int left = width - static_cast<int>(Letters) * 4 - 5;
        for (int y = top; y < top + 7; ++y) {
            for (int x = left; x < width - 2; ++x) {
                pixels[y * width + x] = 0xFF000000U;
            }
        }
        for (size_t letter = 0; letter < Letters; ++letter) {
            for (int y = 0; y < 5; ++y) {
                for (int x = 0; x < 3; ++x) {
                    if (glyphs[letter][y] & (4U >> x)) {
                        pixels[(top + y + 1) * width + left + 2 + letter * 4 + x] = color;
                    }
                }
            }
        }
    }

    struct Sample
    {
        uint64_t draw = 0;
        double elapsed = 0;
        double interval = 0;
        unsigned frames = 0;
        bool paused = false;
        bool submitted = false;
        std::array<double, PhaseCount> milliseconds{};
    };

    static std::string formatSample(const char* kind, const Sample& sample)
    {
        char line[512];
        std::snprintf(line, sizeof(line),
            "[diag] %s draw=%llu t_ms=%.3f interval_ms=%.3f frames=%u paused=%d submitted=%d emulation_ms=%.3f texture_ms=%.3f present_ms=%.3f wait_ms=%.3f render_ms=%.3f\n",
            kind, static_cast<unsigned long long>(sample.draw), sample.elapsed,
            sample.interval, sample.frames, sample.paused, sample.submitted,
            sample.milliseconds[Emulation], sample.milliseconds[Texture],
            sample.milliseconds[Present], sample.milliseconds[Wait], sample.milliseconds[Render]);
        return line;
    }

    Sink sink;
    Now now;
    Clock::time_point epoch;
    std::array<double, PhaseCount> milliseconds{}, totals{}, maxima{};
    std::vector<uint32_t> pixels;
    uint64_t draws = 0, frames = 0;
    unsigned count = 0, pausedDraws = 0, failedDraws = 0, catchupDraws = 0;
    unsigned anomalies = 0, maxFrames = 0;
    bool slowEmulation = false;
    bool rateWindowActive = false;
    Clock::time_point rateWindowStart{};
    uint64_t rateFrames = 0;
    bool markerPrepared = false, preparedLag = false, displayedLag = false;
    bool paused = false, hasPrevious = false, hasWorst = false;
    bool hasBefore = false, hasAfter = false;
    double windowStart = 0, lastElapsed = 0, maxInterval = 0, worstSeverity = 0;
    Sample previous, before, worst, after;
};

#endif
