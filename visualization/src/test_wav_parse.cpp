#include <iostream>
#include "WAVReader.hpp"
#include "SignalData.hpp"

int main() {
    SignalData data;
    if (WAVReader::read("../../test_data/gr_signal.wav", data)) {
        std::cout << "--- C++ WAVReader CHECK ---\n";
        std::cout << "Sample rate: " << data.sample_rate << "\n";
        std::cout << "First 5 samples:\n";
        for(int i=0; i<5 && i<data.samples.size(); ++i) {
            std::cout << data.samples[i].real() << " + j" << data.samples[i].imag() << "\n";
        }
        
        float min_i = data.samples[0].real(), max_i = data.samples[0].real();
        for(auto& s : data.samples) {
            if (s.real() < min_i) min_i = s.real();
            if (s.real() > max_i) max_i = s.real();
        }
        std::cout << "Min I: " << min_i << ", Max I: " << max_i << "\n";
    }
    return 0;
}
