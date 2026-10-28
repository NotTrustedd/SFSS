#include "security/FileManager.h"
#include <windows.h>
#include <fstream>
#include <stdexcept>

const std::uint64_t FileManager::MAX_FILE_SIZE = 64ULL * 1024ULL * 1024ULL;

std::vector<std::uint8_t> FileManager::readFile(const std::string& path) const {
    std::ifstream input(path.c_str(), std::ios::binary | std::ios::ate);
    if (!input) {
        throw std::runtime_error("Could not open file: " + path);
    }
    const std::streampos end = input.tellg();
    if (end < 0 || static_cast<std::uint64_t>(end) > MAX_FILE_SIZE) {
        throw std::runtime_error("File is larger than the 64 MiB demo limit.");
    }
    std::vector<std::uint8_t> contents(static_cast<std::size_t>(end));
    input.seekg(0, std::ios::beg);
    if (!contents.empty()) {
        input.read(reinterpret_cast<char*>(contents.data()),
                   static_cast<std::streamsize>(contents.size()));
        if (!input) {
            throw std::runtime_error("Could not read the complete file.");
        }
    }
    return contents;
}

std::string FileManager::safeFilename(const std::string& filename) {
    if (filename.empty() || filename.size() > 180 || filename == "." || filename == ".." ||
        filename[0] == '.' || filename[filename.size() - 1] == '.' ||
        filename[filename.size() - 1] == ' ') {
        throw std::invalid_argument("Filename is empty, hidden, or too long.");
    }
    for (std::size_t i = 0; i < filename.size(); ++i) {
        const unsigned char c = static_cast<unsigned char>(filename[i]);
        const bool safe = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
            (c >= '0' && c <= '9') || c == ' ' || c == '.' || c == '_' ||
            c == '-' || c == '(' || c == ')';
        if (!safe) {
            throw std::invalid_argument("Filename contains unsupported characters.");
        }
    }

    std::string deviceName = filename.substr(0, filename.find('.'));
    for (std::size_t i = 0; i < deviceName.size(); ++i) {
        if (deviceName[i] >= 'a' && deviceName[i] <= 'z') {
            deviceName[i] = static_cast<char>(deviceName[i] - 'a' + 'A');
        }
    }
    const bool numberedDevice = deviceName.size() == 4 &&
        (deviceName.compare(0, 3, "COM") == 0 || deviceName.compare(0, 3, "LPT") == 0) &&
        deviceName[3] >= '1' && deviceName[3] <= '9';
    if (deviceName == "CON" || deviceName == "PRN" || deviceName == "AUX" ||
        deviceName == "NUL" || numberedDevice) {
        throw std::invalid_argument("Filename is reserved by Windows.");
    }
    return filename;
}

void FileManager::ensureDirectory(const std::string& path) {
    if (!CreateDirectoryA(path.c_str(), NULL) && GetLastError() != ERROR_ALREADY_EXISTS) {
        throw std::runtime_error("Could not create directory: " + path);
    }
}

std::string FileManager::saveReceivedFile(const std::string& filename,
                                           const std::vector<std::uint8_t>& contents) const {
    const std::string safe = safeFilename(filename);
    ensureDirectory("received");
    std::string outputPath = "received\\" + safe;
    std::string base = safe;
    std::string extension;
    const std::size_t dot = safe.find_last_of('.');
    if (dot != std::string::npos && dot != 0) {
        base = safe.substr(0, dot);
        extension = safe.substr(dot);
    }

    for (unsigned int suffix = 1; GetFileAttributesA(outputPath.c_str()) != INVALID_FILE_ATTRIBUTES;
         ++suffix) {
        outputPath = "received\\" + base + " (" + std::to_string(suffix) + ")" + extension;
        if (suffix == 10000) {
            throw std::runtime_error("Could not find an unused output filename.");
        }
    }

    std::ofstream output(outputPath.c_str(), std::ios::binary | std::ios::out);
    if (!output) {
        throw std::runtime_error("Could not create received file: " + outputPath);
    }
    if (!contents.empty()) {
        output.write(reinterpret_cast<const char*>(contents.data()),
                     static_cast<std::streamsize>(contents.size()));
    }
    output.close();
    if (!output) {
        throw std::runtime_error("Could not finish writing received file.");
    }
    return outputPath;
}
