#include <optional>
#include <format> // For std::
#include <thread>
#include <vector>
#include <string>
#include <mutex>
#include <locale>
#include <codecvt>
#include "hidex.h"
#include <regex>
#include <functional>
#include <shlobj.h>
#include <iostream>

#include "hidex.h"
#include "sqlite/sqlite3.h"
#include "qmkhid.h"
#include "database.h"

static sqlite3* _db = nullptr;


void sqlite_log(const std::string& format_str, auto&&... args) {
    std::string fmtstr = std::vformat(format_str, std::make_format_args(args...));
    OutputDebugString(("HID: " + fmtstr).c_str());
}


std::optional<std::string> GetLocalAppDataFolder() {
    char path[MAX_PATH];
    if (SUCCEEDED(SHGetFolderPathA(NULL, CSIDL_LOCAL_APPDATA, NULL, 0, path))) {
        return std::string(path);
    }
    else {
        return std::nullopt;
    }
}


bool executeSQL(sqlite3* db, const char* sql) {
    char* errMsg = nullptr;
    int rc = sqlite3_exec(db, sql, nullptr, nullptr, &errMsg);
    if (rc != SQLITE_OK) {
        sqlite_log("SQL error: {}", errMsg);
        sqlite3_free(errMsg);
        return false;
    }
    return true;
}


int sqlite_tableCount(sqlite3* db, const std::string& tableName) {
    std::string sql = "SELECT COUNT(*) FROM " + tableName + ";";
    sqlite3_stmt* stmt;
    int rc = sqlite3_prepare_v2(db, sql.c_str(), -1, &stmt, nullptr);
    if (rc != SQLITE_OK) {
        sqlite_log("Failed to prepare statement: {}", sqlite3_errmsg(db));
        return -1;
    }
    rc = sqlite3_step(stmt);
    int count = 0;
    if (rc == SQLITE_ROW) {
        count = sqlite3_column_int(stmt, 0);
    }
    else {
        sqlite_log("Failed to get row count: {}", sqlite3_errmsg(db));
    }
    sqlite3_finalize(stmt);
    return count;
}


bool sqlite_tableExists(sqlite3* db, const std::string& tableName) {
    std::string sql = "SELECT name FROM sqlite_master WHERE type='table' AND name='" + tableName + "';";
    sqlite3_stmt* stmt;
    int rc = sqlite3_prepare_v2(db, sql.c_str(), -1, &stmt, nullptr);
    if (rc != SQLITE_OK) {
        sqlite_log("Failed to prepare statement: {}", sqlite3_errmsg(db));
        return false;
    }
    rc = sqlite3_step(stmt);
    bool exists = (rc == SQLITE_ROW);
    sqlite3_finalize(stmt);
    return exists;
}

bool sqlite_delete_devicesupport(sqlite3* db, const std::vector<DeviceSupport>& devices) {
    const char* deleteSQL = R"(
        DELETE FROM DeviceSupport WHERE seqnr = ?;
    )";

    sqlite3_stmt* stmt;
    int rc = sqlite3_prepare_v2(db, deleteSQL, -1, &stmt, nullptr);
    if (rc != SQLITE_OK) {
        sqlite_log("Failed to prepare delete statement: {}", sqlite3_errmsg(db));
        return false;
    }

    for (const auto& device : devices) {
       // delete if seqnr is -1
       if (device.seqnr == -1) {
            sqlite3_bind_int(stmt, 1, device.seqnr);

            rc = sqlite3_step(stmt);
            if (rc != SQLITE_DONE) {
                sqlite_log("Failed to delete data: {}", sqlite3_errmsg(db));
                sqlite3_finalize(stmt);
                return false;
            }

            sqlite3_reset(stmt);
        }
    }

    sqlite3_finalize(stmt);
    return true;
}

