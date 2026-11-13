#pragma once

#include <cstdint>

namespace fxtend
{

enum class NoiseColor { white = 0, pink, brown };

/** Deterministic noise source (same seed -> same samples), roughly level-matched across colours. */
class NoiseGenerator
{
public:
    void seed (std::uint32_t newSeed) noexcept
    {
        state = newSeed != 0 ? newSeed : 0x1234567u;
        b0 = b1 = b2 = brown = 0.0f;
    }

    float white() noexcept
    {
        // xorshift32
        state ^= state << 13;
        state ^= state >> 17;
        state ^= state << 5;
        return (float) (std::int32_t) state * (1.0f / 2147483648.0f);
    }

    float next (NoiseColor color) noexcept
    {
        const float w = white();

        switch (color)
        {
            case NoiseColor::white:
                return w;

            case NoiseColor::pink:
            {
                // Paul Kellet's economy pink filter.
                b0 = 0.99765f * b0 + w * 0.0990460f;
                b1 = 0.96300f * b1 + w * 0.2965164f;
                b2 = 0.57000f * b2 + w * 1.0526913f;
                return (b0 + b1 + b2 + w * 0.1848f) * 0.33f;
            }

            case NoiseColor::brown:
                brown = (brown + 0.02f * w) / 1.02f;
                return brown * 6.0f;
        }

        return w;
    }

private:
    std::uint32_t state = 0x1234567u;
    float b0 = 0.0f, b1 = 0.0f, b2 = 0.0f, brown = 0.0f;
};

} // namespace fxtend
