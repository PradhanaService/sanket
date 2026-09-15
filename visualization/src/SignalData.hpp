#pragma once
#include <vector>
#include <complex>
#include <string>
#include <cstdlib>
#include <new>
#include <limits>
#include <span>
#include <memory>
#include "MemoryMappedFile.hpp"

/**
 * Custom allocator that enforces a strict memory alignment boundary.
 * Essential for zero-penalty AVX2 256-bit SIMD intrinsics (_mm256_load_ps).
 */
#if defined(_WIN32) || defined(_WIN64)
#include <malloc.h>
#endif

template <typename T, std::size_t Alignment>
struct AlignedAllocator {
    using value_type = T;

    AlignedAllocator() noexcept = default;

    template <typename U>
    AlignedAllocator(const AlignedAllocator<U, Alignment>&) noexcept {}

    T* allocate(std::size_t n) {
        if (n == 0) return nullptr;
        if (n > std::numeric_limits<std::size_t>::max() / sizeof(T))
            throw std::bad_alloc();
        
        void* ptr = nullptr;
#if defined(_WIN32) || defined(_WIN64)
        ptr = _aligned_malloc(n * sizeof(T), Alignment);
        if (!ptr) {
            throw std::bad_alloc();
        }
#else
        if (posix_memalign(&ptr, Alignment, n * sizeof(T)) != 0) {
            throw std::bad_alloc();
        }
#endif
        return static_cast<T*>(ptr);
    }

    void deallocate(T* p, std::size_t) noexcept {
#if defined(_WIN32) || defined(_WIN64)
        _aligned_free(p);
#else
        free(p);
#endif
    }

    template <typename U>
    struct rebind {
        using other = AlignedAllocator<U, Alignment>;
    };
};

template <typename T, typename U, std::size_t A>
bool operator==(const AlignedAllocator<T, A>&, const AlignedAllocator<U, A>&) { return true; }

template <typename T, typename U, std::size_t A>
bool operator!=(const AlignedAllocator<T, A>&, const AlignedAllocator<U, A>&) { return false; }

// Typedef for a vector of complex floats perfectly aligned to 32 bytes (256 bits)
using AlignedComplexVector = std::vector<std::complex<float>, AlignedAllocator<std::complex<float>, 32>>;

struct SignalData {
    // For true zero-copy of natively formatted files (like .iq)
    std::shared_ptr<MemoryMappedFile> mapped_file;
    const std::complex<float>* raw_mapped_samples = nullptr;
    
    // For formats that require conversion (like .wav PCM)
    AlignedComplexVector backing_samples;
    std::shared_ptr<MemoryMappedFile> converted_mapped_file;
    
    // Unified span-based accessor acting as the primary interface for DSP engines
    std::span<const std::complex<float>> samples;

    // For on-the-fly conversion of huge WAV files
    const uint8_t* raw_wav_data = nullptr;
    size_t wav_bytes_per_sample = 0;
    size_t wav_num_channels = 0;
    uint16_t wav_audio_format = 0; // 1 = PCM, 3 = Float

    double sample_rate = 0.0;
    double center_frequency = 0.0;
    size_t sample_count = 0;
    std::string source_format;
    std::string file_name;
};
