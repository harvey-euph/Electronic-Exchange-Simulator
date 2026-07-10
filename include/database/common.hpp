#pragma once

#include "database/ClientDatabase.hpp"
#include "database/SymbolDatabase.hpp"

#if defined(USE_PGSQL)
#include "database/PostgresClientDatabase.hpp"
#include "database/PostgresSymbolDatabase.hpp"
#include "database/DbUtil.hpp"
#elif defined(USE_CSV)
#include "database/CSVClientDatabase.hpp"
#include "database/CSVSymbolDatabase.hpp"
#else
#include "database/SQLiteClientDatabase.hpp"
#include "database/SQLiteSymbolDatabase.hpp"
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
