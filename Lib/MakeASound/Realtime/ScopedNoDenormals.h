#pragma once

#include <cstdint>

#if defined(__SSE2__) || defined(_M_X64) || defined(_M_AMD64)
#define MAKEASOUND_DENORMALS_X86 1
#include <xmmintrin.h>
#elif defined(__aarch64__)
// clang defines __aarch64__ on Windows-on-ARM too; real cl.exe on ARM falls
// through to the no-op rather than pulling in _ReadStatusReg.
#define MAKEASOUND_DENORMALS_ARM64 1
#endif

namespace MakeASound
{

// Flushes denormals while alive and restores the host's mode after: x86 takes
// hundreds of cycles per subnormal multiply. Set on ARM too, where it is free, so
// both slices of a universal binary render identically.
class ScopedNoDenormals
{
public:
    ScopedNoDenormals() noexcept
    {
#if defined(MAKEASOUND_DENORMALS_X86)
        saved = _mm_getcsr();
        _mm_setcsr(saved | flushBits);
#elif defined(MAKEASOUND_DENORMALS_ARM64)
        saved = readFpcr();
        writeFpcr(saved | flushBit);
#endif
    }

    ~ScopedNoDenormals() noexcept
    {
#if defined(MAKEASOUND_DENORMALS_X86)
        _mm_setcsr(saved);
#elif defined(MAKEASOUND_DENORMALS_ARM64)
        writeFpcr(saved);
#endif
    }

    ScopedNoDenormals(const ScopedNoDenormals&) = delete;
    ScopedNoDenormals& operator=(const ScopedNoDenormals&) = delete;

private:
#if defined(MAKEASOUND_DENORMALS_X86)
    // MXCSR FTZ (bit 15) flushes results, DAZ (bit 6) treats subnormal operands
    // as zero; FTZ alone still pays the assist on a subnormal input.
    static constexpr unsigned int flushBits = 0x8000u | 0x0040u;

    unsigned int saved = 0;

#elif defined(MAKEASOUND_DENORMALS_ARM64)
    static constexpr std::uint64_t flushBit = std::uint64_t {1} << 24; // FPCR.FZ

    // The "memory" clobber stops the optimiser moving the guarded arithmetic's
    // loads across the mode writes, which it may since FP maths is modelled pure.
    static std::uint64_t readFpcr() noexcept
    {
        auto value = std::uint64_t {};
        __asm__ __volatile__("mrs %0, fpcr" : "=r"(value) : : "memory");
        return value;
    }

    static void writeFpcr(std::uint64_t value) noexcept
    { __asm__ __volatile__("msr fpcr, %0" : : "r"(value) : "memory"); }

    std::uint64_t saved = 0;
#endif
};

} // namespace MakeASound
