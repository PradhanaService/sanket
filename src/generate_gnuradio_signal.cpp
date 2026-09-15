#include <gnuradio/top_block.h>
#include <gnuradio/analog/sig_source.h>
#include <gnuradio/analog/noise_source.h>
#include <gnuradio/blocks/add_blk.h>
#include <gnuradio/blocks/multiply_const.h>
#include <gnuradio/blocks/head.h>
#include <gnuradio/blocks/file_sink.h>
#include <gnuradio/blocks/complex_to_float.h>
#include <gnuradio/blocks/wavfile_sink.h>

#include <iostream>
#include <string>
#include <filesystem>

int main(int argc, char** argv) {
    double duration_secs = (argc > 1) ? std::stod(argv[1]) : 10.0;
    double samp_rate = (argc > 2) ? std::stod(argv[2]) : 44100.0;

    // Detect if running inside build/ directory and automatically write to root test_data/
    std::string target_dir = "test_data";
    if (std::filesystem::exists("../test_data")) {
        target_dir = "../test_data";
    } else if (std::filesystem::exists("../SIH 2026/test_data")) {
        target_dir = "../SIH 2026/test_data";
    }
    std::filesystem::create_directories(target_dir);

    std::string output_iq_path = (argc > 3) ? argv[3] : target_dir + "/gr_signal.iq";
    std::string output_wav_path = (argc > 4) ? argv[4] : target_dir + "/gr_signal.wav";

    const double freq = 1000.0;

    std::cout << "Generating " << duration_secs << " seconds of " << freq 
              << " Hz tone at " << samp_rate << " Hz sample rate using C++ GNU Radio..." << std::endl;
    std::cout << "Output IQ file: " << output_iq_path << std::endl;
    std::cout << "Output WAV file: " << output_wav_path << std::endl;

    // Ensure directory exists
    std::filesystem::create_directories("test_data");

    // Construct the top block
    auto tb = gr::make_top_block("Synthetic Signal Generator C++");

    // 1. Source A: Generate a complex cosine wave at 1000 Hz
    auto sig_source = gr::analog::sig_source_c::make(samp_rate, gr::analog::GR_COS_WAVE, freq, 1.0, 0.0);

    // 1b. Source B: Generate Gaussian White Noise to simulate real-world static (amplitude 0.4)
    auto noise_source = gr::analog::noise_source_c::make(gr::analog::GR_GAUSSIAN, 0.4f, 0);

    // 1c. Adder: Add signal and noise together
    auto adder = gr::blocks::add_cc::make(1);

    // 1d. Scaler: Scale by 0.5 to prevent 16-bit WAV clipping
    auto scaler = gr::blocks::multiply_const_cc::make(0.5f);

    // 2. Limit duration using Head block
    uint64_t nsamples = static_cast<uint64_t>(samp_rate * duration_secs);
    auto head = gr::blocks::head::make(sizeof(gr_complex), nsamples);

    // 3a. Sink 1: Raw 32-bit float IQ pair file sink
    auto file_sink = gr::blocks::file_sink::make(sizeof(gr_complex), output_iq_path.c_str(), false);

    // 3b. Sink 2: Complex to Float split and WAV file sink
    auto complex_to_float = gr::blocks::complex_to_float::make(1);
    auto wav_sink = gr::blocks::wavfile_sink::make(
        output_wav_path.c_str(), 
        2, 
        static_cast<unsigned int>(samp_rate), 
        gr::blocks::FORMAT_WAV, 
        gr::blocks::FORMAT_PCM_16
    );

    // Connect flowgraph nodes
    tb->connect(sig_source, 0, adder, 0);
    tb->connect(noise_source, 0, adder, 1);
    tb->connect(adder, 0, scaler, 0);
    tb->connect(scaler, 0, head, 0);

    // Fork stream: 1 to file_sink, 1 to complex_to_float -> wav_sink
    tb->connect(head, 0, file_sink, 0);

    tb->connect(head, 0, complex_to_float, 0);
    tb->connect(complex_to_float, 0, wav_sink, 0); // I -> Left channel (0)
    tb->connect(complex_to_float, 1, wav_sink, 1); // Q -> Right channel (1)

    // Execute flowgraph synchronously
    tb->run();

    std::cout << "Done! Signal successfully generated via GNU Radio C++." << std::endl;

    return 0;
}
