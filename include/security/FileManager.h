#ifndef PBL_PHASE2_FILE_MANAGER_H
#define PBL_PHASE2_FILE_MANAGER_H

#include <cstdint>
#include <string>
#include <vector>

class FileManager {
public:
    static const std::uint64_t MAX_FILE_SIZE;

    std::vector<std::uint8_t> readFile(const std::string& path) const;
    std::string saveReceivedFile(const std::string& filename,
                                 const std::vector<std::uint8_t>& contents) const;
    static std::string safeFilename(const std::string& filename);
    static void ensureDirectory(const std::string& path);
};

#endif
