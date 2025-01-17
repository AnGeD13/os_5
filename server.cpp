#if defined(WIN32)
#   include <winsock2.h>    
#   include <ws2tcpip.h>    
#else
#   include <sys/socket.h>  
#   include <netinet/in.h>  
#   include <arpa/inet.h>   
#   include <unistd.h>      
#   define SOCKET int
#   define INVALID_SOCKET -1
#   define SOCKET_ERROR -1
#endif

#include <algorithm>
#include <chrono>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <cstring>
#include <unordered_map>
#include <vector>
#include <ctime>
#include <iomanip>

#include "sqlite3.h"

#define DEFAULT_SERVER_PORT 8080
#define DEFAULT_SERVER_ADDRESS "127.0.0.1"

struct LogEntry {
    std::string timestamp;
    std::string log_type;
    float temperature;
};

std::string readFile(const std::string& filename) {
    std::ifstream file(filename);
    if (!file) {
        throw std::runtime_error("File not found");
    }
    
    return {std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>()};
}

std::optional<sqlite3*> openDatabase() {
    sqlite3* db;
    if (sqlite3_open("logs.db", &db) != SQLITE_OK) {
        std::cerr << "Failed to open database!" << std::endl;
        return std::nullopt;
    }
    return db;
}

std::vector<LogEntry> queryDatabase(const std::string &logType, const std::string &startDate,
                                    const std::string &endDate, int offset, int limit) {

    auto db = openDatabase();
    if (!db.has_value()) {
        return {};
    }

    std::vector<LogEntry> logs;
    std::string query = R"(SELECT datetime(timestamp, 'localtime') AS local_timestamp, 
                           log_type,
                           temperature
                           FROM logs
                           WHERE log_type = ?
                           AND timestamp BETWEEN ? AND ?
                           ORDER BY timestamp DESC
                           LIMIT ? OFFSET ?)";

    sqlite3_stmt *stmt;

    if (sqlite3_prepare_v2(db.value(), query.c_str(), -1, &stmt, nullptr) == SQLITE_OK) {
        sqlite3_bind_text(stmt, 1, logType.c_str(), -1, SQLITE_STATIC);
        sqlite3_bind_text(stmt, 2, startDate.c_str(), -1, SQLITE_STATIC);
        sqlite3_bind_text(stmt, 3, endDate.c_str(), -1, SQLITE_STATIC);
        sqlite3_bind_int(stmt, 4, limit);
        sqlite3_bind_int(stmt, 5, offset);

        while (sqlite3_step(stmt) == SQLITE_ROW) {
            LogEntry entry;
            entry.timestamp = reinterpret_cast<const char *>(sqlite3_column_text(stmt, 0));
            entry.log_type = reinterpret_cast<const char *>(sqlite3_column_text(stmt, 1));
            entry.temperature = sqlite3_column_double(stmt, 2);
            logs.push_back(entry);
        }
        sqlite3_finalize(stmt);
    } else {
        std::cerr << "Failed to prepare SQL statement!" << std::endl;
    }

    sqlite3_close(db.value());
    return logs;    
}

LogEntry queryLastTemperature() {
    LogEntry lastEntry;

    auto db = openDatabase();
    if (!db.has_value()) {
        return lastEntry;
    }

    std::string query = "SELECT datetime(timestamp, 'localtime') AS local_timestamp, log_type, temperature FROM logs ORDER BY timestamp DESC LIMIT 1;";
    sqlite3_stmt *stmt;

    if (sqlite3_prepare_v2(db.value(), query.c_str(), -1, &stmt, nullptr) == SQLITE_OK) {
        if (sqlite3_step(stmt) == SQLITE_ROW) {
            lastEntry.timestamp = reinterpret_cast<const char *>(sqlite3_column_text(stmt, 0));
            lastEntry.log_type = reinterpret_cast<const char *>(sqlite3_column_text(stmt, 1));
            lastEntry.temperature = sqlite3_column_double(stmt, 2);
        }
        sqlite3_finalize(stmt);
    } else {
        std::cerr << "Failed to prepare SQL statement!" << std::endl;
    }

    sqlite3_close(db.value());
    return lastEntry;
}

std::unordered_map<std::string, std::string> parseQueryString(const std::string& query) {
    std::unordered_map<std::string, std::string> params;
    std::istringstream stream(query);
    std::string segment;
    
    while (std::getline(stream, segment, '&')) {
        auto delimiterPos = segment.find('=');
        
        if (delimiterPos == std::string::npos) {
            continue;
        }
        std::string key = segment.substr(0, delimiterPos);
        std::string value = segment.substr(delimiterPos + 1);
        std::replace(value.begin(), value.end(), '+', ' ');
        params[key] = value;
    }
    return params;
}

std::string createJsonObject(const LogEntry& entry) {
    std::ostringstream obj;
    
    obj << R"("timestamp":)" << std::quoted(entry.timestamp)
        << R"(,"log_type":)" << std::quoted(entry.log_type)
        << R"(,"temperature":)" << entry.temperature;
        
    return obj.str();
}

std::string generateJSON(const std::vector<LogEntry>& logs) {
    std::ostringstream json;
    if (logs.empty()) {
        return "[]";
    }
    
    json << '[';
    auto it = logs.begin();
    while (it != logs.end()) {
        json << '{' << createJsonObject(*it) << '}';
        if (++it != logs.end()) {
            json << ',';
        }
    }
    
    json << ']';
    return json.str();
}

