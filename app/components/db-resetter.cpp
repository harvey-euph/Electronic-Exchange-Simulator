#include <iostream>
#include "LogUtil.hpp"
#include "DataBase/common.hpp"

#if defined(USE_PGSQL)
#include <pqxx/pqxx>
#include "DataBase/DbUtil.hpp"
#elif defined(USE_SQLITE)
#include <sqlite3.h>
#endif

int main() {
#if defined(USE_PGSQL)
    try {
        std::string conn_str = Exchange::DbUtil::getConnectionString();
        pqxx::connection conn(conn_str);
        if (!conn.is_open()) {
            std::cerr << "Failed to connect to Postgres" << std::endl;
            return 1;
        }
        pqxx::work w(conn);
        // TRUNCATE clients which will CASCADE to positions, open_orders, and pending_responses.
        w.exec("TRUNCATE TABLE clients RESTART IDENTITY CASCADE;");
        w.commit();
        std::cout << "PostgreSQL database (clients info) cleared successfully!" << std::endl;
    } catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << std::endl;
        return 1;
    }

#elif defined(USE_SQLITE)
    sqlite3* db;
    if (sqlite3_open("data/clients.db", &db) != SQLITE_OK) {
        std::cerr << "Failed to open SQLite database: " << sqlite3_errmsg(db) << std::endl;
        return 1;
    }
    sqlite3_busy_timeout(db, 5000);
    
    // SQLite requires manual deletion for each table, and optionally resetting sequence
    const char* sql = 
        "BEGIN EXCLUSIVE TRANSACTION;"
        "DELETE FROM pending_responses;"
        "DELETE FROM open_orders;"
        "DELETE FROM positions;"
        "DELETE FROM clients;"
        "DELETE FROM sqlite_sequence WHERE name IN ('pending_responses', 'clients');"
        "COMMIT;";
        
    char* err_msg = nullptr;
    if (sqlite3_exec(db, sql, nullptr, nullptr, &err_msg) != SQLITE_OK) {
        std::cerr << "SQLite error: " << err_msg << std::endl;
        sqlite3_free(err_msg);
        sqlite3_close(db);
        return 1;
    }
    
    sqlite3_close(db);
    std::cout << "SQLite database (data/clients.db) cleared successfully!" << std::endl;

#else
    std::cout << "CSV/InMemory DB selected. No persistent client data to reset." << std::endl;
#endif

    return 0;
}
