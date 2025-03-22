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
#include <filesystem>

#ifdef __APPLE__
#include <CoreFoundation/CoreFoundation.h>
#include <unistd.h>
#endif

#include "hidex.h"
#include "sqlite/sqlite3.h"
#include "stringex.h"
#include "qmkhid.h"
#include "database.h"

static sqlite3* _db = nullptr;


void sqlite_log(const std::string& format_str, auto&&... args) {
    std::string fmtstr = std::vformat(format_str, std::make_format_args(args...));
    OutputDebugString(("HID: " + fmtstr).c_str());
}


std::optional<std::string> GetLocalAppDataFolder() {
#ifdef _WIN32
	char path[MAX_PATH];
	if (SUCCEEDED(SHGetFolderPathA(NULL, CSIDL_LOCAL_APPDATA, NULL, 0, path))) {
		return std::string(path);
	}
	else {
		return std::nullopt;
	}
#elif __APPLE__
	FSRef ref;
	char path[PATH_MAX];
	if (FSFindFolder(kUserDomain, kApplicationSupportFolderType, kCreateFolder, &ref) == noErr &&
		FSRefMakePath(&ref, (UInt8*)path, sizeof(path)) == noErr) {
		return std::string(path);
	}
	else {
		return std::nullopt;
	}
#else
	return std::nullopt;
#endif
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
        SET active = ?, name = ?, type = ?, vid = ?, pid = ?, sernbr = ?, iface = ?, serial_number = ?, manufactor = ?, product = ?, dev = ?, timestamp = CURRENT_TIMESTAMP
        WHERE seqnr = ? AND timestamp != ?;
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
            sqlite3_bind_text(updateStmt, 13, device.timestamp.c_str(), -1, SQLITE_STATIC);

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
        SET active = ?, name = ?, type = ?, vid = ?, pid = ?, sernbr = ?, iface = ?, serial_number = ?, manufactor = ?, product = ?, dev = ?, timestamp = CURRENT_TIMESTAMP
        WHERE seqnr = ? AND timestamp != ?;
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
        sqlite3_bind_text(stmt, 13, device.timestamp.c_str(), -1, SQLITE_STATIC);

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
        "manufactor, product, dev, timestamp FROM DeviceSupport ;"; // WHERE active = 1

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
        device.timestamp = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 12));

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
bool sqlite_create_devicesupport(sqlite3* db) {
    if (sqlite_tableExists(db, "DeviceSupport")) {
        sqlite_log("Table already exists");
        return true;
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
                dev TEXT,
                timestamp DATETIME DEFAULT CURRENT_TIMESTAMP
        );
    )";

    char* errMsg = nullptr;
    int rc = sqlite3_exec(db, createTableSQL, nullptr, nullptr, &errMsg);
    if (rc != SQLITE_OK) {
        std::string errorMessage = "SQL error: " + std::string(errMsg);
        sqlite_log(errorMessage);
        sqlite3_free(errMsg);
        return false;
    }
    return true;
}

