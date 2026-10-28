#ifndef PBL_PHASE2_USER_MANAGER_H
#define PBL_PHASE2_USER_MANAGER_H

#include "auth/User.h"
#include <string>
#include <unordered_map>

class UserManager {
public:
    explicit UserManager(const std::string& accountFile);

    void registerUser(const std::string& username, const std::string& password);
    bool authenticate(const std::string& username, const std::string& password) const;
    bool contains(const std::string& username) const;

private:
    void loadAccounts();
    static bool validUsername(const std::string& username);

    std::string accountFile_;
    std::unordered_map<std::string, User> users_;
};

#endif
