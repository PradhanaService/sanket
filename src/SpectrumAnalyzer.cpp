#include "SpectrumAnalyzer.hpp"
#include <cmath>
#include <iostream>
#include <algorithm>
#include <cstdlib>

#if defined(__AVX2__) || defined(_MSC_VER)
#include <immintrin.h>
#endif

#ifdef NO_FFTW3
static void internal_cooley_tukey_fft(std::complex<float>* data, size_t N) {
    size_t j = 0;
    for (size_t i = 0; i < N; ++i) {
        if (i < j) std::swap(data[i], data[j]);
        size_t m = N >> 1;
        while (m >= 1 && j >= m) {
            j -= m;
            m >>= 1;
        }
        j += m;
    }
    for (size_t len = 2; len <= N; len <<= 1) {
        float angle = -2.0f * 3.14159265358979323846f / len;
        std::complex<float> wlen(std::cos(angle), std::sin(angle));
        for (size_t i = 0; i < N; i += len) {
            std::complex<float> w(1.0f, 0.0f);
            for (size_t k = 0; k < len / 2; ++k) {
                std::complex<float> u = data[i + k];
                std::complex<float> v = data[i + k + len / 2] * w;
                data[i + k] = u + v;
                data[i + k + len / 2] = u - v;
                w *= wlen;
            }
        }
    }
}
#endif

SpectrumAnalyzer::SpectrumAnalyzer(size_t chunk_size) : size(chunk_size) {
#ifdef NO_FFTW3
    fft_in = reinterpret_cast<fftwf_complex*>(std::malloc(sizeof(float) * 2 * size));
    fft_out = reinterpret_cast<fftwf_complex*>(std::malloc(sizeof(float) * 2 * size));
    plan = nullptr;
#else
    fft_in = (fftwf_complex*) fftwf_malloc(sizeof(fftwf_complex) * size);
    fft_out = (fftwf_complex*) fftwf_malloc(sizeof(fftwf_complex) * size);
    plan = fftwf_plan_dft_1d(size, fft_in, fft_out, FFTW_FORWARD, FFTW_MEASURE);
#endif
    initialize_window();
}

SpectrumAnalyzer::~SpectrumAnalyzer() {
#ifdef NO_FFTW3
    std::free(fft_in);
    std::free(fft_out);
#else
    fftwf_destroy_plan(plan);
    fftwf_free(fft_in);
    fftwf_free(fft_out);
#endif
}

void SpectrumAnalyzer::initialize_window() {
    window.resize(size);
    const double PI = 3.14159265358979323846;
    for (size_t i = 0; i < size; ++i) {
        window[i] = static_cast<float>(0.54 - 0.46 * std::cos((2.0 * PI * i) / (size - 1)));
    }
}

void SpectrumAnalyzer::process_chunk(const AlignedComplexVector& input_chunk, std::vector<float>& out_magnitude_db) {
    size_t actual_size = input_chunk.size();
    if (actual_size > size) {
        std::cerr << "[ERROR] SpectrumAnalyzer: Chunk size exceeds FFT plan!" << std::endl;
        return;
    }

    const float* raw_input = reinterpret_cast<const float*>(input_chunk.data());
    float* raw_fft_in = reinterpret_cast<float*>(fft_in);
    const float* raw_window = window.data();

    size_t total_floats = actual_size * 2;
    size_t i = 0;
    
#if defined(__AVX2__)
    for (; i + 7 < total_floats; i += 8) {
        __m256 v_in = _mm256_loadu_ps(&raw_input[i]);
        size_t w_idx = i / 2;
        __m128 w_half = _mm_loadu_ps(&raw_window[w_idx]);
        __m128 w_low = _mm_unpacklo_ps(w_half, w_half);
        __m128 w_high = _mm_unpackhi_ps(w_half, w_half);
        __m256 v_win = _mm256_insertf128_ps(_mm256_castps128_ps256(w_low), w_high, 1);
        __m256 v_out = _mm256_mul_ps(v_in, v_win);
        _mm256_storeu_ps(&raw_fft_in[i], v_out);
    }
#endif

    for (; i < total_floats; i += 2) {
        size_t w_idx = i / 2;
        raw_fft_in[i] = raw_input[i] * raw_window[w_idx];
        raw_fft_in[i + 1] = raw_input[i + 1] * raw_window[w_idx];
    }

    for (; i < size * 2; ++i) {
        raw_fft_in[i] = 0.0f;
    }

#ifdef NO_FFTW3
    std::complex<float>* cplx_in = reinterpret_cast<std::complex<float>*>(fft_in);
    std::complex<float>* cplx_out = reinterpret_cast<std::complex<float>*>(fft_out);
    std::copy(cplx_in, cplx_in + size, cplx_out);
    internal_cooley_tukey_fft(cplx_out, size);
#else
    fftwf_execute(plan);
#endif

    out_magnitude_db.resize(size);
    const float* raw_fft_out = reinterpret_cast<const float*>(fft_out);
    float* raw_mag = out_magnitude_db.data();

    size_t j = 0;
#if defined(__AVX2__)
    for (; j + 7 < size * 2; j += 8) {
        __m256 v_cplx = _mm256_loadu_ps(&raw_fft_out[j]);
        __m256 v_sq = _mm256_mul_ps(v_cplx, v_cplx);
        __m256 v_hadd = _mm256_hadd_ps(v_sq, v_sq);
        
        float mag_sq[8];
        _mm256_storeu_ps(mag_sq, v_hadd);
        
        size_t m_idx = j / 2;
        raw_mag[m_idx] = 10.0f * std::log10(std::max(mag_sq[0], 1e-10f));
        raw_mag[m_idx + 1] = 10.0f * std::log10(std::max(mag_sq[1], 1e-10f));
        raw_mag[m_idx + 2] = 10.0f * std::log10(std::max(mag_sq[4], 1e-10f));
        raw_mag[m_idx + 3] = 10.0f * std::log10(std::max(mag_sq[5], 1e-10f));
    }
#endif

    for (; j < size * 2; j += 2) {
        size_t m_idx = j / 2;
        float r = raw_fft_out[j];
        float img = raw_fft_out[j + 1];
        float mag_sq = r * r + img * img;
        raw_mag[m_idx] = 10.0f * std::log10(std::max(mag_sq, 1e-10f));
    }
}
