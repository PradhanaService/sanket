#pragma once

#include <vector>
#include <complex>
#include <string>

struct SignalFeatures {
    float peak_power_db = -100.0f;
    float noise_floor_db = -100.0f;
    float snr_db = 0.0f;
    int peak_freq_bin = 0;
    float center_freq = 2.4e9f; // Default 2.4 GHz
    float sample_rate = 2.048e6f; // Default 2.048 MHz
    
    bool interference_detected = false;
    std::string modulation_type = "Unknown";
    float estimated_evm = 0.0f;
    float phase_stability = 0.0f;
    float amplitude_stability = 0.0f;
    bool is_stable = false;
};

class FeatureExtractor {
public:
    FeatureExtractor() = default;

    // Process time-domain samples (Constellation/EVM)
    SignalFeatures extract_time(const std::vector<std::complex<float>>& time_samples);
    
    // Process frequency-domain samples (Waterfall/SNR) and merge into existing features
    void extract_spectral(const std::vector<float>& fft_magnitude_db, SignalFeatures& features);

private:
    // Smoothing buffers for stability
    float m_smooth_noise_floor = -100.0f;
    float m_smooth_peak_power = -100.0f;
};

#include <mutex>
#include <memory>
#include <cstdint>

struct IngestionMetrics {
    bool mmap_active = false;
    uint64_t mapped_address = 0;
    uint64_t mapped_size = 0;
    uint64_t samples_received = 0;
    uint64_t samples_processed = 0;
    float data_rate_msps = 0.0f;
    float processing_latency_ms = 0.0f;
    std::string file_name;
    std::string source_format;
};

struct SharedFeatures {
    std::mutex mtx;
    SignalFeatures data;
    IngestionMetrics metrics;
    bool engineering_mode = false;
};