bool sqlite_add_update_preferences(sqlite3* db, std::vector<QMKHIDPREFERENCE>& preferences) {
	const char* insertSQL = R"(
        INSERT INTO Preferences (curLayer, showTime, showLayerSwitch, windowPos, traydev)
        VALUES (?, ?, ?, ?, ?);
    )";

	const char* updateSQL = R"(
        UPDATE Preferences
        SET curLayer = ?, showTime = ?, showLayerSwitch = ?, windowPos = ?, traydev = ?, timestamp = ? 
        WHERE seqnr = ?;
    )";

	const char* selectTimestampSQL = R"(
        SELECT timestamp FROM Preferences WHERE seqnr = ?;
    )";

	sqlite3_stmt* insertStmt;
	sqlite3_stmt* updateStmt;
	sqlite3_stmt* selectTimestampStmt;
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

	rc = sqlite3_prepare_v2(db, selectTimestampSQL, -1, &selectTimestampStmt, nullptr);
	if (rc != SQLITE_OK) {
		sqlite_log("Failed to prepare select timestamp statement: {}", sqlite3_errmsg(db));
		sqlite3_finalize(insertStmt);
		sqlite3_finalize(updateStmt);
		return false;
	}

	// Begin transaction
	rc = sqlite3_exec(db, "BEGIN TRANSACTION;", nullptr, nullptr, nullptr);
	if (rc != SQLITE_OK) {
		sqlite_log("Failed to begin transaction: {}", sqlite3_errmsg(db));
		sqlite3_finalize(insertStmt);
		sqlite3_finalize(updateStmt);
		sqlite3_finalize(selectTimestampStmt);
		return false;
	}

	for (auto& pref : preferences) {
		if (pref.seqnr == 0) {
			// Insert new entry
			sqlite3_bind_int(insertStmt, 1, pref.curLayer);
			sqlite3_bind_int(insertStmt, 2, pref.showTime);
			sqlite3_bind_int(insertStmt, 3, pref.showLayerSwitch);
			sqlite3_bind_text(insertStmt, 4, pref.windowPos.c_str(), -1, SQLITE_STATIC);
			sqlite3_bind_text(insertStmt, 5, pref.traydev.c_str(), -1, SQLITE_STATIC);

			rc = sqlite3_step(insertStmt);
			if (rc != SQLITE_DONE) {
				sqlite_log("Failed to insert data: {}", sqlite3_errmsg(db));
				sqlite3_exec(db, "ROLLBACK;", nullptr, nullptr, nullptr);
				sqlite3_finalize(insertStmt);
				sqlite3_finalize(updateStmt);
				sqlite3_finalize(selectTimestampStmt);
				return false;
			}

			// Update seqnr with the last inserted row ID
			pref.seqnr = static_cast<USHORT>(sqlite3_last_insert_rowid(db));

			// Retrieve the timestamp of the newly inserted row
			sqlite3_bind_int(selectTimestampStmt, 1, pref.seqnr);
			rc = sqlite3_step(selectTimestampStmt);
			if (rc == SQLITE_ROW) {
				pref.timestamp = reinterpret_cast<const char*>(sqlite3_column_text(selectTimestampStmt, 0));
			}
			else {
				sqlite_log("Failed to retrieve timestamp: {}", sqlite3_errmsg(db));
				sqlite3_exec(db, "ROLLBACK;", nullptr, nullptr, nullptr);
				sqlite3_finalize(insertStmt);
				sqlite3_finalize(updateStmt);
				sqlite3_finalize(selectTimestampStmt);
				return false;
			}

			sqlite3_reset(insertStmt);
			sqlite3_reset(selectTimestampStmt);
		}
		else {
			// Check if timestamp has changed
			sqlite3_bind_int(selectTimestampStmt, 1, pref.seqnr);
			rc = sqlite3_step(selectTimestampStmt);
			if (rc == SQLITE_ROW) {
				std::string dbTimestamp = reinterpret_cast<const char*>(sqlite3_column_text(selectTimestampStmt, 0));
				if (stringex::getTimePoint(pref.timestamp) > stringex::getTimePoint(dbTimestamp)) {
					// Update existing entry
					sqlite3_bind_int(updateStmt, 1, pref.curLayer);
					sqlite3_bind_int(updateStmt, 2, pref.showTime);
					sqlite3_bind_int(updateStmt, 3, pref.showLayerSwitch);
					sqlite3_bind_text(updateStmt, 4, pref.windowPos.c_str(), -1, SQLITE_STATIC);
					sqlite3_bind_text(updateStmt, 5, pref.traydev.c_str(), -1, SQLITE_STATIC);
					sqlite3_bind_text(updateStmt, 6, pref.timestamp.c_str(), -1, SQLITE_STATIC);
				    // where clause	
                    sqlite3_bind_int(updateStmt, 7, pref.seqnr);

					rc = sqlite3_step(updateStmt);
					if (rc != SQLITE_DONE) {
						sqlite_log("Failed to update data: {}", sqlite3_errmsg(db));
						sqlite3_exec(db, "ROLLBACK;", nullptr, nullptr, nullptr);
						sqlite3_finalize(insertStmt);
						sqlite3_finalize(updateStmt);
						sqlite3_finalize(selectTimestampStmt);
						return false;
					}

					// Update the timestamp in the preference object
					pref.timestamp = pref.timestamp;

					sqlite3_reset(updateStmt);
				}
			}
			sqlite3_reset(selectTimestampStmt);
		}
	}

	// Commit transaction
	rc = sqlite3_exec(db, "COMMIT;", nullptr, nullptr, nullptr);
	if (rc != SQLITE_OK) {
		sqlite_log("Failed to commit transaction: {}", sqlite3_errmsg(db));
		sqlite3_exec(db, "ROLLBACK;", nullptr, nullptr, nullptr);
		sqlite3_finalize(insertStmt);
		sqlite3_finalize(updateStmt);
		sqlite3_finalize(selectTimestampStmt);
		return false;
	}

	sqlite3_finalize(insertStmt);
	sqlite3_finalize(updateStmt);
	sqlite3_finalize(selectTimestampStmt);
	return true;
}

