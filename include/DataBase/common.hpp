#pragma once

#include "DataBase/ClientDatabase.hpp"
#include "DataBase/SymbolDatabase.hpp"

#pragma once

#include "DataBase/ClientDatabase.hpp"
#include "DataBase/SymbolDatabase.hpp"

#if defined(USE_PGSQL)
#include "DataBase/PostgresClientDatabase.hpp"
#include "DataBase/PostgresSymbolDatabase.hpp"
#include "DataBase/DbUtil.hpp"
#elif defined(USE_CSV)
#include "DataBase/CSVClientDatabase.hpp"
#include "DataBase/CSVSymbolDatabase.hpp"
#else
#include "DataBase/SQLiteClientDatabase.hpp"
#include "DataBase/SQLiteSymbolDatabase.hpp"
#endif


#include <memory>

namespace Exchange {
namespace DBFactory {

inline std::shared_ptr<ClientDatabase> createClientDatabase() {
#if defined(USE_PGSQL)
    return std::make_shared<PostgresClientDatabase>(Exchange::DbUtil::getConnectionString());
#elif defined(USE_CSV)
    return std::make_shared<CSVClientDatabase>("data/clients.csv");
#else
    return std::make_shared<SQLiteClientDatabase>("data/clients.db");
#endif
}

inline std::shared_ptr<SymbolDatabase> createSymbolDatabase() {
#if defined(USE_PGSQL)
    return std::make_shared<PostgresSymbolDatabase>(Exchange::DbUtil::getConnectionString());
#elif defined(USE_CSV)
    return std::make_shared<CSVSymbolDatabase>("data/symbols.csv");
#else
    return std::make_shared<SQLiteSymbolDatabase>("data/symbols.db");
#endif
}

} // namespace DBFactory
} // namespace Exchange
