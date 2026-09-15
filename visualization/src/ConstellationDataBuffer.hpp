#pragma once

#include <vector>
#include <complex>
#include <mutex>

/**
 * @class ConstellationDataBuffer
 * @brief Thread-safe boundary between DSP ingestion workers and Qt GUI.
 * 
 * Uses mutex-protected double buffering. The render loop provides its own
 * pre-allocated vector to copy the latest data, ensuring zero dynamic
 * allocations during the critical GUI paint events.
 */
class ConstellationDataBuffer {
public:
    using Frame = std::vector<std::complex<float>>;

    explicit ConstellationDataBuffer(size_t expected_capacity = 8192) {
        shared_data.reserve(expected_capacity);
    }

    /**
     * @brief Pushes a new frame of I/Q samples from the DSP thread.
     */
    void push(const Frame& new_data) {
        std::lock_guard<std::mutex> lock(mtx);
        shared_data = new_data; // Fast copy (or move if we optimized). Will not allocate if capacities match.
    }

    /**
     * @brief Pulls the latest frame into the GUI thread.
     * @param out_data Pre-allocated vector to prevent memory allocs.
     */
    void pull(Frame& out_data) {
        std::lock_guard<std::mutex> lock(mtx);
        out_data = shared_data; // Fast block copy. Zero allocations if out_data.capacity() >= shared_data.size()
    }

private:
    Frame shared_data;
    std::mutex mtx;
};
