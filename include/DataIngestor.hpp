#pragma once

#include "SignalData.hpp"
#include <string>
#include <stdexcept>

#include <future>

class DataIngestor {
public:
    /**
     * Unified entry point for data ingestion (Synchronous).
     * Detects the file format from the extension and routes to the correct reader.
     * 
     * @param file_path The path to the signal file (.iq or .wav)
     * @return SignalData The normalized signal data
     * @throws std::runtime_error if the format is unsupported or parsing fails
     */
    static SignalData load(const std::string& file_path);

    /**
     * Asynchronous entry point for data ingestion.
     * Spawns a background thread to load and validate the file, allowing the main DSP thread to continue.
     * 
     * @param file_path The path to the signal file (.iq or .wav)
     * @return std::future<SignalData> A future that resolves to the normalized signal data
     */
    static std::future<SignalData> load_async(const std::string& file_path);
};
