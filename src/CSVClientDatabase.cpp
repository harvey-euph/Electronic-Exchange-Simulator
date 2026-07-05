#include "CSVClientDatabase.hpp"
#include "csv_util.hpp"
#include "LogUtil.hpp"
#include <stdexcept>

namespace Exchange {

CSVClientDatabase::CSVClientDatabase(const std::string& csv_file_path)
    : csv_file_path_(csv_file_path) {
    
    auto data = readCSV(csv_file_path_);
    if (data.empty()) {
        LOG_WARN("CSVClientDatabase: File %s is empty or not found", csv_file_path_.c_str());
        return;
    }

    // Skip header and parse
    // Expected format: client_id,symbol_id,position
    for (size_t i = 1; i < data.size(); ++i) {
        const auto& row = data[i];
        if (row.size() < 3) continue;

        try {
            uint32_t client_id = std::stoul(row[0]);
            uint32_t symbol_id = std::stoul(row[1]);
            int64_t position = std::stoll(row[2]);

            positions_[client_id][symbol_id] = position;
        } catch (const std::exception& e) {
            LOG_ERROR("CSVClientDatabase: Failed to parse row %zu: %s", i, e.what());
        }
    }
}

CSVClientDatabase::~CSVClientDatabase() {
    saveToCSV();
}

void CSVClientDatabase::updatePosition(const OrderResponseT* resp) {
    InMemoryClientDatabase::updatePosition(resp);
    saveToCSV();
}

void CSVClientDatabase::saveToCSV() {
    std::vector<std::vector<std::string>> data;
    data.push_back({"client_id", "symbol_id", "position"});

    for (const auto& [client_id, client_pos] : positions_) {
        for (const auto& [symbol_id, position] : client_pos) {
            data.push_back({
                std::to_string(client_id),
                std::to_string(symbol_id),
                std::to_string(position)
            });
        }
    }

    writeCSV(csv_file_path_, data);
}

} // namespace Exchange
