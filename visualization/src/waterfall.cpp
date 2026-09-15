// waterfall.cpp
//
// Implementation of the FFT/windowing pipeline declared in waterfall.h,
// plus a dependency-free PPM writer for Stage 1 validation.

#include "waterfall.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <fstream>
#include <stdexcept>

#include <fftw3.h>

std::vector<float> hann_window(size_t n) {
    std::vector<float> w(n);
    if (n == 1) {
        w[0] = 1.0f;
        return w;
    }
    for (size_t i = 0; i < n; ++i) {
        w[i] = 0.5f * (1.0f - std::cos(2.0f * static_cast<float>(M_PI) * i / (n - 1)));
    }
    return w;
}

std::vector<std::vector<float>> compute_waterfall(
    std::span<const std::complex<float>> samples,
    const WaterfallParams& p) {

    if (p.fft_size == 0) {
        throw std::invalid_argument("fft_size must be > 0");
    }
    if (p.hop_size == 0) {
        throw std::invalid_argument("hop_size must be > 0");
    }
    if (samples.size() < p.fft_size) {
        throw std::invalid_argument(
            "Signal shorter than one FFT window - not enough samples to "
            "compute even a single waterfall row.");
    }

    const auto window = hann_window(p.fft_size);
    std::vector<std::vector<float>> rows;

    // Reuse a single FFTW plan across all windows - creating a new plan per
    // FFT call is a well-known performance killer.
    fftwf_complex* in  = fftwf_alloc_complex(static_cast<int>(p.fft_size));
    fftwf_complex* out = fftwf_alloc_complex(static_cast<int>(p.fft_size));
    fftwf_plan plan = fftwf_plan_dft_1d(
        static_cast<int>(p.fft_size), in, out, FFTW_FORWARD, FFTW_ESTIMATE);

    for (size_t start = 0; start + p.fft_size <= samples.size(); start += p.hop_size) {
        for (size_t i = 0; i < p.fft_size; ++i) {
            const std::complex<float> s = samples[start + i] * window[i];
            in[i][0] = s.real();
            in[i][1] = s.imag();
        }

        fftwf_execute(plan);

        std::vector<float> mag(p.fft_size);
        for (size_t i = 0; i < p.fft_size; ++i) {
            const float re = out[i][0];
            const float im = out[i][1];
            // dB scale - raw linear magnitude is visually unreadable.
            mag[i] = 20.0f * std::log10(std::sqrt(re * re + im * im) + 1e-9f);
        }
        rows.push_back(std::move(mag));
    }

    fftwf_destroy_plan(plan);
    fftwf_free(in);
    fftwf_free(out);

    return rows;
}

WaterfallProcessor::WaterfallProcessor(const WaterfallParams& params) : p(params) {
    window = hann_window(p.fft_size);
    in_buf = fftwf_alloc_complex(static_cast<int>(p.fft_size));
    out_buf = fftwf_alloc_complex(static_cast<int>(p.fft_size));
    plan = fftwf_plan_dft_1d(
        static_cast<int>(p.fft_size), 
        static_cast<fftwf_complex*>(in_buf), 
        static_cast<fftwf_complex*>(out_buf), 
        FFTW_FORWARD, FFTW_MEASURE);
}

WaterfallProcessor::~WaterfallProcessor() {
    if (plan) fftwf_destroy_plan(static_cast<fftwf_plan>(plan));
    if (in_buf) fftwf_free(in_buf);
    if (out_buf) fftwf_free(out_buf);
}

std::vector<float> WaterfallProcessor::process_chunk(std::span<const std::complex<float>> chunk) {
    if (chunk.size() < p.fft_size) {
        throw std::invalid_argument("Chunk size smaller than fft_size");
    }
    
    fftwf_complex* in = static_cast<fftwf_complex*>(in_buf);
    fftwf_complex* out = static_cast<fftwf_complex*>(out_buf);
    
    for (size_t i = 0; i < p.fft_size; ++i) {
        const std::complex<float> s = chunk[i] * window[i];
        in[i][0] = s.real();
        in[i][1] = s.imag();
    }

    fftwf_execute(static_cast<fftwf_plan>(plan));

    std::vector<float> mag(p.fft_size);
    for (size_t i = 0; i < p.fft_size; ++i) {
        const float re = out[i][0];
        const float im = out[i][1];
        mag[i] = 20.0f * std::log10(std::sqrt(re * re + im * im) + 1e-9f);
    }
    return mag;
}


void write_ppm(const std::string& path,
               const std::vector<std::vector<float>>& rows) {
    if (rows.empty() || rows[0].empty()) {
        throw std::invalid_argument("write_ppm: empty waterfall matrix");
    }

    const size_t h = rows.size();
    const size_t w = rows[0].size();

    // Normalize across the whole matrix so contrast is consistent.
    float minv = rows[0][0];
    float maxv = rows[0][0];
    for (const auto& r : rows) {
        for (float v : r) {
            minv = std::min(minv, v);
            maxv = std::max(maxv, v);
        }
    }
    const float range = (maxv - minv) > 1e-9f ? (maxv - minv) : 1.0f;

    std::ofstream f(path, std::ios::binary);
    if (!f) {
        throw std::runtime_error("write_ppm: could not open output file " + path);
    }

    f << "P6\n" << w << " " << h << "\n255\n";
    for (const auto& r : rows) {
        for (float v : r) {
            const uint8_t gray = static_cast<uint8_t>(
                255.0f * (v - minv) / range);
            f.put(static_cast<char>(gray));
            f.put(static_cast<char>(gray));
            f.put(static_cast<char>(gray));
        }
    }
}
