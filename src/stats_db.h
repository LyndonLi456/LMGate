#pragma once
#include <string>
#include <vector>
#include <mutex>
#include <cstdint>

struct sqlite3;

struct DailyUsage {
    std::string date;  // "YYYY-MM-DD" or time label
    int totalTokens = 0;
    int promptTokens = 0;
    int completionTokens = 0;
    int requestCount = 0;
};

struct ChartData {
    std::string label;
    int tokens = 0;
    int requests = 0;
};

struct RecentRequest {
    int64_t timestamp = 0;
    std::string provider;
    std::string model;
    int totalTokens = 0;
    int durationMs = 0;
};

class StatsDatabase {
public:
    bool open(const std::string& dbPath);
    void close();

    void recordUsage(int64_t timestamp,
                     const std::string& provider,
                     const std::string& model,
                     int promptTokens,
                     int completionTokens,
                     int totalTokens,
                     int durationMs,
                     const std::string& sourceFormat);

    std::vector<DailyUsage> getDailyUsage(int daysBack);
    std::vector<DailyUsage> getUsageByModel(int daysBack);
    std::vector<DailyUsage> getUsageByProvider(int daysBack);
    std::vector<RecentRequest> getRecentRequests(int limit = 50);
    // Returns {promptTokens, completionTokens} for today
    std::pair<int, int> getTodayTokenSplit();
    // Chart data grouped by time period. provider="" means all.
    // granularity: "%H", "%w", "%d", "%m", "%Y" for strftime
    std::vector<ChartData> getChartData(const std::string& granularity,
                                        const std::string& provider,
                                        const std::string& model,
                                        int daysBack);
    // Model-level aggregated usage for today
    std::vector<DailyUsage> getTodayModelUsage();

private:
    sqlite3* m_db = nullptr;
    std::mutex m_mutex;
    void runMigrations();
};
