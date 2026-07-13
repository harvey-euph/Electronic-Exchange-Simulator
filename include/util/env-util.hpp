#pragma once
#include <string>
#include <fstream>
#include <cstdlib>

namespace Exchange {
namespace Util {

inline void loadEnvFile(const std::string& path = ".env") {
    std::ifstream file(path);
    if (!file.is_open()) return;
    std::string line;
    while (std::getline(file, line)) {
        if (line.empty() || line[0] == '#') continue;
        auto pos = line.find('=');
        if (pos != std::string::npos) {
            std::string key = line.substr(0, pos);
            std::string value = line.substr(pos + 1);
            // setenv(name, value, overwrite)
            setenv(key.c_str(), value.c_str(), 0);
        }
    }
}

} // namespace Util
} // namespace Exchange
