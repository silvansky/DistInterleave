#include "InterleaveEngine.h"
#include <cstdlib>
#include <iostream>

void require(bool condition, const char* message)
{
    if (!condition) { std::cerr << message << '\n'; std::exit(1); }
}

void checkConcatenation(int cycles, int periodA = 4, int periodB = 6, int frames = 20000,
                        double rate = 48000.0)
{
    interleave::Engine engine;
    engine.prepare(rate);
    engine.setCycles(cycles);
    int source = 0, position = 0, verified = 0;
    bool started = false;
    for (int i = 0; i < frames; ++i)
    {
        const float a = i % periodA < periodA / 2 ? -0.25f : 0.25f;
        const float b = i % periodB < periodB / 2 ? -0.75f : 0.75f;
        auto out = engine.process({a, a * 2}, {b, b * 2}, a, b);
        if (!started && out.left == 0) continue;
        started = true;
        const int period = source == 0 ? periodA : periodB;
        const float amplitude = source == 0 ? 0.25f : 0.75f;
        const float expected = position % period < period / 2 ? amplitude : -amplitude;
        require(out.left == expected, "Groups must alternate with intact cycles and original sample lengths");
        require(out.right == expected * 2, "Stereo frames must preserve both channels");
        if (++position == cycles * period) { position = 0; source = 1 - source; ++verified; }
    }
    require(verified > 10, "Must verify multiple alternating groups");
    engine.reset();
    require(engine.process({}, {}, 0, 0).left == 0, "Reset must discard captured audio");
}

int main()
{
    checkConcatenation(1);
    checkConcatenation(3);
    checkConcatenation(100);
    // Full 100-cycle groups at 20 / 12.5 Hz last 5 / 8 seconds, exceeding
    // individual cycle storage. They must still contain all 100 cycles.
    checkConcatenation(100, 50, 80, 150000, 1000.0);
    interleave::Engine engine;
    engine.prepare(100.0);
    for (int i = 0; i < 199; ++i)
        require(engine.process({0.3f, 0.3f}, {-0.4f, -0.4f}, 0.3f, -0.4f).left == 0,
                "DC waits for bounded capture");
    require(engine.process({0.3f, 0.3f}, {-0.4f, -0.4f}, 0.3f, -0.4f).left == 0.3f,
            "DC must make progress at the capture limit");
    for (int i = 0; i < 10000; ++i)
        require(std::isfinite(engine.process({}, {}, 0, 0).left), "Silence must remain finite");

    engine.reset();
    const float wave[] {-1, 0, 0, 1, 0, 0};
    int nonzero = 0;
    for (int i = 0; i < 2000; ++i)
    {
        const float v = wave[i % 6];
        const float out = engine.process({v, v}, {v, v}, v, v).left;
        if (i > 20)
        {
            const int phase = (i - 9) % 6;
            require(out == (phase == 0 ? 1.0f : (phase == 3 ? -1.0f : 0.0f)),
                    "Exact-zero plateaus must not count as additional crossings");
            nonzero += out != 0;
        }
    }
    require(nonzero > 0, "Plateau test must emit audio");
    std::cout << "Engine tests passed\n";
}
