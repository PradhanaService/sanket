#include "MemoryMappedFile.hpp"
#include <iostream>
#include <utility>

#if defined(_WIN32) || defined(_WIN64)
    #define WIN32_LEAN_AND_MEAN
    #include <windows.h>
#else
    #include <sys/mman.h>
    #include <sys/stat.h>
    #include <fcntl.h>
    #include <unistd.h>
#endif

MemoryMappedFile::MemoryMappedFile(const std::string& path, bool read_only) {
    open(path, 0, 0, read_only);
}

MemoryMappedFile::~MemoryMappedFile() {
    close();
}

MemoryMappedFile::MemoryMappedFile(MemoryMappedFile&& other) noexcept {
    *this = std::move(other);
}

MemoryMappedFile& MemoryMappedFile::operator=(MemoryMappedFile&& other) noexcept {
    if (this != &other) {
        close();

        data_ptr = other.data_ptr;
        mapped_size = other.mapped_size;
        total_file_size = other.total_file_size;

#if defined(_WIN32) || defined(_WIN64)
        file_handle = other.file_handle;
        mapping_handle = other.mapping_handle;
        other.file_handle = nullptr;
        other.mapping_handle = nullptr;
#else
        fd = other.fd;
        other.fd = -1;
#endif

        other.data_ptr = nullptr;
        other.mapped_size = 0;
        other.total_file_size = 0;
    }
    return *this;
}

bool MemoryMappedFile::open(const std::string& path, size_t length, uint64_t offset, bool read_only) {
    close();

#if defined(_WIN32) || defined(_WIN64)
    DWORD access = read_only ? GENERIC_READ : (GENERIC_READ | GENERIC_WRITE);
    DWORD share_mode = FILE_SHARE_READ;
    DWORD disposition = OPEN_EXISTING;
    DWORD flags = FILE_ATTRIBUTE_NORMAL | FILE_FLAG_SEQUENTIAL_SCAN;

    HANDLE hFile = CreateFileA(path.c_str(), access, share_mode, NULL, disposition, flags, NULL);
    if (hFile == INVALID_HANDLE_VALUE) {
        std::cerr << "[ERROR] MemoryMappedFile: Failed to open file -> " << path << std::endl;
        return false;
    }

    LARGE_INTEGER fs;
    if (!GetFileSizeEx(hFile, &fs)) {
        std::cerr << "[ERROR] MemoryMappedFile: Failed to query file size -> " << path << std::endl;
        CloseHandle(hFile);
        return false;
    }
    total_file_size = static_cast<uint64_t>(fs.QuadPart);

    if (total_file_size == 0) {
        std::cerr << "[ERROR] MemoryMappedFile: File is empty -> " << path << std::endl;
        CloseHandle(hFile);
        return false;
    }

    if (length == 0) {
        mapped_size = static_cast<size_t>(total_file_size - offset);
    } else {
        mapped_size = length;
    }

    DWORD page_protect = read_only ? PAGE_READONLY : PAGE_READWRITE;
    HANDLE hMap = CreateFileMappingA(hFile, NULL, page_protect, 0, 0, NULL);
    if (!hMap) {
        std::cerr << "[ERROR] MemoryMappedFile: CreateFileMapping failed for -> " << path << std::endl;
        CloseHandle(hFile);
        return false;
    }

    DWORD map_access = read_only ? FILE_MAP_READ : (FILE_MAP_READ | FILE_MAP_WRITE);
    DWORD offset_high = static_cast<DWORD>(offset >> 32);
    DWORD offset_low = static_cast<DWORD>(offset & 0xFFFFFFFF);

    void* pMem = MapViewOfFile(hMap, map_access, offset_high, offset_low, mapped_size);
    if (!pMem) {
        std::cerr << "[ERROR] MemoryMappedFile: MapViewOfFile failed for -> " << path << std::endl;
        CloseHandle(hMap);
        CloseHandle(hFile);
        return false;
    }

    file_handle = static_cast<void*>(hFile);
    mapping_handle = static_cast<void*>(hMap);
    data_ptr = pMem;
    return true;

#else
    int flags = read_only ? O_RDONLY : O_RDWR;
    fd = ::open(path.c_str(), flags);
    if (fd == -1) {
        std::cerr << "[ERROR] MemoryMappedFile: Failed to open file -> " << path << std::endl;
        return false;
    }

    struct stat st;
    if (::fstat(fd, &st) == -1) {
        std::cerr << "[ERROR] MemoryMappedFile: fstat failed -> " << path << std::endl;
        ::close(fd);
        fd = -1;
        return false;
    }
    total_file_size = static_cast<uint64_t>(st.st_size);

    if (total_file_size == 0) {
        std::cerr << "[ERROR] MemoryMappedFile: File is empty -> " << path << std::endl;
        ::close(fd);
        fd = -1;
        return false;
    }

    if (length == 0) {
        mapped_size = static_cast<size_t>(total_file_size - offset);
    } else {
        mapped_size = length;
    }

    int prot = read_only ? PROT_READ : (PROT_READ | PROT_WRITE);
    int map_flags = read_only ? MAP_PRIVATE : MAP_SHARED;
    void* pMem = ::mmap(nullptr, mapped_size, prot, map_flags, fd, static_cast<off_t>(offset));
    if (pMem == MAP_FAILED) {
        std::cerr << "[ERROR] MemoryMappedFile: mmap failed -> " << path << std::endl;
        ::close(fd);
        fd = -1;
        return false;
    }

    data_ptr = pMem;
    return true;
#endif
}

void MemoryMappedFile::close() {
    if (!data_ptr) return;

#if defined(_WIN32) || defined(_WIN64)
    if (data_ptr) {
        UnmapViewOfFile(data_ptr);
        data_ptr = nullptr;
    }
    if (mapping_handle) {
        CloseHandle(static_cast<HANDLE>(mapping_handle));
        mapping_handle = nullptr;
    }
    if (file_handle) {
        CloseHandle(static_cast<HANDLE>(file_handle));
        file_handle = nullptr;
    }
#else
    if (data_ptr) {
        ::munmap(data_ptr, mapped_size);
        data_ptr = nullptr;
    }
    if (fd != -1) {
        ::close(fd);
        fd = -1;
    }
#endif

    mapped_size = 0;
    total_file_size = 0;
}
