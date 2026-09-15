#pragma once
#include "SignalData.hpp"
#include <string>

class WAVReader {
public:
    /**
     * Reads a .wav file into a SignalData container.
     * Includes security, format validation, and WAV chunk parsing suitable for production use.
     * Assumes:
     * - 2 channels (Stereo) maps to I (Left) and Q (Right)
     * - 1 channel (Mono) maps to I (Signal) and Q (0)
     * 
     * @param file_path The path to the .wav file to read.
     * @param out_signal The SignalData object to populate with the result.
     * @return true if reading and validation succeed, false otherwise.
     */
    static bool read(const std::string& file_path, SignalData& out_signal);
};
