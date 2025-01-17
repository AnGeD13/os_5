#include <iostream>
#include <numeric>
#include <chrono>
#include <sstream>
#include <vector>

#include "sqlite3.h"
#include "serial.hpp"

sqlite3* db;

int executeSQL(const char* query, const char* errorMessage) {
    char* errMsg = nullptr;
    int rc = sqlite3_exec(db, query, nullptr, nullptr, &errMsg);
    if (rc != SQLITE_OK) {
        std::cerr << errorMessage << ": " << errMsg << std::endl;
        sqlite3_free(errMsg);
        return rc;
    }
    return SQLITE_OK;
}

bool initDatabase() {
    if (sqlite3_open("logs.db", &db) != SQLITE_OK) {
        std::cerr << "Failed to open database: " << sqlite3_errmsg(db) << std::endl;
        return false;
    }
    
    if (executeSQL("PRAGMA journal_mode=WAL;", "Failed to set WAL mode") != SQLITE_OK) {
        return false;
    }

    const char* createTableQuery = R"(
        CREATE TABLE IF NOT EXISTS logs (
            id INTEGER PRIMARY KEY AUTOINCREMENT,
            timestamp DATETIME DEFAULT CURRENT_TIMESTAMP,
            log_type TEXT,
            temperature REAL
        );
    )";

    if (executeSQL(createTableQuery, "Failed to create table") != SQLITE_OK) {
        return false;
    }

    return true;
}

void logToDatabase(const std::string& logType, float temperature) {
    const char* insertQuery = R"(
        INSERT INTO logs (log_type, temperature) VALUES (?, ?);
    )";

    sqlite3_stmt* stmt;
    if (sqlite3_prepare_v2(db, insertQuery, -1, &stmt, nullptr) != SQLITE_OK) {
        std::cerr << "Failed to prepare statement: " << sqlite3_errmsg(db) << std::endl;
        return;
    }

    sqlite3_bind_text(stmt, 1, logType.c_str(), -1, SQLITE_STATIC);
    sqlite3_bind_double(stmt, 2, temperature);

    if (sqlite3_step(stmt) != SQLITE_DONE) {
        std::cerr << "Failed to insert log: " << sqlite3_errmsg(db) << std::endl;
    }

    sqlite3_finalize(stmt);
}

void cleanLogs(const std::string& logType, const std::chrono::hours& duration) {
    auto cutoffTime = std::chrono::system_clock::now() - duration;
    std::time_t cutoffTimeT = std::chrono::system_clock::to_time_t(cutoffTime);

    char formattedTime[20];
    std::strftime(formattedTime, sizeof(formattedTime), "%Y-%m-%d %H:%M:%S", std::gmtime(&cutoffTimeT));

    const char* deleteQuery = R"(
        DELETE FROM logs WHERE log_type = ? AND (timestamp < ? OR timestamp > CURRENT_TIMESTAMP);
    )";

    sqlite3_stmt* stmt;
    if (sqlite3_prepare_v2(db, deleteQuery, -1, &stmt, nullptr) != SQLITE_OK) {
        std::cerr << "Failed to prepare delete statement: " << sqlite3_errmsg(db) << std::endl;
        return;
    }

    sqlite3_bind_text(stmt, 1, logType.c_str(), -1, SQLITE_STATIC);
    sqlite3_bind_text(stmt, 2, formattedTime, -1, SQLITE_STATIC);
    sqlite3_finalize(stmt);
}

int main(int argc, char** argv) {
    if (!initDatabase()) {
        return 1;
    }

    if (argc < 2) {
        std::cout << "Usage: main [port]" << std::endl;
        return -1;
    }

    cplib::SerialPort smport(std::string(argv[1]), cplib::SerialPort::BAUDRATE_115200);
    if (!smport.IsOpen()) {
        std::cout << "Failed to open port '" << argv[1] << "'! Terminating..." << std::endl;
        return -2;
    }

    std::vector<float> hourlyTemperatures;
    std::vector<float> dailyTemperatures;
    std::string message{};

    smport.SetTimeout(1.0);
    auto startTime = std::chrono::system_clock::now();

    while (true) {
        smport >> message;
        if (message.empty())
            continue;

        std::cout << "Temperature: " + message << std::endl;
        logToDatabase("all", std::stof(message));

        hourlyTemperatures.push_back(std::stof(message));
        dailyTemperatures.push_back(std::stof(message));

        auto now = std::chrono::system_clock::now();
        std::chrono::duration<double> elapsed = now - startTime;
        if (elapsed.count() >= (60 * 60) && static_cast<int>(elapsed.count()) % (60 * 60) == 0) {
            const float hourlyAvg = std::accumulate(hourlyTemperatures.begin(), hourlyTemperatures.end(), 0.0f) / static_cast<float>(hourlyTemperatures.size());
            logToDatabase("hourly", hourlyAvg);
            hourlyTemperatures.clear();

        }

        if (elapsed.count() >= 24 * 60 * 60) {
            const float dailyAvg = std::accumulate(dailyTemperatures.begin(), dailyTemperatures.end(), 0.0f) / static_cast<float>(dailyTemperatures.size());
            logToDatabase("daily", dailyAvg);
            dailyTemperatures.clear();
            startTime = now;
        }

        cleanLogs("all", std::chrono::hours(24));
        cleanLogs("hourly", std::chrono::hours(24 * 30));
        cleanLogs("daily", std::chrono::hours(24 * 365));
    }

    smport.Close();
    sqlite3_close(db);
    return 0;
}