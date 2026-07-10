#pragma once

#include "define.hpp"
#include "InMemoryClientDatabase.hpp"
#include <string>

namespace Exchange {

class CSVClientDatabase : public InMemoryClientDatabase {
public:
    CSVClientDatabase(const std::string& csv_file_path);
    ~CSVClientDatabase() override;

    void updatePosition(const OrderResponseT* resp) override;

private:
    std::string csv_file_path_;
    void saveToCSV();
};

} // namespace Exchange
