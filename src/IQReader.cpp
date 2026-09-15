#include "IQReader.hpp"
#include "MemoryMappedFile.hpp"
#include <iostream>
#include <cmath>
#include <stdexcept>
#include <cstdint>

#if defined(__AVX2__) || defined(_MSC_VER)
#include <immintrin.h>
#endif

static std::string extract_filename(const std::string& filepath) {
    size_t pos = filepath.find_last_of("/\\");
    return (pos == std::string::npos) ? filepath : filepath.substr(pos + 1);
}

bool IQReader::read(const std::string& file_path, SignalData& out_signal) {
    try {
        // 1. Zero-Copy Memory Mapping across POSIX and Windows
        MemoryMappedFile mmapped_file;
        if (!mmapped_file.open(file_path, 0, 0, true)) {
            std::cerr << "[ERROR] System: Failed to zero-copy mmap file -> " << file_path << std::endl;
            return false;
        }

        uint64_t file_size = mmapped_file.file_size();
        if (file_size == 0) {
            std::cerr << "[ERROR] Validation: File is empty -> " << file_path << std::endl;
            return false;
        }
        
        // 2. Format integrity check (Float32 I + Float32 Q = 8 bytes per sample)
        constexpr size_t BYTES_PER_SAMPLE = sizeof(float) * 2;
        if (file_size % BYTES_PER_SAMPLE != 0) {
            std::cerr << "[ERROR] Validation: File size is not a multiple of 8 bytes (corrupt or invalid IQ format) -> " << file_path << std::endl;
            return false;
        }

        size_t num_samples = static_cast<size_t>(file_size / BYTES_PER_SAMPLE);

        // Cast mapped memory pointer directly (Zero-Copy entry point)
        const std::complex<float>* mapped_samples = reinterpret_cast<const std::complex<float>*>(mmapped_file.data());

        // 3. Value Validation (Sanity Checks directly on mapped pages)
        // OS virtual memory subsystem guarantees 4096-byte page alignment for mmap/MapViewOfFile
        const float* raw_floats = reinterpret_cast<const float*>(mapped_samples);
        size_t total_floats = num_samples * 2;
        size_t i = 0;

#if defined(__AVX2__)
        // AVX2 Implementation: Validate 8 floats (4 complex samples) per clock cycle
        const __m256i exp_mask = _mm256_set1_epi32(0x7F800000);

        for (; i + 7 < total_floats; i += 8) {
            // Page-aligned mmap pointer allows zero-penalty aligned SIMD load
            __m256 v = _mm256_load_ps(&raw_floats[i]);
            __m256i vi = _mm256_castps_si256(v);
            __m256i exp = _mm256_and_si256(vi, exp_mask);
            __m256i is_inf_or_nan = _mm256_cmpeq_epi32(exp, exp_mask);
            
            if (_mm256_movemask_epi8(is_inf_or_nan) != 0) {
                std::cerr << "[ERROR] Validation: Invalid sample data (NaN or Infinity) detected near index " << (i/2) << std::endl;
                return false;
            }
        }
#endif

        // Scalar fallback for remaining floats or non-AVX2 builds
        for (; i < total_floats; ++i) {
            float val = raw_floats[i];
            if (std::isnan(val) || std::isinf(val)) {
                std::cerr << "[ERROR] Validation: Invalid sample data (NaN or Infinity) detected near index " << (i/2) << std::endl;
                return false;
            }
        }

        // 4. Zero-Copy: transfer ownership of the mapped file handle to SignalData
        out_signal.mapped_file = std::make_shared<MemoryMappedFile>(std::move(mmapped_file));
        out_signal.raw_mapped_samples = reinterpret_cast<const std::complex<float>*>(out_signal.mapped_file->data());
        out_signal.samples = std::span<const std::complex<float>>(out_signal.raw_mapped_samples, num_samples);

        // 5. Success: Populate the output container
        out_signal.sample_count = num_samples;
        out_signal.source_format = "IQ";
        out_signal.file_name = extract_filename(file_path);
        
        std::cout << "[INFO] Successfully validated and zero-copy mapped " << num_samples << " samples from " << file_path << std::endl;
        return true;

    } catch (const std::exception& e) {
        std::cerr << "[FATAL] Unexpected exception during file ingestion: " << e.what() << std::endl;
        return false;
    }
}
