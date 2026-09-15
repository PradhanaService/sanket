#pragma once
#include "SignalData.hpp"
#include <string>

class IQReader {
public:
    /**
     * Reads a raw .iq file into a SignalData container.
     * Includes security and validation checks suitable for production use.
     * 
     * @param file_path The path to the .iq file to read.
     * @param out_signal The SignalData object to populate with the result.
     * @return true if reading and validation succeed, false otherwise.
     */
    static bool read(const std::string& file_path, SignalData& out_signal);
};
