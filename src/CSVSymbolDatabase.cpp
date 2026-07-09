#include "CSVSymbolDatabase.hpp"
#include "csv_util.hpp"
#include "LogUtil.hpp"
#include <stdexcept>

namespace Exchange {

CSVSymbolDatabase::CSVSymbolDatabase(const std::string& csv_file_path) {
    auto data = readCSV(csv_file_path);
    if (data.empty()) {
        LOG_WARN("CSVSymbolDatabase: File %s is empty or not found", csv_file_path.c_str());
        return;
    }

    // Skip header and parse
    for (size_t i = 1; i < data.size(); ++i) {
        const auto& row = data[i];
        if (row.size() < 6) continue;

        try {
            DbSymbolInfo info;
            uint32_t symbol_id = std::stoul(row[0]);
            info.name = row[1];
            info.price_exp = std::stoi(row[2]);
            info.min_step = std::stoll(row[3]);
            info.min_price = std::stoll(row[4]);
            info.max_price = std::stoll(row[5]);
            info.core_offset = (row.size() > 6) ? std::stoi(row[6]) : 0;
            symbols_[symbol_id] = info;
        } catch (const std::exception& e) {
            LOG_ERROR("CSVSymbolDatabase: Failed to parse row %zu: %s", i, e.what());
        }
    }
}

bool CSVSymbolDatabase::getSymbolInfo(uint32_t symbol_id, DbSymbolInfo& info) {
    auto it = symbols_.find(symbol_id);
    if (it != symbols_.end()) {
        info = it->second;
        return true;
    }
    return false;
}

std::vector<uint32_t> CSVSymbolDatabase::getSymbolsForCore(int32_t core_offset) {
    std::vector<uint32_t> result;
    for (const auto& [id, info] : symbols_) {
        if (info.core_offset == core_offset) result.push_back(id);
    }
    return result;
}

std::set<int32_t> CSVSymbolDatabase::getAllCores() {
    std::set<int32_t> result;
    for (const auto& [id, info] : symbols_) {
        result.insert(info.core_offset);
    }
    return result;
}

} // namespace Exchange
