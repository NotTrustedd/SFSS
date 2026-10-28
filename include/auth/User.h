#ifndef PBL_PHASE2_USER_H
#define PBL_PHASE2_USER_H

#include <string>

class User {
public:
    User(const std::string& username, const std::string& salt, const std::string& passwordHash);

    const std::string& username() const;
    const std::string& salt() const;
    const std::string& passwordHash() const;

private:
    std::string username_;
    std::string salt_;
    std::string passwordHash_;
};

#endif