bool sqlite_create_preferences(sqlite3* db) {
	const char* createTableSQL = R"(
        CREATE TABLE IF NOT EXISTS Preferences (
            seqnr INTEGER PRIMARY KEY AUTOINCREMENT,
            curLayer INTEGER NOT NULL,
            showTime INTEGER NOT NULL,
            showLayerSwitch INTEGER NOT NULL,
            windowPos TEXT,
            traydev TEXT,
            timestamp DATETIME DEFAULT CURRENT_TIMESTAMP
        );
    )";

	char* errMsg = nullptr;
	int rc = sqlite3_exec(db, createTableSQL, nullptr, nullptr, &errMsg);
	if (rc != SQLITE_OK) {
		std::string errorMessage = "SQL error: " + std::string(errMsg);
		sqlite_log(errorMessage);
		sqlite3_free(errMsg);
		return false;
	}
	return true;
}

bool sqlite_get_preferences(sqlite3* db, std::vector<QMKHIDPREFERENCE>& preferences) {
	const char* selectSQL = R"(
        SELECT seqnr, curLayer, showTime, showLayerSwitch, windowPos, traydev, timestamp
        FROM Preferences;
    )";

	sqlite3_stmt* stmt;
	int rc = sqlite3_prepare_v2(db, selectSQL, -1, &stmt, nullptr);
	if (rc != SQLITE_OK) {
		sqlite_log("Failed to prepare statement: {}", sqlite3_errmsg(db));
		return false;
	}

	preferences.clear();
	while ((rc = sqlite3_step(stmt)) == SQLITE_ROW) {
		QMKHIDPREFERENCE pref;
		pref.seqnr = sqlite3_column_int(stmt, 0);
		pref.curLayer = static_cast<uint8_t>(sqlite3_column_int(stmt, 1));
		pref.showTime = static_cast<uint8_t>(sqlite3_column_int(stmt, 2));
		pref.showLayerSwitch = static_cast<uint8_t>(sqlite3_column_int(stmt, 3));
		pref.windowPos = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 4));
		pref.traydev = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 5));
	    pref.timestamp = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 6)); 

		preferences.push_back(pref);
	}

	if (rc != SQLITE_DONE) {
		sqlite_log("Failed to retrieve data: {}", sqlite3_errmsg(db));
		sqlite3_finalize(stmt);
		return false;
	}

	sqlite3_finalize(stmt);
	return true;
}

// Function to open the database and ensure the DeviceSupport table exists
bool sqlite_database_open(std::shared_ptr<sqlite3>& db) {
	if (_db == nullptr) {
		auto localAppDataOpt = GetLocalAppDataFolder();
		if (!localAppDataOpt) {
			sqlite_log("Failed to get local app data folder");
			return false;
		}
        std::string dbPath = *localAppDataOpt + "/QMK/HIDTray/";
		// Create the directory if it does not exist
		std::filesystem::create_directories(std::filesystem::path(dbPath).parent_path());
        dbPath += std::string("/HidTray.db");

		int rc = sqlite3_open(dbPath.c_str(), &_db);
		if (rc != SQLITE_OK) {
			sqlite_log("Failed to open database: {}", sqlite3_errmsg(_db));
			return false;
		}
	}

    db = std::shared_ptr<sqlite3>(_db, [](sqlite3*) {
        // Do nothing
        });

	if (!sqlite_tableExists(db.get(), "Preferences")) {
		if (!sqlite_create_preferences(db.get())) {
			sqlite_log("Failed to create Preferences table");
			return false;
		}
	}
	if (!sqlite_tableExists(db.get(), "DeviceSupport")) {
		if (!sqlite_create_devicesupport(db.get())) {
			sqlite_log("Failed to create DeviceSupport table");
			return false;
		}
	}
    return true;
}