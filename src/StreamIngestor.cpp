#include "StreamIngestor.hpp"
#include "MemoryMappedFile.hpp"
#include <iostream>
#include <cmath>
#include <algorithm>
#include <cstring>
#include <cstdint>
#include <span>

#if defined(__AVX2__) || defined(_MSC_VER)
#include <immintrin.h>
#endif

#pragma pack(push, 1)
struct WAVHeader {
    char riff_tag[4];
    uint32_t riff_size;
    char wave_tag[4];
};

struct ChunkHeader {
    char tag[4];
    uint32_t size;
};

struct FmtChunk {
    uint16_t audio_format;
    uint16_t num_channels;
    uint32_t sample_rate;
    uint32_t byte_rate;
    uint16_t block_align;
    uint16_t bits_per_sample;
};
#pragma pack(pop)

StreamIngestor::StreamIngestor(const std::string& file_path, size_t chunk_size) 
    : path(file_path), size(chunk_size) {
    buffer = std::make_shared<PingPongBuffer>(chunk_size);
}

StreamIngestor::~StreamIngestor() {
    if (worker.joinable()) {
        worker.join();
    }
}

void StreamIngestor::start() {
    if (path.length() >= 3 && path.substr(path.length() - 3) == ".iq") {
        worker = std::thread(&StreamIngestor::stream_iq, this);
    } else if (path.length() >= 4 && path.substr(path.length() - 4) == ".wav") {
        worker = std::thread(&StreamIngestor::stream_wav, this);
    } else {
        std::cerr << "[ERROR] Streamer: Unsupported format for streaming -> " << path << std::endl;
        buffer->set_done();
    }
}

bool StreamIngestor::get_next_chunk(AlignedComplexVector& out_chunk) {
    return buffer->pop(out_chunk);
}

void StreamIngestor::stream_iq() {
    MemoryMappedFile mmapped_file;
    if (!mmapped_file.open(path, 0, 0, true)) {
        std::cerr << "[ERROR] Streamer: Failed to mmap file -> " << path << std::endl;
        buffer->set_done();
        return;
    }

    uint64_t file_size = mmapped_file.file_size();
    constexpr size_t BYTES_PER_SAMPLE = sizeof(float) * 2;
    size_t total_samples = static_cast<size_t>(file_size / BYTES_PER_SAMPLE);

    const float* raw_floats = reinterpret_cast<const float*>(mmapped_file.data());
    const std::complex<float>* mapped_samples = reinterpret_cast<const std::complex<float>*>(mmapped_file.data());

    size_t samples_read = 0;
    while (samples_read < total_samples) {
        size_t samples_to_read = std::min(size, total_samples - samples_read);
        size_t floats_to_read = samples_to_read * 2;
        
        size_t i = 0;
        size_t offset_floats = samples_read * 2;

#if defined(__AVX2__)
        const __m256i exp_mask = _mm256_set1_epi32(0x7F800000);
        // AVX2 Validation for this chunk
        for (; i + 7 < floats_to_read; i += 8) {
            __m256 v = _mm256_load_ps(&raw_floats[offset_floats + i]);
            __m256i vi = _mm256_castps_si256(v);
            __m256i exp = _mm256_and_si256(vi, exp_mask);
            __m256i is_inf_or_nan = _mm256_cmpeq_epi32(exp, exp_mask);
            if (_mm256_movemask_epi8(is_inf_or_nan) != 0) {
                std::cerr << "[FATAL] Streamer: Corrupt IQ data detected." << std::endl;
                buffer->set_done();
                return;
            }
        }
#endif

        // Scalar fallback
        for (; i < floats_to_read; ++i) {
            float val = raw_floats[offset_floats + i];
            if (std::isnan(val) || std::isinf(val)) {
                std::cerr << "[FATAL] Streamer: Corrupt IQ data detected." << std::endl;
                buffer->set_done();
                return;
            }
        }

        // Push view directly to lock-free PingPongBuffer
        buffer->push(std::span<const std::complex<float>>(mapped_samples + samples_read, samples_to_read));
        
        samples_read += samples_to_read;
    }

    buffer->set_done();
}

