#pragma once

#include <MakeASound/MakeASound.h>

#include <array>

namespace AudioProbe
{

// The FFT, and the two threads it sits between.
//
// The audio callback pushes mono samples into MakeASound's own SPSC queue and
// does nothing else; the transform runs on the render thread once per displayed
// frame, so the picture refreshes as often as it is drawn rather than as often
// as a block arrives. A full queue means nobody is drawing, and the writes are
// dropped.
class Analyser
{
public:
    static constexpr auto fftOrder = 11;
    static constexpr auto fftSize = 1 << fftOrder;

    // Display bins: a log-frequency regrouping of the linear FFT bins, so an
    // octave takes the same width at the bottom of the picture as at the top.
    static constexpr auto binCount = 96;

    // Audio thread.
    void push(float sample) noexcept { fifo.push(sample); }

    // Render thread, once per displayed frame.
    void analyse(float deltaSeconds, int sampleRate);

    const std::array<float, binCount>& getBins() const noexcept { return bins; }
    float getLevel() const noexcept { return level; }

private:
    struct Band
    {
        int first = 1;
        int last = 1;
    };

    void rebuildBands(int rate);
    bool drainIntoRing();
    void transform();

    MakeASound::SPSCQueue<float, fftSize * 2> fifo;

    std::array<float, fftSize> ring {};
    int ringWrite = 0;

    std::array<float, fftSize> hann {};
    std::array<float, fftSize> real {};
    std::array<float, fftSize> imag {};

    std::array<Band, binCount> bands {};
    std::array<float, binCount> measured {};
    std::array<float, binCount> bins {};

    float level = 0.f;
    int bandRate = 0;
    bool windowBuilt = false;
};

} // namespace AudioProbe
