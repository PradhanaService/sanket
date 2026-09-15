#pragma once

#include <string>
#include <cstddef>
#include <cstdint>
#include <stdexcept>

/**
 * High-performance, cross-platform RAII wrapper for zero-copy memory-mapped file access.
 * Maps files directly into the virtual address space using POSIX mmap() on Unix/Linux/macOS
 * and MapViewOfFile() on Windows.
 * 
 * OS virtual memory subsystem guarantees 4096-byte page boundary alignment for zero-offset
 * mappings, making mapped pointers immediately ready for AVX2/AVX-512 SIMD operations.
 */
class MemoryMappedFile {
public:
    MemoryMappedFile() = default;
    explicit MemoryMappedFile(const std::string& path, bool read_only = true);
    ~MemoryMappedFile();

    // Non-copyable
    MemoryMappedFile(const MemoryMappedFile&) = delete;
    MemoryMappedFile& operator=(const MemoryMappedFile&) = delete;

    // Moveable
    MemoryMappedFile(MemoryMappedFile&& other) noexcept;
    MemoryMappedFile& operator=(MemoryMappedFile&& other) noexcept;

    /**
     * Map a file into virtual address space.
     * @param path File system path to the target file.
     * @param length Number of bytes to map (0 means map entire file).
     * @param offset Byte offset within the file (must be multiple of system page size, default 0).
     * @param read_only True for read-only mapping, false for read-write.
     * @return true if mapping succeeded, false otherwise.
     */
    bool open(const std::string& path, size_t length = 0, uint64_t offset = 0, bool read_only = true);

    /**
     * Unmap the file and release OS handles.
     */
    void close();

    /**
     * Check if a file is currently mapped.
     */
    bool is_mapped() const noexcept { return data_ptr != nullptr; }

    /**
     * Get pointer to mapped byte data (const).
     */
    const uint8_t* data() const noexcept { return static_cast<const uint8_t*>(data_ptr); }

    /**
     * Get pointer to mapped byte data (mutable).
     */
    uint8_t* data() noexcept { return static_cast<uint8_t*>(data_ptr); }

    /**
     * Get mapped memory size in bytes.
     */
    size_t size() const noexcept { return mapped_size; }

    /**
     * Get total file size on disk in bytes.
     */
    uint64_t file_size() const noexcept { return total_file_size; }

private:
    void* data_ptr = nullptr;
    size_t mapped_size = 0;
    uint64_t total_file_size = 0;

#if defined(_WIN32) || defined(_WIN64)
    void* file_handle = nullptr;       // HANDLE (HANDLE is void*)
    void* mapping_handle = nullptr;    // HANDLE
#else
    int fd = -1;
#endif
};
