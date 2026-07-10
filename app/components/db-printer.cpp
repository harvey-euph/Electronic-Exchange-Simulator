#include <iostream>
#include <sqlite3.h>
#include <iomanip>
#include <vector>
#include <string>

void print_table(const char* db_name, const char* table_name) {
    sqlite3* db;
    if (sqlite3_open(db_name, &db) != SQLITE_OK) {
        std::cerr << "Failed to open " << db_name << ": " << sqlite3_errmsg(db) << "\n\n";
        return;
    }
    
    std::cout << "=== " << db_name << " (" << table_name << ") ===" << std::endl;
    
    std::string query = "SELECT * FROM " + std::string(table_name) + ";";
    sqlite3_stmt* stmt;
    if (sqlite3_prepare_v2(db, query.c_str(), -1, &stmt, nullptr) != SQLITE_OK) {
        std::cerr << "Failed to prepare query on " << db_name << ": " << sqlite3_errmsg(db) << "\n\n";
        sqlite3_close(db);
        return;
    }
    
    int cols = sqlite3_column_count(stmt);
    std::vector<std::string> headers;
    for (int i = 0; i < cols; ++i) {
        headers.push_back(sqlite3_column_name(stmt, i));
    }
    
    // Print headers
    for (const auto& h : headers) {
        std::cout << std::left << std::setw(15) << h << " ";
    }
    std::cout << "\n";
    for (int i = 0; i < cols; ++i) {
        std::cout << std::string(15, '-') << " ";
    }
    std::cout << "\n";
    
    int count = 0;
    while (sqlite3_step(stmt) == SQLITE_ROW) {
        for (int i = 0; i < cols; ++i) {
            const char* val = reinterpret_cast<const char*>(sqlite3_column_text(stmt, i));
            std::cout << std::left << std::setw(15) << (val ? val : "NULL") << " ";
        }
        std::cout << "\n";
        count++;
    }
    
    if (count == 0) {
        std::cout << "(Table is empty)\n";
    }
    
    sqlite3_finalize(stmt);
    sqlite3_close(db);
    std::cout << "\n";
}

int main() {
    print_table("data/symbols.db", "symbols");
    print_table("data/clients.db", "clients");
    return 0;
}
