// main.cpp
//
// Stage 1 entry point: load a signal via the WAVReader or IQReader, run it through the
// waterfall DSP core, and dump the result as a PPM image. No GUI yet -
// this is purely to validate the math before wiring into Qt/OpenGL.
//
// Usage:
//   ./waterfall_stage1 [path/to/signal.wav|.iq] [output.ppm]
//
// Output path is optional and defaults to output/waterfall.ppm.

#include <iostream>
#include <string>
#include <vector>
#include <filesystem>

#include "SignalData.hpp"
#include "IQReader.hpp"
#include "WAVReader.hpp"
#include "waterfall.h"

bool ends_with(const std::string& str, const std::string& suffix) {
    if (str.length() >= suffix.length()) {
        return str.compare(str.length() - suffix.length(), suffix.length(), suffix) == 0;
    }
    return false;
}

int main(int argc, char** argv) {
    if (argc < 2) {
        std::cerr << "Usage: " << argv[0] << " <path_to_signal.wav|.iq> [output.ppm]\n";
        return 1;
    }

    std::string file_path = argv[1];
    std::string out_path  = (argc > 2) ? argv[2] : "output/waterfall.ppm";

    try {
        std::cout << "Loading signal:\n"
                  << "  file:  " << file_path << "\n";

        SignalData data;
        bool loaded = false;

        if (ends_with(file_path, ".wav") || ends_with(file_path, ".WAV")) {
            loaded = WAVReader::read(file_path, data);
        } else if (ends_with(file_path, ".iq") || ends_with(file_path, ".IQ")) {
            loaded = IQReader::read(file_path, data);
        } else {
            std::cerr << "Error: Unsupported file extension. Please use .wav or .iq.\n";
            return 1;
        }

        if (!loaded) {
            std::cerr << "Error: Failed to load signal data from " << file_path << "\n";
            return 1;
        }

        std::cout << "Loaded " << data.sample_count << " samples "
                  << "(sample_rate=" << data.sample_rate << " Hz, "
                  << "source_format=" << data.source_format << ")\n";

        WaterfallParams params;
        params.fft_size = 1024;
        params.hop_size = 256;

        std::cout << "Computing waterfall (fft_size=" << params.fft_size
                  << ", hop_size=" << params.hop_size << ")...\n";

        auto rows = compute_waterfall(data.samples, params);

        std::cout << "Computed " << rows.size() << " time-slices x "
                  << (rows.empty() ? 0 : rows[0].size()) << " frequency bins\n";

        std::filesystem::path out_p(out_path);
        if (out_p.has_parent_path()) {
            std::filesystem::create_directories(out_p.parent_path());
        }

        write_ppm(out_path, rows);
        std::cout << "Wrote waterfall image to: " << out_path << "\n"
                  << "Open it with an image viewer, or convert to PNG:\n"
                  << "  convert " << out_path << " " << out_path << ".png\n";

    } catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << "\n";
        return 1;
    }

    return 0;
}
