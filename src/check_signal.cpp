#include "IQReader.hpp"
#include "WAVReader.hpp"
#include <iostream>
#include <iomanip>
#include <vector>
#include <complex>
#include <algorithm>

int main(int argc, char** argv) {
    std::string iq_path = (argc > 1) ? argv[1] : "test_data/gr_signal.iq";
    std::string wav_path = (argc > 2) ? argv[2] : "test_data/gr_signal.wav";

    std::cout << "================ C++ SIGNAL VERIFICATION ================" << std::endl;

    // 1. Verify IQ Signal
    SignalData iq_signal;
    if (IQReader::read(iq_path, iq_signal)) {
        std::cout << "\n--- IQ FILE CHECK (" << iq_path << ") ---" << std::endl;
        std::cout << "Total Samples: " << iq_signal.sample_count << std::endl;
        std::cout << "First 5 IQ samples:" << std::endl;
        size_t count = std::min<size_t>(5, iq_signal.samples.size());
        float real_min = 1e9f, real_max = -1e9f;
        for (size_t i = 0; i < iq_signal.samples.size(); ++i) {
            float r = iq_signal.samples[i].real();
            if (r < real_min) real_min = r;
            if (r > real_max) real_max = r;
            if (i < count) {
                std::cout << "  [" << i << "]: " << iq_signal.samples[i].real()
                          << " + j" << iq_signal.samples[i].imag() << std::endl;
            }
        }
        std::cout << "IQ Real Min: " << real_min << ", Max: " << real_max << std::endl;
    } else {
        std::cerr << "[ERROR] Failed to read IQ file: " << iq_path << std::endl;
    }

    // 2. Verify WAV Signal
    SignalData wav_signal;
    if (WAVReader::read(wav_path, wav_signal)) {
        std::cout << "\n--- WAV FILE CHECK (" << wav_path << ") ---" << std::endl;
        std::cout << "Total Samples: " << wav_signal.sample_count << std::endl;
        std::cout << "First 5 WAV samples (I / Q):" << std::endl;
        size_t count = std::min<size_t>(5, wav_signal.samples.size());
        for (size_t i = 0; i < count; ++i) {
            std::cout << "  [" << i << "]: I=" << wav_signal.samples[i].real()
                      << ", Q=" << wav_signal.samples[i].imag() << std::endl;
        }
    } else {
        std::cerr << "[ERROR] Failed to read WAV file: " << wav_path << std::endl;
    }

    std::cout << "\n========================================================" << std::endl;
    return 0;
}
