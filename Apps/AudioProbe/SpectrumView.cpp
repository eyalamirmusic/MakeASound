#include "SpectrumView.h"

namespace AudioProbe
{
namespace
{
// Two triangles covering clip space, so every pixel of the view is a fragment
// and the whole picture is made in the fragment stage.
const SpectrumVertex quadVertices[] = {
    {{-1.f, -1.f}},
    {{1.f, -1.f}},
    {{-1.f, 1.f}},

    {{-1.f, 1.f}},
    {{1.f, -1.f}},
    {{1.f, 1.f}},
};

constexpr auto binCount = Analyser::binCount;
constexpr auto spectrumBytes = static_cast<int>(sizeof(float) * binCount);
} // namespace

void SpectrumShader::define()
{
    auto position = vertexInput(&SpectrumVertex::position);
    auto uv = varying(position);

    setPosition(float4(position, 0.f, 1.f));

    // Clip space is -1..1 with y up; the picture is easier to write in the space
    // it means: x frequency, y level, both 0..1.
    auto x = uv.x() * 0.5f + 0.5f;
    auto y = uv.y() * 0.5f + 0.5f;

    // Mixing between neighbouring bins is what stops the curve being a visible
    // staircase once the view is wider than binCount pixels.
    auto slot = clamp(x, 0.f, 1.f) * static_cast<float>(binCount - 1);
    auto lower = floor(slot);
    auto index = toUInt(lower);

    auto height = mix(spectrum[index],
                      spectrum[min(index + 1u, static_cast<unsigned>(binCount - 1))],
                      slot - lower)
                  * 0.92f;

    auto above = y - height;

    // Three layers, added rather than composited: the body under the curve, a
    // glow that only reaches upwards and only as far as the bin is loud, and the
    // lit edge on the curve itself.
    auto edge = 0.005f;
    auto fill = 1.f - smoothstep(height - edge, height + edge, y);
    auto bloom = exp(max(above, 0.f) * -12.f) * height;
    auto ridge = exp(abs(above) * -64.f);

    auto tint = mix(float3(constant(0.20f), 0.55f, 1.f),
                    float3(constant(0.35f), 0.95f, 0.72f),
                    smoothstep(0.f, 0.55f, x));

    tint = mix(tint, float3(constant(1.f), 0.78f, 0.30f), smoothstep(0.55f, 1.f, x));

    // A slow wash so a silent stream still shows something alive rather than a
    // black rectangle.
    auto wash = sin(x * 4.f - time * 0.6f + y * 2.f) * 0.5f + 0.5f;
    auto backdrop =
        float3(constant(0.035f), 0.038f, 0.055f)
        + float3(constant(0.02f), 0.03f, 0.07f) * wash * (level * 0.7f + 0.3f);

    auto colour = backdrop + tint * fill * (1.f - y * 0.5f) * 0.55f
                  + tint * bloom * 0.4f
                  + mix(tint, float3(constant(1.f), 1.f, 1.f), 0.5f) * ridge;

    setFragment(float4(clamp(colour, 0.f, 1.f), 1.f));
}

SpectrumView::SpectrumView(Analyser& analyserToUse)
    : analyser(analyserToUse)
    , spectrum(Device::shared().makeBuffer(spectrumBytes, BufferUsage::Storage))
{
    shader.setVertices(quadVertices);

    // Bound once: the uniform holds the buffer, not a copy of its contents, so
    // every update below is seen by the next draw.
    shader.spectrum = spectrum;
    shader.prepare(sampleCount());

    setContinuous(true);
}

void SpectrumView::update(eacp::Threads::FrameTime frameTime)
{
    auto delta = static_cast<float>(frameTime.delta);
    elapsed += delta;

    analyser.analyse(delta, sampleRate());
}

void SpectrumView::render(Frame& frame)
{
    spectrum.update(analyser.getBins().data(), spectrumBytes);

    shader.time = elapsed;
    shader.level = analyser.getLevel();

    auto pass = frame.beginPass({eacp::Graphics::Color {0.02f, 0.02f, 0.035f}});
    pass.draw(shader);
}

} // namespace AudioProbe
