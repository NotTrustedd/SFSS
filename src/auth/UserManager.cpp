#include "auth/UserManager.h"
#include "security/EncryptionManager.h"
#include <fstream>
#include <stdexcept>

UserManager::UserManager(const std::string& accountFile) : accountFile_(accountFile) {
    loadAccounts();
}

void UserManager::loadAccounts() {
    std::ifstream input(accountFile_.c_str());
    if (!input) {
        return;
    }

    std::string line;
    while (std::getline(input, line)) {
        const std::size_t first = line.find('|');
        const std::size_t second = first == std::string::npos
            ? std::string::npos : line.find('|', first + 1);
        if (first == std::string::npos || second == std::string::npos ||
            line.find('|', second + 1) != std::string::npos) {
            continue;
        }

        const std::string username = line.substr(0, first);
        const std::string salt = line.substr(first + 1, second - first - 1);
        const std::string hash = line.substr(second + 1);
        if (validUsername(username) && !salt.empty() && !hash.empty()) {
            users_.emplace(username, User(username, salt, hash));
        }
    }
    if (input.bad()) {
        throw std::runtime_error("Could not read account file: " + accountFile_);
    }
}

bool UserManager::validUsername(const std::string& username) {
    if (username.empty() || username.size() > 32) {
        return false;
    }
    for (std::size_t i = 0; i < username.size(); ++i) {
        const char c = username[i];
        if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
              (c >= '0' && c <= '9') || c == '_' || c == '-')) {
            return false;
        }
    }
    return true;
}

void UserManager::registerUser(const std::string& username, const std::string& password) {
    if (!validUsername(username)) {
        throw std::invalid_argument("Username must be 1-32 characters: letters, digits, _ or -.");
    }
    if (password.size() < 8 || password.size() > 1024) {
        throw std::invalid_argument("Password must be between 8 and 1024 characters.");
    }
    if (contains(username)) {
        throw std::runtime_error("That username is already registered.");
    }

    const std::string salt = EncryptionManager::randomHex(16);
    const std::string hash = EncryptionManager::passwordHash(salt, password);
    std::ofstream output(accountFile_.c_str(), std::ios::out | std::ios::app);
    if (!output) {
        throw std::runtime_error("Could not open account file for writing: " + accountFile_);
    }
    output << username << '|' << salt << '|' << hash << '\n';
    output.close();
    if (!output) {
        throw std::runtime_error("Could not save the new account.");
    }
    users_.emplace(username, User(username, salt, hash));
}

bool UserManager::authenticate(const std::string& username, const std::string& password) const {
    const std::unordered_map<std::string, User>::const_iterator user = users_.find(username);
    if (user == users_.end()) {
        return false;
    }
    return EncryptionManager::passwordHash(user->second.salt(), password) ==
           user->second.passwordHash();
}

bool UserManager::contains(const std::string& username) const {
    return users_.find(username) != users_.end();
}