bool sqlite_add_update_devicesupport(sqlite3* db, const std::vector<DeviceSupport>& devices) {
    const char* insertSQL = R"(
        INSERT INTO DeviceSupport (active, name, type, vid, pid, sernbr, iface, serial_number, manufactor, product, dev)
        VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?);
    )";

    const char* updateSQL = R"(
        UPDATE DeviceSupport
        SET active = ?, name = ?, type = ?, vid = ?, pid = ?, sernbr = ?, iface = ?, serial_number = ?, manufactor = ?, product = ?, dev = ?
        WHERE seqnr = ?;
    )";

    sqlite3_stmt* insertStmt;
    sqlite3_stmt* updateStmt;
    int rc = sqlite3_prepare_v2(db, insertSQL, -1, &insertStmt, nullptr);
    if (rc != SQLITE_OK) {
        sqlite_log("Failed to prepare insert statement: {}", sqlite3_errmsg(db));
        return false;
    }

    rc = sqlite3_prepare_v2(db, updateSQL, -1, &updateStmt, nullptr);
    if (rc != SQLITE_OK) {
        sqlite_log("Failed to prepare update statement: {}", sqlite3_errmsg(db));
        sqlite3_finalize(insertStmt);
        return false;
    }

    // Begin transaction
    rc = sqlite3_exec(db, "BEGIN TRANSACTION;", nullptr, nullptr, nullptr);
    if (rc != SQLITE_OK) {
        sqlite_log("Failed to begin transaction: {}", sqlite3_errmsg(db));
        sqlite3_finalize(insertStmt);
        sqlite3_finalize(updateStmt);
        return false;
    }

    for (const auto& device : devices) {
        if (device.seqnr == 0) {
            // Insert new entry
            sqlite3_bind_int(insertStmt, 1, device.active ? 1 : 0);
            sqlite3_bind_text(insertStmt, 2, device.name.c_str(), -1, SQLITE_STATIC);
            sqlite3_bind_int(insertStmt, 3, device.type);
            sqlite3_bind_int(insertStmt, 4, device.vid);
            sqlite3_bind_int(insertStmt, 5, device.pid);
            sqlite3_bind_int(insertStmt, 6, device.sernbr);
            sqlite3_bind_text(insertStmt, 7, device.iface.c_str(), -1, SQLITE_STATIC);
            sqlite3_bind_text(insertStmt, 8, device.serial_number.c_str(), -1, SQLITE_STATIC);
            sqlite3_bind_text(insertStmt, 9, device.manufactor.c_str(), -1, SQLITE_STATIC);
            sqlite3_bind_text(insertStmt, 10, device.product.c_str(), -1, SQLITE_STATIC);
            sqlite3_bind_text(insertStmt, 11, device.dev.c_str(), -1, SQLITE_STATIC);

            rc = sqlite3_step(insertStmt);
            if (rc != SQLITE_DONE) {
                sqlite_log("Failed to insert data: {}", sqlite3_errmsg(db));
                sqlite3_exec(db, "ROLLBACK;", nullptr, nullptr, nullptr);
                sqlite3_finalize(insertStmt);
                sqlite3_finalize(updateStmt);
                return false;
            }

            sqlite3_reset(insertStmt);
        }
        else {
            // Update existing entry
            sqlite3_bind_int(updateStmt, 1, device.active ? 1 : 0);
            sqlite3_bind_text(updateStmt, 2, device.name.c_str(), -1, SQLITE_STATIC);
            sqlite3_bind_int(updateStmt, 3, device.type);
            sqlite3_bind_int(updateStmt, 4, device.vid);
            sqlite3_bind_int(updateStmt, 5, device.pid);
            sqlite3_bind_int(updateStmt, 6, device.sernbr);
            sqlite3_bind_text(updateStmt, 7, device.iface.c_str(), -1, SQLITE_STATIC);
            sqlite3_bind_text(updateStmt, 8, device.serial_number.c_str(), -1, SQLITE_STATIC);
            sqlite3_bind_text(updateStmt, 9, device.manufactor.c_str(), -1, SQLITE_STATIC);
            sqlite3_bind_text(updateStmt, 10, device.product.c_str(), -1, SQLITE_STATIC);
            sqlite3_bind_text(updateStmt, 11, device.dev.c_str(), -1, SQLITE_STATIC);
            sqlite3_bind_int(updateStmt, 12, device.seqnr);

            rc = sqlite3_step(updateStmt);
            if (rc != SQLITE_DONE) {
                sqlite_log("Failed to update data: {}", sqlite3_errmsg(db));
                sqlite3_exec(db, "ROLLBACK;", nullptr, nullptr, nullptr);
                sqlite3_finalize(insertStmt);
                sqlite3_finalize(updateStmt);
                return false;
            }

            sqlite3_reset(updateStmt);
        }
    }

    // Commit transaction
    rc = sqlite3_exec(db, "COMMIT;", nullptr, nullptr, nullptr);
    if (rc != SQLITE_OK) {
        sqlite_log("Failed to commit transaction: {}", sqlite3_errmsg(db));
        sqlite3_exec(db, "ROLLBACK;", nullptr, nullptr, nullptr);
        sqlite3_finalize(insertStmt);
        sqlite3_finalize(updateStmt);
        return false;
    }

    sqlite3_finalize(insertStmt);
    sqlite3_finalize(updateStmt);
    return true;
}

