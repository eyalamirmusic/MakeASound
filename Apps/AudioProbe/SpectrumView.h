#pragma once

#include "Analyser.h"

#include <eacp/GPU/GPU.h>

#include <functional>

namespace AudioProbe
{

// Inside this namespace only, so a shader reads the way eacp's own examples do
// - float4(), mix(), Uniform<Float> - rather than being mostly qualification.
using namespace eacp::GPU;

struct SpectrumVertex
{
    float position[2];
};

// The spectrum, as a shader. `spectrum` is a storage buffer bound whole and
// subscripted by the fragment stage, which is what carries a curve to the GPU
// rather than a level: one float per display bin, uploaded once a frame.
struct SpectrumShader final : ShaderProgram
{
    SpectrumShader() { compile(); }

    void define() override;

    Uniform<InputBuffer> spectrum;
    Uniform<Float> time;
    Uniform<Float> level;

    EACP_SHADER(spectrum, time, level)
};

class SpectrumView final : public GPUView
{
public:
    explicit SpectrumView(Analyser& analyserToUse);

    // Read once per rendered frame; the analysis needs the rate the stream is
    // actually running at, not the one that was asked for.
    std::function<int()> sampleRate = [] { return 48000; };

    void update(eacp::Threads::FrameTime frameTime) override;
    void render(Frame& frame) override;

private:
    Analyser& analyser;

    SpectrumShader shader;
    Buffer spectrum;

    float elapsed = 0.f;
};

} // namespace AudioProbe
