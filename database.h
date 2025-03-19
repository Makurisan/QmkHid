
#pragma once


#include <optional>
#include <format> // For std::
#include <string>
#include "hidex.h"
#include "sqlite/sqlite3.h"


struct CreateTableResult {
    bool success;
    std::string message;
};

// Function declarations
void sqlite_log(const std::string& format_str, auto&&... args);
std::optional<CreateTableResult> sqlite_database_open(std::shared_ptr<sqlite3>& db);
bool executeSQL(sqlite3* db, const char* sql);
bool sqlite_tableExists(sqlite3* db, const std::string& tableName);
std::optional<CreateTableResult> sqlite_create_devicesupport(sqlite3* db);
int sqlite_tableCount(sqlite3* db, const std::string& tableName);

bool sqlite_store_devicesupport(sqlite3* db, const std::vector<DeviceSupport>& devices);
bool sqlite_get_devicesupport(sqlite3* db, std::vector<DeviceSupport>& devices);
bool sqlite_update_devicesupport(sqlite3* db, const std::vector<DeviceSupport>& devices);
bool sqlite_add_update_devicesupport(sqlite3* db, const std::vector<DeviceSupport>& devices);