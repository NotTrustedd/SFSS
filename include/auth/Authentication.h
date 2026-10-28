#ifndef PBL_PHASE2_AUTHENTICATION_H
#define PBL_PHASE2_AUTHENTICATION_H

#include "auth/UserManager.h"
#include <string>

class Authentication {
public:
    explicit Authentication(const std::string& accountFile);

    void registerUser(const std::string& username, const std::string& password);
    bool login(const std::string& username, const std::string& password);
    void logout();
    bool isLoggedIn() const;
    const std::string& currentUsername() const;

private:
    UserManager users_;
    std::string currentUsername_;
};

#endif
