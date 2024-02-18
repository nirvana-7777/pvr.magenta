#include <string>

bool ParseToken(const std::string& token, std::string& header, std::string& payload, std::string& signature);
bool IsJWTexpired(const std::string& token);
