// waterfall.h
//
// Core DSP for turning a complex I/Q stream into a waterfall (time x
// frequency magnitude) matrix. Deliberately has no rendering code in it —
// Stage 1 dumps the result to a PPM image, Stage 2/3 will feed the same
// `compute_waterfall()` output into a Qt/OpenGL renderer instead. The DSP
// core does not change between stages.

#pragma once

#include <complex>
#include <string>
#include <vector>
#include <span>

struct WaterfallParams {
    size_t fft_size = 1024;   // FFT window length
    size_t hop_size = 256;    // step between windows (overlap = fft_size - hop_size)
};

// Returns a Hann window of length n. Reduces spectral leakage before FFT.
std::vector<float> hann_window(size_t n);

// Slices `samples` into overlapping windows, applies a Hann window, runs an
// FFT on each, and returns one magnitude-in-dB row per window.
// Result layout: rows[time_index][frequency_bin] -> magnitude (dB).
std::vector<std::vector<float>> compute_waterfall(
    std::span<const std::complex<float>> samples,
    const WaterfallParams& params);

// Streaming processor for real-time applications to avoid recreating FFTW plans
class WaterfallProcessor {
public:
    WaterfallProcessor(const WaterfallParams& params);
    ~WaterfallProcessor();
    
    // Process exactly one chunk (must be size >= fft_size) and return one row
    std::vector<float> process_chunk(std::span<const std::complex<float>> chunk);

private:
    WaterfallParams p;
    std::vector<float> window;
    void* plan = nullptr; // void* to avoid exposing fftwf_plan in header
    void* in_buf = nullptr;
    void* out_buf = nullptr;
};

// Writes a waterfall matrix out as a grayscale binary PPM (P6) image for
// quick visual sanity-checking without any GUI/Qt dependency.
void write_ppm(const std::string& path,
               const std::vector<std::vector<float>>& rows);
