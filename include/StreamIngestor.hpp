#pragma once
#include "PingPongBuffer.hpp"
#include <memory>
#include <thread>
#include <string>

class StreamIngestor {
public:
    StreamIngestor(const std::string& file_path, size_t chunk_size = 65536);
    ~StreamIngestor();

    // Start the background streaming thread
    void start();

    // Pull the next chunk from the ping-pong buffer (returns false if EOF)
    bool get_next_chunk(AlignedComplexVector& out_chunk);

private:
    std::string path;
    size_t size;
    std::shared_ptr<PingPongBuffer> buffer;
    std::thread worker;

    void stream_iq();
    void stream_wav(); // We will focus on IQ streaming first for simplicity
};
