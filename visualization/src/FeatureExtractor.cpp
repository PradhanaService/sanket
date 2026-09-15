#include "FeatureExtractor.hpp"
#include <algorithm>
#if defined(__AVX2__) || defined(_MSC_VER)
#include <immintrin.h>
#endif

SignalFeatures FeatureExtractor::extract_time(const std::vector<std::complex<float>>& time_samples) {
    SignalFeatures features;
    size_t n = time_samples.size();
    if (n == 0) return features;

    // Time/Constellation Analysis (Pseudo-EVM & Modulation)
    float max_amp = 0.0f;
    float sum_amp = 0.0f;
    float variance_amp = 0.0f;
    size_t i = 0;

#if defined(__AVX2__)
    const float* raw = reinterpret_cast<const float*>(time_samples.data());
    __m256 sum_vec = _mm256_setzero_ps();
    __m256 max_vec = _mm256_setzero_ps();

    for (; i + 7 < n; i += 8) {
        __m256 v1 = _mm256_loadu_ps(raw + i * 2);      // [I0, Q0, I1, Q1, I2, Q2, I3, Q3]
        __m256 v2 = _mm256_loadu_ps(raw + i * 2 + 8);  // [I4, Q4, I5, Q5, I6, Q6, I7, Q7]
        
        __m256 v1_sq = _mm256_mul_ps(v1, v1);
        __m256 v2_sq = _mm256_mul_ps(v2, v2);

        // Horizontal add pairs: [I0^2+Q0^2, I1^2+Q1^2, I0^2+Q0^2, ...]
        __m256 hadd1 = _mm256_hadd_ps(v1_sq, v1_sq); 
        __m256 hadd2 = _mm256_hadd_ps(v2_sq, v2_sq);
        
        __m256 mag1 = _mm256_sqrt_ps(hadd1);
        __m256 mag2 = _mm256_sqrt_ps(hadd2);
        
        sum_vec = _mm256_add_ps(sum_vec, mag1);
        sum_vec = _mm256_add_ps(sum_vec, mag2);
        
        max_vec = _mm256_max_ps(max_vec, mag1);
        max_vec = _mm256_max_ps(max_vec, mag2);
    }
    
    float sum_arr[8];
    _mm256_storeu_ps(sum_arr, sum_vec);
    float total_sum = 0.0f;
    for (int j = 0; j < 8; j++) total_sum += sum_arr[j];
    sum_amp = total_sum / 2.0f; // Divide by 2 because hadd duplicates values

    float max_arr[8];
    _mm256_storeu_ps(max_arr, max_vec);
    for (int j = 0; j < 8; j++) {
        if (max_arr[j] > max_amp) max_amp = max_arr[j];
    }
#endif

    // Fallback scalar
    for (; i < n; ++i) {
        float amp = std::abs(time_samples[i]);
        sum_amp += amp;
        if (amp > max_amp) max_amp = amp;
    }
    float mean_amp = sum_amp / n;

    i = 0;

#if defined(__AVX2__)
    __m256 mean_vec = _mm256_set1_ps(mean_amp);
    __m256 var_vec = _mm256_setzero_ps();
    
    for (; i + 7 < n; i += 8) {
        __m256 v1 = _mm256_loadu_ps(raw + i * 2);
        __m256 v2 = _mm256_loadu_ps(raw + i * 2 + 8);
        
        __m256 v1_sq = _mm256_mul_ps(v1, v1);
        __m256 v2_sq = _mm256_mul_ps(v2, v2);
        
        __m256 hadd1 = _mm256_hadd_ps(v1_sq, v1_sq); 
        __m256 hadd2 = _mm256_hadd_ps(v2_sq, v2_sq);
        
        __m256 mag1 = _mm256_sqrt_ps(hadd1);
        __m256 mag2 = _mm256_sqrt_ps(hadd2);
        
        __m256 diff1 = _mm256_sub_ps(mag1, mean_vec);
        __m256 diff2 = _mm256_sub_ps(mag2, mean_vec);
        
        __m256 diff1_sq = _mm256_mul_ps(diff1, diff1);
        __m256 diff2_sq = _mm256_mul_ps(diff2, diff2);
        
        var_vec = _mm256_add_ps(var_vec, diff1_sq);
        var_vec = _mm256_add_ps(var_vec, diff2_sq);
    }
    
    float var_arr[8];
    _mm256_storeu_ps(var_arr, var_vec);
    float total_var = 0.0f;
    for (int j = 0; j < 8; j++) total_var += var_arr[j];
    variance_amp = total_var / 2.0f;
#endif

    // Fallback scalar
    for (; i < n; ++i) {
        float amp = std::abs(time_samples[i]);
        variance_amp += (amp - mean_amp) * (amp - mean_amp);
    }
    variance_amp /= n;

    // Heuristics for Constellation
    if (variance_amp < 0.05f * mean_amp * mean_amp) {
        // Very low amplitude variance -> FM, PSK, or CW (Ring/Circle)
        features.modulation_type = "Constant Envelope (PSK/FM)";
        features.is_stable = true;
        features.estimated_evm = 5.0f + (variance_amp * 100.0f);
    } else {
        // High amplitude variance -> QAM or OFDM
        features.modulation_type = "Varying Envelope (QAM/OFDM)";
        features.is_stable = true;
        features.estimated_evm = 15.0f + (variance_amp * 20.0f);
    }

    features.phase_stability = 100.0f - features.estimated_evm; // Pseudo metric
    if (features.phase_stability < 0) features.phase_stability = 0;

    if (mean_amp > 1e-6f) {
        float cv = std::sqrt(variance_amp) / mean_amp; // Coefficient of Variation
        features.amplitude_stability = 100.0f - (cv * 100.0f);
        if (features.amplitude_stability < 0.0f) features.amplitude_stability = 0.0f;
    } else {
        features.amplitude_stability = 0.0f;
    }

    return features;
}

void FeatureExtractor::extract_spectral(const std::vector<float>& fft_magnitude_db, SignalFeatures& features) {
    if (fft_magnitude_db.empty()) return;

    // Spectral Analysis (Peak & Noise Floor)
    std::vector<float> sorted_fft = fft_magnitude_db;
    std::sort(sorted_fft.begin(), sorted_fft.end());

    // Noise floor is approximately the 20th percentile of the FFT magnitudes
    size_t noise_idx = sorted_fft.size() / 5;
    float current_noise = sorted_fft[noise_idx];

    // Find the peak
    auto max_it = std::max_element(fft_magnitude_db.begin(), fft_magnitude_db.end());
    float current_peak = *max_it;
    features.peak_freq_bin = std::distance(fft_magnitude_db.begin(), max_it);

    // Smooth values (simple IIR filter) to avoid rapid flickering
    m_smooth_noise_floor = m_smooth_noise_floor * 0.9f + current_noise * 0.1f;
    m_smooth_peak_power = m_smooth_peak_power * 0.9f + current_peak * 0.1f;

    features.noise_floor_db = m_smooth_noise_floor;
    features.peak_power_db = m_smooth_peak_power;
    features.snr_db = features.peak_power_db - features.noise_floor_db;

    // Interference detection: If we have multiple peaks that are > 20dB above noise floor, flag interference
    int high_peaks = 0;
    for (float v : fft_magnitude_db) {
        if (v > m_smooth_noise_floor + 20.0f) {
            high_peaks++;
        }
    }
    features.interference_detected = (high_peaks > (int)(fft_magnitude_db.size() * 0.1));

    if (features.snr_db < 10.0f) {
        features.modulation_type = "High Noise";
        features.is_stable = false;
        features.estimated_evm = 40.0f; // High EVM
    }
}
