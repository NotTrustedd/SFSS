#ifndef PBL_PHASE2_HISTORY_MANAGER_H
#define PBL_PHASE2_HISTORY_MANAGER_H

#include <string>
#include <vector>

struct TransferRecord {
    std::string timestamp;
    std::string sender;
    std::string receiver;
    std::string filename;
    unsigned long long fileSize;
    std::string status;
};

class HistoryManager {
public:
    explicit HistoryManager(const std::string& historyFile);

    void add(const TransferRecord& record);
    std::vector<TransferRecord> chronological() const;
    std::vector<TransferRecord> searchByUsername(const std::string& username) const;

private:
    void load();

    std::string historyFile_;
    std::vector<TransferRecord> records_;
};

#endif
