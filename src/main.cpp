#include "DataIngestor.hpp"
#include "StreamIngestor.hpp"
#include "SpectrumAnalyzer.hpp"
#include <iostream>
#include <iomanip>
#include <string>
#include <thread>
#include <chrono>

int main(int argc, char* argv[]) {
    // Basic command line argument check
    if (argc < 2) {
        std::cerr << "Usage: " << argv[0] << " <path_to_signal_file>" << std::endl;
        return 1;
    }

    std::string file_path = argv[1];

    std::cout << "[MAIN] Starting Ping-Pong Streaming Pipeline for: " << file_path << std::endl;
    
    // 1. Initialize the Streaming Ingestor (defaults to 65536 sample chunks)
    StreamIngestor streamer(file_path);
    
    // Initialize the Spectrum Analyzer with the same chunk size
    SpectrumAnalyzer analyzer(65536);
    std::vector<float> magnitude_db;
    
    // --- BENCHMARK START ---
    auto start_time = std::chrono::high_resolution_clock::now();

    // 2. Start the background Ingestion thread
    streamer.start();

    // 3. DSP Thread (Main Thread) Consuming Loop
    AlignedComplexVector current_chunk;
    size_t chunks_processed = 0;
    size_t total_samples_processed = 0;
    
    // get_next_chunk blocks until the next Ping-Pong buffer is ready
    while (streamer.get_next_chunk(current_chunk)) {
        chunks_processed++;
        total_samples_processed += current_chunk.size();
        
        // Feed the chunk to the Spectrum Analyzer (AVX2 Windowing + FFTW + AVX2 Log10)
        analyzer.process_chunk(current_chunk, magnitude_db);
    }

    // --- BENCHMARK END ---
    auto end_time = std::chrono::high_resolution_clock::now();
    std::chrono::duration<double, std::milli> elapsed_ms = end_time - start_time;

    // Calculate Throughput
    // Each complex float sample is 8 bytes (2x 32-bit floats)
    double total_megabytes = (total_samples_processed * 8.0) / (1024.0 * 1024.0);
    double throughput_mb_s = total_megabytes / (elapsed_ms.count() / 1000.0);

    std::cout << "\n--- Streaming Complete ---" << std::endl;
    std::cout << "Chunks Processed:  " << chunks_processed << std::endl;
    std::cout << "Total Samples:     " << total_samples_processed << std::endl;
    
    std::cout << "\n--- BENCHMARK RESULTS ---" << std::endl;
    std::cout << std::fixed << std::setprecision(2);
    std::cout << "Data Processed:    " << total_megabytes << " MB" << std::endl;
    std::cout << "Elapsed Time:      " << elapsed_ms.count() << " ms" << std::endl;
    std::cout << "Throughput:        " << throughput_mb_s << " MB/s" << std::endl;

    return 0;
}
