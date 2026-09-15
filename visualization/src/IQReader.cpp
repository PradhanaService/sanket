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

        // 3. (Removed) Value Validation
        // We previously scanned the entire file for NaNs/Infinities.
        // For massive files (500GB+), this defeats mmap's lazy-loading, forces the OS to page the entire file into RAM,
        // and freezes the GUI. We must trust the file contents and let the DSP thread handle issues on-the-fly.

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
