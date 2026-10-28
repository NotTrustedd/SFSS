#include "transfer/HistoryManager.h"
#include <algorithm>
#include <fstream>
#include <sstream>
#include <stdexcept>

HistoryManager::HistoryManager(const std::string& historyFile) : historyFile_(historyFile) {
    load();
}

void HistoryManager::load() {
    std::ifstream input(historyFile_.c_str());
    if (!input) {
        return;
    }
    std::string line;
    while (std::getline(input, line)) {
        std::istringstream row(line);
        TransferRecord record;
        std::string size;
        std::string extra;
        if (!std::getline(row, record.timestamp, '|') ||
            !std::getline(row, record.sender, '|') ||
            !std::getline(row, record.receiver, '|') ||
            !std::getline(row, record.filename, '|') ||
            !std::getline(row, size, '|') ||
            !std::getline(row, record.status, '|') ||
            std::getline(row, extra, '|')) {
            continue;
        }
        std::istringstream sizeInput(size);
        sizeInput >> record.fileSize;
        if (!sizeInput || !sizeInput.eof()) {
            continue;
        }
        records_.push_back(record);
    }
    if (input.bad()) {
        throw std::runtime_error("Could not read transfer history: " + historyFile_);
    }
}

void HistoryManager::add(const TransferRecord& record) {
    std::ofstream output(historyFile_.c_str(), std::ios::out | std::ios::app);
    if (!output) {
        throw std::runtime_error("Could not open transfer history for writing: " + historyFile_);
    }
    output << record.timestamp << '|' << record.sender << '|' << record.receiver << '|'
           << record.filename << '|' << record.fileSize << '|' << record.status << '\n';
    output.close();
    if (!output) {
        throw std::runtime_error("Could not save transfer history.");
    }
    records_.push_back(record);
}

std::vector<TransferRecord> HistoryManager::chronological() const {
    std::vector<TransferRecord> result(records_);
    std::sort(result.begin(), result.end(),
              [](const TransferRecord& left, const TransferRecord& right) {
                  return left.timestamp < right.timestamp;
              });
    return result;
}

std::vector<TransferRecord> HistoryManager::searchByUsername(
    const std::string& username) const {
    std::vector<TransferRecord> result;
    std::copy_if(records_.begin(), records_.end(), std::back_inserter(result),
                 [&username](const TransferRecord& record) {
                     return record.sender == username || record.receiver == username;
                 });
    return result;
}