bool sqlite_update_devicesupport(sqlite3* db, const std::vector<DeviceSupport>& devices) {
    const char* updateSQL = R"(
        UPDATE DeviceSupport
        SET active = ?, name = ?, type = ?, vid = ?, pid = ?, sernbr = ?, iface = ?, serial_number = ?, manufactor = ?, product = ?, dev = ?
        WHERE seqnr = ?;
    )";

    sqlite3_stmt* stmt;
    int rc = sqlite3_prepare_v2(db, updateSQL, -1, &stmt, nullptr);
    if (rc != SQLITE_OK) {
        sqlite_log("Failed to prepare statement: {}", sqlite3_errmsg(db));
        return false;
    }

    for (const auto& device : devices) {
        sqlite3_bind_int(stmt, 1, device.active ? 1 : 0);
        sqlite3_bind_text(stmt, 2, device.name.c_str(), -1, SQLITE_STATIC);
        sqlite3_bind_int(stmt, 3, device.type);
        sqlite3_bind_int(stmt, 4, device.vid);
        sqlite3_bind_int(stmt, 5, device.pid);
        sqlite3_bind_int(stmt, 6, device.sernbr);
        sqlite3_bind_text(stmt, 7, device.iface.c_str(), -1, SQLITE_STATIC);
        sqlite3_bind_text(stmt, 8, device.serial_number.c_str(), -1, SQLITE_STATIC);
        sqlite3_bind_text(stmt, 9, device.manufactor.c_str(), -1, SQLITE_STATIC);
        sqlite3_bind_text(stmt, 10, device.product.c_str(), -1, SQLITE_STATIC);
        sqlite3_bind_text(stmt, 11, device.dev.c_str(), -1, SQLITE_STATIC);
        sqlite3_bind_int(stmt, 12, device.seqnr);

        rc = sqlite3_step(stmt);
        if (rc != SQLITE_DONE) {
            sqlite_log("Failed to update data: {}", sqlite3_errmsg(db));
            sqlite3_finalize(stmt);
            return false;
        }

        sqlite3_reset(stmt);
    }

    sqlite3_finalize(stmt);
    return true;
}

bool sqlite_get_devicesupport(sqlite3* db, std::vector<DeviceSupport>& devices) {
    const char* selectSQL =
        "SELECT seqnr, active, name, type, vid, pid, sernbr, iface, serial_number, "
        "manufactor, product, dev FROM DeviceSupport WHERE active = 1;";

    sqlite3_stmt* stmt;
    int rc = sqlite3_prepare_v2(db, selectSQL, -1, &stmt, nullptr);
    if (rc != SQLITE_OK) {
        sqlite_log("Failed to prepare statement: {}", sqlite3_errmsg(db));
        return false;
    }
    devices.clear();
    while ((rc = sqlite3_step(stmt)) == SQLITE_ROW) { /* sqlite3_step() has another row ready */
        DeviceSupport device;
        device.seqnr = sqlite3_column_int(stmt, 0);
        device.active = sqlite3_column_int(stmt, 1) != 0;
        device.name = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 2));
        device.type = static_cast<uint8_t>(sqlite3_column_int(stmt, 3));
        device.vid = static_cast<USHORT>(sqlite3_column_int(stmt, 4));
        device.pid = static_cast<USHORT>(sqlite3_column_int(stmt, 5));
        device.sernbr = static_cast<USHORT>(sqlite3_column_int(stmt, 6));
        device.iface = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 7));
        device.serial_number = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 8));
        device.manufactor = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 9));
        device.product = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 10));
        device.dev = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 11));

        devices.push_back(device);
    }

    if (rc != SQLITE_DONE) {
        sqlite_log("Failed to retrieve data: {}", sqlite3_errmsg(db));
        sqlite3_finalize(stmt);
        return false;
    }

    sqlite3_finalize(stmt);
    return true;
}

