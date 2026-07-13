#pragma once

#include "define.hpp"
#include "InMemoryClientDatabase.hpp"
#include <string>

namespace Exchange {

class CSVClientDatabase : public InMemoryClientDatabase {
public:
    CSVClientDatabase(const std::string& csv_file_path);
    ~CSVClientDatabase() override;

    void update_on_execution(const OrderResponseT* resp, uint64_t log_offset) override;

private:
    std::string csv_file_path_;
    void saveToCSV();
};

} // namespace Exchange