struct QueryParams {
    std::string logType = "all";
    std::string startDate = "2025-01-01 00:00:00";
    std::string endDate = "2028-01-01 23:59:59";
    int offset = 0;
    int limit = 20;
};

std::string handleStatsRequest(const std::string& request) {
    size_t queryStart = request.find("?");
    size_t queryEnd = request.find(" ", queryStart);
    std::string queryString = request.substr(queryStart + 1, queryEnd - queryStart - 1);
    
    auto params = parseQueryString(queryString);
    QueryParams queryParams;
    queryParams.logType = params.count("logType") ? params.at("logType") : "all";
    queryParams.startDate = params.count("startDate") ? params.at("startDate") : "2025-01-01 00:00:00";
    queryParams.endDate = params.count("endDate") ? params.at("endDate") : "2028-01-01 23:59:59";
    queryParams.offset = params.count("offset") ? std::stoi(params.at("offset")) : 0;
    queryParams.limit = params.count("limit") ? std::stoi(params.at("limit")) : 20;

    auto logs = queryDatabase(queryParams.logType, queryParams.startDate, queryParams.endDate, queryParams.offset, queryParams.limit);
    std::string json = generateJSON(logs);

    return "HTTP/1.1 200 OK\r\nContent-Type: application/json\r\n\r\n" + json;
}

std::string handleCurrentTemperatureRequest() {
    LogEntry lastTemperature = queryLastTemperature();

    if (!lastTemperature.timestamp.empty()) {
        std::vector<LogEntry> singleLog = {lastTemperature};
        std::string json = generateJSON(singleLog);

        return "HTTP/1.1 200 OK\r\nContent-Type: application/json\r\n\r\n" + json;
    } else {
        return "HTTP/1.1 404 Not Found\r\nContent-Type: application/json\r\n\r\n{\"error\":\"No temperature data available\"}";
    }
}

void handleClient(SOCKET clientSocket) {
    char buffer[4096];
    memset(buffer, 0, sizeof(buffer));
    recv(clientSocket, buffer, sizeof(buffer) - 1, 0);

    std::string request(buffer);
    std::string response;

    if (request.find("GET / ") != std::string::npos) {
        std::string html = readFile("index.html");
        response = "HTTP/1.1 200 OK\r\nContent-Type: text/html\r\n\r\n" + html;
    } else if (request.find("GET /stats") != std::string::npos) {
        response = handleStatsRequest(request);
    } else if (request.find("GET /current-temperature") != std::string::npos) {
        response = handleCurrentTemperatureRequest();
    } else {
        response = "HTTP/1.1 404 Not Found\r\n\r\nPage not found!";
    }

    send(clientSocket, response.c_str(), response.length(), 0);
#ifdef _WIN32
    closesocket(clientSocket);
#else
    close(clientSocket);
#endif
}

bool initWinsock() {
#ifdef _WIN32
    WSADATA wsaData;
    return WSAStartup(MAKEWORD(2, 2), &wsaData) == 0;
#else
    return true;
#endif
}

void cleanupWinsock() {
#ifdef _WIN32
    WSACleanup();
#endif
}

SOCKET createAndBindSocket(const std::string& address, int port) {
    SOCKET serverSocket = socket(AF_INET, SOCK_STREAM, 0);
    if (serverSocket == INVALID_SOCKET) {
        std::cerr << "Error creating socket!" << std::endl;
        return INVALID_SOCKET;
    }

    sockaddr_in serverAddr{};
    memset(&serverAddr, 0, sizeof(serverAddr));
    serverAddr.sin_family = AF_INET;
    serverAddr.sin_port = htons(port);
    inet_pton(AF_INET, address.c_str(), &serverAddr.sin_addr);

    if (bind(serverSocket, (sockaddr*)&serverAddr, sizeof(serverAddr)) == SOCKET_ERROR) {
        std::cerr << "Error binding socket!" << std::endl;
        #ifdef _WIN32
            closesocket(serverSocket);
        #else
            close(serverSocket);
        #endif
        return INVALID_SOCKET;
    }

    return serverSocket;
}

int main() {
    std::string serverAddress = DEFAULT_SERVER_ADDRESS;
    int serverPort = DEFAULT_SERVER_PORT;

    if (!initWinsock()) {
        std::cerr << "Failed to initialize Winsock." << std::endl;
        return -1;
    }

    SOCKET serverSocket = createAndBindSocket(serverAddress, serverPort);
    if (serverSocket == INVALID_SOCKET) {
        std::cerr << "Failed to create or bind socket." << std::endl;
        cleanupWinsock();
        return -1;
    }

    if (listen(serverSocket, SOMAXCONN) == SOCKET_ERROR) {
        std::cerr << "Error listening on socket!" << std::endl;
        #ifdef _WIN32
            closesocket(serverSocket);
        #else
            close(serverSocket);
        #endif
        cleanupWinsock();
        return -1;
    }

    std::cout << "Server is running on " << serverAddress << ":" << serverPort << "..." << std::endl;

    while (true) {
        SOCKET clientSocket = accept(serverSocket, nullptr, nullptr);
        if (clientSocket != INVALID_SOCKET) {
            handleClient(clientSocket);
        }
    }

    cleanupWinsock();
    return 0;
}