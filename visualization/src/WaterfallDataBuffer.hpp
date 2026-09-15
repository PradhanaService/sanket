#pragma once

#include <vector>
#include <mutex>
#include <queue>

/**
 * @class WaterfallDataBuffer
 * @brief Thread-safe queue between DSP ingestion workers and Qt GUI for waterfall rows.
 * 
 * Uses a mutex to safely push new FFT magnitude rows and pop them for rendering.
 */
class WaterfallDataBuffer {
public:
    using Row = std::vector<float>;

    WaterfallDataBuffer() = default;

    /**
     * @brief Pushes a new computed magnitude row from the DSP thread.
     */
    void push(Row row) {
        std::lock_guard<std::mutex> lock(mtx);
        m_latest = row; // Keep a copy of the latest row for the Spectrum plot
        queue.push(std::move(row));
    }

    /**
     * @brief Pulls all available rows into the GUI thread (consumes them).
     * @param out_rows Destination vector to append the pulled rows to.
     */
    void pull_all(std::vector<Row>& out_rows) {
        std::lock_guard<std::mutex> lock(mtx);
        while (!queue.empty()) {
            out_rows.push_back(std::move(queue.front()));
            queue.pop();
        }
    }

    /**
     * @brief Gets the latest row without removing anything from the queue.
     */
    bool get_latest(Row& out_row) {
        std::lock_guard<std::mutex> lock(mtx);
        if (m_latest.empty()) return false;
        out_row = m_latest;
        return true;
    }

private:
    std::queue<Row> queue;
    Row m_latest;
    std::mutex mtx;
};
