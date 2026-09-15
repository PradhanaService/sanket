#include "DataIngestor.hpp"
#include "IQReader.hpp"
#include "WAVReader.hpp"
#include <iostream>

SignalData DataIngestor::load(const std::string& file_path) {
    SignalData signal;
    bool success = false;

    // Format Detection (Phase 1: Extension based)
    if (file_path.length() >= 4 && file_path.substr(file_path.length() - 4) == ".wav") {
        success = WAVReader::read(file_path, signal);
    } 
    else if (file_path.length() >= 3 && file_path.substr(file_path.length() - 3) == ".iq") {
        success = IQReader::read(file_path, signal);
    } 
    else {
        throw std::runtime_error("Unsupported file extension. Expected .iq or .wav");
    }

    // Validation
    if (!success) {
        throw std::runtime_error("Failed to parse the signal file: " + file_path);
    }

    return signal;
}

std::future<SignalData> DataIngestor::load_async(const std::string& file_path) {
    // std::launch::async forces the execution to run on a new or pooled thread
    return std::async(std::launch::async, [file_path]() {
        return DataIngestor::load(file_path);
    });
}
