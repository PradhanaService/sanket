#pragma once

#include "SignalData.hpp"
#include <vector>
#include <cstddef>
#include <complex>

#ifdef NO_FFTW3
typedef float fftwf_complex[2];
typedef void* fftwf_plan;
#define FFTW_FORWARD -1
#define FFTW_MEASURE 0
#else
#include <fftw3.h>
#endif

class SpectrumAnalyzer {
public:
    /**
     * Initializes the SpectrumAnalyzer for a specific chunk size.
     * Generates the FFTW plan optimized for the current hardware.
     * 
     * @param chunk_size The number of samples per FFT window
     */
    SpectrumAnalyzer(size_t chunk_size = 65536);

    /**
     * Cleans up FFTW plans and memory.
     */
    ~SpectrumAnalyzer();

    /**
     * Computes the Fast Fourier Transform (FFT) on the provided chunk.
     * Also applies a windowing function (e.g. Hamming) internally before the FFT.
     * 
     * @param input_chunk The raw time-domain complex data from the StreamIngestor
     * @param out_magnitude_db The resulting frequency spectrum magnitude in Decibels (dB)
     */
    void process_chunk(const AlignedComplexVector& input_chunk, std::vector<float>& out_magnitude_db);

private:
    size_t size;
    
    // FFTW buffers and plan
    fftwf_complex* fft_in;
    fftwf_complex* fft_out;
    fftwf_plan plan;
    
    // Pre-calculated windowing function (e.g., Hamming window)
    std::vector<float> window;

    // Generates the windowing function array
    void initialize_window();
};
