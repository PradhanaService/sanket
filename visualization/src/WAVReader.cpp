#include "WAVReader.hpp"
#include "MemoryMappedFile.hpp"
#include <iostream>
#include <cstdint>
#include <cstring>
#include <stdexcept>
#include <filesystem>
#include <fstream>
#include <random>

#pragma pack(push, 1)
struct WAVHeader {
    char riff_tag[4];      // "RIFF"
    uint32_t riff_size;
    char wave_tag[4];      // "WAVE"
};

struct ChunkHeader {
    char tag[4];
    uint32_t size;
};

struct FmtChunk {
    uint16_t audio_format; // 1 = PCM, 3 = IEEE Float
    uint16_t num_channels;
    uint32_t sample_rate;
    uint32_t byte_rate;
    uint16_t block_align;
    uint16_t bits_per_sample;
};
#pragma pack(pop)

static std::string extract_filename(const std::string& filepath) {
    size_t pos = filepath.find_last_of("/\\");
    return (pos == std::string::npos) ? filepath : filepath.substr(pos + 1);
}

bool WAVReader::read(const std::string& file_path, SignalData& out_signal) {
    try {
        // 1. Zero-Copy Memory Mapping
        MemoryMappedFile mmapped_file;
        if (!mmapped_file.open(file_path, 0, 0, true)) {
            std::cerr << "[ERROR] System: Failed to mmap WAV file -> " << file_path << std::endl;
            return false;
        }

        uint64_t file_size = mmapped_file.file_size();
        if (file_size < sizeof(WAVHeader)) {
            std::cerr << "[ERROR] Validation: File too small for WAV header." << std::endl;
            return false;
        }

        const uint8_t* mapped_bytes = mmapped_file.data();

        // 2. Zero-Copy RIFF Header Validation
        const WAVHeader* header = reinterpret_cast<const WAVHeader*>(mapped_bytes);
        if (std::strncmp(header->riff_tag, "RIFF", 4) != 0 || std::strncmp(header->wave_tag, "WAVE", 4) != 0) {
            std::cerr << "[ERROR] Validation: File is not a valid RIFF/WAVE format." << std::endl;
            return false;
        }

        bool found_fmt = false;
        bool found_data = false;
        FmtChunk fmt_data{};
        
        const uint8_t* raw_audio_ptr = nullptr;
        uint32_t data_chunk_size = 0;

        // 3. Secure Zero-Copy Chunk Parser
        uint64_t offset = sizeof(WAVHeader);
        while (offset + sizeof(ChunkHeader) <= file_size && (!found_data || !found_fmt)) {
            const ChunkHeader* chunk = reinterpret_cast<const ChunkHeader*>(mapped_bytes + offset);
            uint32_t chunk_size = chunk->size;

            if (offset + sizeof(ChunkHeader) + chunk_size > file_size) {
                // Truncated chunk or invalid size claimed
                break;
            }

            if (std::strncmp(chunk->tag, "fmt ", 4) == 0) {
                if (chunk_size < sizeof(FmtChunk)) {
                    std::cerr << "[ERROR] Validation: Corrupt 'fmt ' chunk size." << std::endl;
                    return false;
                }
                std::memcpy(&fmt_data, mapped_bytes + offset + sizeof(ChunkHeader), sizeof(FmtChunk));
                found_fmt = true;
            } 
            else if (std::strncmp(chunk->tag, "data", 4) == 0) {
                raw_audio_ptr = mapped_bytes + offset + sizeof(ChunkHeader);
                data_chunk_size = chunk_size;
                found_data = true;
            }

            // Word align chunk padding (RIFF standard pads odd-sized chunks with 1 byte)
            uint32_t padded_chunk_size = chunk_size + (chunk_size % 2);
            offset += sizeof(ChunkHeader) + padded_chunk_size;
        }

        if (!found_fmt || !found_data || !raw_audio_ptr) {
            std::cerr << "[ERROR] Validation: Missing 'fmt ' or 'data' chunk." << std::endl;
            return false;
        }

        // 4. RF Format Validation (PCM 16-bit or IEEE Float 32-bit)
        if (fmt_data.audio_format != 1 && fmt_data.audio_format != 3) {
            std::cerr << "[ERROR] Format: Unsupported audio format. Expected PCM (1) or IEEE Float (3), got: " << fmt_data.audio_format << std::endl;
            return false;
        }

        size_t bytes_per_sample = fmt_data.bits_per_sample / 8;
        size_t num_channels = fmt_data.num_channels;

        if (num_channels > 2 || num_channels == 0 || bytes_per_sample == 0) {
            std::cerr << "[ERROR] Format: Unsupported channel count or bit depth." << std::endl;
            return false;
        }

        size_t total_samples = data_chunk_size / (bytes_per_sample * num_channels);

        // 5. Populate the output container directly from the memory map (True zero-copy on-the-fly)
        out_signal.mapped_file = std::make_shared<MemoryMappedFile>(std::move(mmapped_file));
        out_signal.raw_wav_data = raw_audio_ptr;
        out_signal.wav_bytes_per_sample = bytes_per_sample;
        out_signal.wav_num_channels = num_channels;
        out_signal.wav_audio_format = fmt_data.audio_format;
        out_signal.sample_count = total_samples;
        
        // Note: For WAV, samples span is empty. The DSP thread will convert chunks on the fly.
        out_signal.samples = std::span<const std::complex<float>>();
        out_signal.sample_count = total_samples;
        out_signal.sample_rate = fmt_data.sample_rate;
        out_signal.source_format = "WAV";
        out_signal.file_name = extract_filename(file_path);
        
        std::cout << "[INFO] Successfully validated and zero-copy parsed " << total_samples << " samples from " << file_path << std::endl;
        return true;

    } catch (const std::exception& e) {
        std::cerr << "[FATAL] Unexpected exception during WAV ingestion: " << e.what() << std::endl;
        return false;
    }
}