void StreamIngestor::stream_wav() {
    MemoryMappedFile mmapped_file;
    if (!mmapped_file.open(path, 0, 0, true)) {
        std::cerr << "[ERROR] Streamer: Failed to mmap WAV file -> " << path << std::endl;
        buffer->set_done();
        return;
    }

    uint64_t file_size = mmapped_file.file_size();
    if (file_size < sizeof(WAVHeader)) {
        std::cerr << "[ERROR] Streamer: WAV file too small." << std::endl;
        buffer->set_done();
        return;
    }

    const uint8_t* mapped_bytes = mmapped_file.data();
    const WAVHeader* header = reinterpret_cast<const WAVHeader*>(mapped_bytes);

    if (std::strncmp(header->riff_tag, "RIFF", 4) != 0 || std::strncmp(header->wave_tag, "WAVE", 4) != 0) {
        std::cerr << "[ERROR] Streamer: Invalid RIFF/WAVE header." << std::endl;
        buffer->set_done();
        return;
    }

    bool found_fmt = false;
    bool found_data = false;
    FmtChunk fmt_data{};
    const uint8_t* raw_audio_ptr = nullptr;
    uint32_t data_chunk_size = 0;

    uint64_t offset = sizeof(WAVHeader);
    while (offset + sizeof(ChunkHeader) <= file_size && (!found_data || !found_fmt)) {
        const ChunkHeader* chunk = reinterpret_cast<const ChunkHeader*>(mapped_bytes + offset);
        uint32_t chunk_size = chunk->size;

        if (offset + sizeof(ChunkHeader) + chunk_size > file_size) break;

        if (std::strncmp(chunk->tag, "fmt ", 4) == 0) {
            if (chunk_size >= sizeof(FmtChunk)) {
                std::memcpy(&fmt_data, mapped_bytes + offset + sizeof(ChunkHeader), sizeof(FmtChunk));
                found_fmt = true;
            }
        } else if (std::strncmp(chunk->tag, "data", 4) == 0) {
            raw_audio_ptr = mapped_bytes + offset + sizeof(ChunkHeader);
            data_chunk_size = chunk_size;
            found_data = true;
        }

        uint32_t padded_chunk_size = chunk_size + (chunk_size % 2);
        offset += sizeof(ChunkHeader) + padded_chunk_size;
    }

    if (!found_fmt || !found_data || !raw_audio_ptr) {
        std::cerr << "[ERROR] Streamer: Missing fmt or data chunk in WAV file." << std::endl;
        buffer->set_done();
        return;
    }

    size_t bytes_per_sample = fmt_data.bits_per_sample / 8;
    size_t num_channels = fmt_data.num_channels;
    if (num_channels == 0 || bytes_per_sample == 0) {
        buffer->set_done();
        return;
    }

    size_t total_samples = data_chunk_size / (bytes_per_sample * num_channels);
    size_t samples_processed = 0;

    AlignedComplexVector chunk;

    while (samples_processed < total_samples) {
        size_t samples_to_process = std::min(size, total_samples - samples_processed);
        chunk.clear();
        chunk.reserve(samples_to_process);

        const uint8_t* sample_ptr = raw_audio_ptr + (samples_processed * num_channels * bytes_per_sample);

        for (size_t s = 0; s < samples_to_process; ++s) {
            float I = 0.0f;
            float Q = 0.0f;

            if (fmt_data.audio_format == 1 && fmt_data.bits_per_sample == 16) {
                const int16_t* pcm_samples = reinterpret_cast<const int16_t*>(sample_ptr);
                I = static_cast<float>(pcm_samples[0]) / 32768.0f;
                if (num_channels == 2) Q = static_cast<float>(pcm_samples[1]) / 32768.0f;
            } else if (fmt_data.audio_format == 3 && fmt_data.bits_per_sample == 32) {
                const float* float_samples = reinterpret_cast<const float*>(sample_ptr);
                I = float_samples[0];
                if (num_channels == 2) Q = float_samples[1];
            }

            sample_ptr += (bytes_per_sample * num_channels);
            chunk.push_back(std::complex<float>(I, Q));
        }

        buffer->push(std::span<const std::complex<float>>(chunk));
        samples_processed += samples_to_process;
    }

    buffer->set_done();
}
