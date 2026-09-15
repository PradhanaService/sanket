#pragma once
#include "SignalData.hpp"
#include <atomic>
#include <array>
#include <span>
#include <algorithm>

class PingPongBuffer {
public:
    PingPongBuffer(size_t chunk_size = 65536) {
        buffers[0].resize(chunk_size);
        buffers[1].resize(chunk_size);
        write_count.store(0, std::memory_order_relaxed);
        read_count.store(0, std::memory_order_relaxed);
        is_done.store(false, std::memory_order_relaxed);
    }

    // Producer writes a chunk (accepts span for zero-copy streaming)
    void push(std::span<const std::complex<float>> chunk_view) {
        auto wc = write_count.load(std::memory_order_acquire);
        
        // Wait until there is space to write
        while (wc - read_count.load(std::memory_order_acquire) >= 2) {
            read_count.wait(wc - 2, std::memory_order_acquire);
            wc = write_count.load(std::memory_order_acquire);
        }

        size_t idx = wc % 2;
        auto& dest = buffers[idx];
        
        if (dest.size() != chunk_view.size()) {
            dest.resize(chunk_view.size());
        }
        std::copy(chunk_view.begin(), chunk_view.end(), dest.begin());
        
        write_count.fetch_add(1, std::memory_order_release);
        write_count.notify_one();
    }

    // Consumer reads a chunk
    bool pop(AlignedComplexVector& out_chunk) {
        auto rc = read_count.load(std::memory_order_acquire);
        
        while (rc >= write_count.load(std::memory_order_acquire)) {
            if (is_done.load(std::memory_order_acquire)) {
                if (rc >= write_count.load(std::memory_order_acquire)) {
                    return false; // EOF
                }
            }
            write_count.wait(rc, std::memory_order_acquire);
        }

        size_t idx = rc % 2;
        auto& src = buffers[idx];
        
        // Efficient swap/move if possible, or copy if sizes match
        out_chunk = std::move(src);
        
        read_count.fetch_add(1, std::memory_order_release);
        read_count.notify_one();
        return true;
    }

    // Producer signals that no more chunks will be pushed
    void set_done() {
        is_done.store(true, std::memory_order_release);
        write_count.notify_all();
    }

private:
    std::array<AlignedComplexVector, 2> buffers;
    
    // Prevent false sharing by putting atomics on separate cache lines
    alignas(64) std::atomic<uint64_t> write_count;
    alignas(64) std::atomic<uint64_t> read_count;
    alignas(64) std::atomic<bool> is_done;
};