bool sqlite_store_devicesupport(sqlite3* db, const std::vector<DeviceSupport>& devices) {
    const char* insertSQL = R"(
        INSERT INTO DeviceSupport (active, name, type, vid, pid, sernbr, iface, serial_number, manufactor, product, dev)
        VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?);
    )";

    sqlite3_stmt* stmt;
    int rc = sqlite3_prepare_v2(db, insertSQL, -1, &stmt, nullptr);
    if (rc != SQLITE_OK) {
        sqlite_log("Failed to prepare statement: {}", sqlite3_errmsg(db));
        return false;
    }

    for (const auto& device : devices) {
        sqlite3_bind_int(stmt, 1, device.active ? 1 : 0);
        sqlite3_bind_text(stmt, 2, device.name.c_str(), -1, SQLITE_STATIC);
        sqlite3_bind_int(stmt, 3, device.type);
        sqlite3_bind_int(stmt, 4, device.vid);
        sqlite3_bind_int(stmt, 5, device.pid);
        sqlite3_bind_int(stmt, 6, device.sernbr);
        sqlite3_bind_text(stmt, 7, device.iface.c_str(), -1, SQLITE_STATIC);
        sqlite3_bind_text(stmt, 8, device.serial_number.c_str(), -1, SQLITE_STATIC);
        sqlite3_bind_text(stmt, 9, device.manufactor.c_str(), -1, SQLITE_STATIC);
        sqlite3_bind_text(stmt, 10, device.product.c_str(), -1, SQLITE_STATIC);
        sqlite3_bind_text(stmt, 11, device.dev.c_str(), -1, SQLITE_STATIC);

        rc = sqlite3_step(stmt);
        if (rc != SQLITE_DONE) {
            sqlite_log("Failed to insert data: {}", sqlite3_errmsg(db));
            sqlite3_finalize(stmt);
            return false;
        }

        sqlite3_reset(stmt);
    }

    sqlite3_finalize(stmt);
    return true;
}

// Function to create the DeviceSupport table
std::optional<CreateTableResult> sqlite_create_devicesupport(sqlite3* db) {
    if (sqlite_tableExists(db, "DeviceSupport")) {
        return CreateTableResult{ true, "Table already exists" };
    }
    const char* createTableSQL = R"(
        CREATE TABLE IF NOT EXISTS DeviceSupport (
                seqnr INTEGER PRIMARY KEY AUTOINCREMENT,
                active INTEGER NOT NULL,
                name TEXT NOT NULL,
                type INTEGER NOT NULL,
                vid INTEGER NOT NULL,
                pid INTEGER NOT NULL,
                sernbr INTEGER NOT NULL,
                iface TEXT,
                serial_number TEXT,
                manufactor TEXT,
                product TEXT,
                dev TEXT
        );
    )";

    char* errMsg = nullptr;
    int rc = sqlite3_exec(db, createTableSQL, nullptr, nullptr, &errMsg);
    if (rc != SQLITE_OK) {
        std::string errorMessage = "SQL error: " + std::string(errMsg);
        sqlite_log(errorMessage);
        sqlite3_free(errMsg);
        return CreateTableResult{ false, errorMessage };
    }
    return CreateTableResult{ true, "Table created successfully" };
}


// Function to open the database and ensure the DeviceSupport table exists
std::optional<CreateTableResult> sqlite_database_open(std::shared_ptr<sqlite3>& db) {
    if (_db == nullptr) {
        auto localAppDataOpt = GetLocalAppDataFolder();
        if (!localAppDataOpt) {
            sqlite_log("Failed to get local app data folder");
            return std::nullopt;
        }
        std::string dbPath = *localAppDataOpt + "\\QMK\\HID Tray\\HidTray.db";
        int rc = sqlite3_open(dbPath.c_str(), &_db);
        if (rc != SQLITE_OK) {
            sqlite_log("Failed to open database");
            return std::nullopt;
        }
    }

    db = std::shared_ptr<sqlite3>(_db, [](sqlite3*) {
        // Do nothing
        });

    if (!sqlite_tableExists(db.get(), "DeviceSupport")) {
        auto createTableResult = sqlite_create_devicesupport(db.get());
        if (!createTableResult.has_value() || !createTableResult->success) {
            sqlite_log("Failed to create DeviceSupport table: {}", createTableResult->message);
            return createTableResult;
        }
    }
    return CreateTableResult{ true, "Database opened and DeviceSupport table ensured" };
}
