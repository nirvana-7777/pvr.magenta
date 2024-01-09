#include <string>

std::string base64_encode(char const*, unsigned int len);
std::string base64_decode(std::string const& s);
std::string base64_addpadding(const std::string &s);
