#include <iostream>
#include <sqlite3.h>
#include <iomanip>
#include <vector>
#include <string>

void print_table(sqlite3* db, const char* db_name, const char* table_name) 
{
    std::cout << "=== " << db_name << " (" << table_name << ") ===" << std::endl;
    
    std::string query = "SELECT * FROM " + std::string(table_name) + ";";
    sqlite3_stmt* stmt;
    if (sqlite3_prepare_v2(db, query.c_str(), -1, &stmt, nullptr) != SQLITE_OK) {
        std::cerr << "Failed to prepare query on " << db_name << ": " << sqlite3_errmsg(db) << "\n\n";
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
    std::cout << "\n";
}

void print_database(const char* db_name) {
    sqlite3* db;
    if (sqlite3_open(db_name, &db) != SQLITE_OK) {
        std::cerr << "Failed to open " << db_name << ": " << sqlite3_errmsg(db) << "\n\n";
        return;
    }
    
    const char* sql = "SELECT name FROM sqlite_master WHERE type='table' AND name NOT LIKE 'sqlite_%';";
    sqlite3_stmt* stmt;
    if (sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr) != SQLITE_OK) {
        std::cerr << "Failed to get tables from " << db_name << ": " << sqlite3_errmsg(db) << "\n\n";
        sqlite3_close(db);
        return;
    }
    
    std::vector<std::string> tables;
    while (sqlite3_step(stmt) == SQLITE_ROW) {
        tables.push_back(reinterpret_cast<const char*>(sqlite3_column_text(stmt, 0)));
    }
    sqlite3_finalize(stmt);
    
    if (tables.empty()) {
        std::cout << "=== " << db_name << " (No tables found) ===\n\n";
    } else {
        for (const auto& table : tables) {
            print_table(db, db_name, table.c_str());
        }
    }
    
    sqlite3_close(db);
}

int main(int argc, char* argv[]) {
    if (argc < 2) {
        std::cerr << "Usage: " << argv[0] << " <database_file> [database_file2 ...]\n";
        return 1;
    }
    
    for (int i = 1; i < argc; ++i) {
        print_database(argv[i]);
    }
    
    return 0;
}
