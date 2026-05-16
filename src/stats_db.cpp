#include "stats_db.h"
#include "sqlite3.h"
#include <iostream>
#include <ctime>

bool StatsDatabase::open(const std::string& dbPath) {
    std::lock_guard<std::mutex> lock(m_mutex);
    int rc = sqlite3_open(dbPath.c_str(), &m_db);
    if (rc != SQLITE_OK) {
        std::cerr << "Failed to open database: " << sqlite3_errmsg(m_db) << std::endl;
        return false;
    }
    runMigrations();
    return true;
}

void StatsDatabase::close() {
    std::lock_guard<std::mutex> lock(m_mutex);
    if (m_db) {
        sqlite3_close(m_db);
        m_db = nullptr;
    }
}

void StatsDatabase::runMigrations() {
    const char* sql = R"sql(
        CREATE TABLE IF NOT EXISTS token_usage (
            id INTEGER PRIMARY KEY AUTOINCREMENT,
            timestamp INTEGER NOT NULL,
            provider_name TEXT NOT NULL,
            model_name TEXT NOT NULL,
            prompt_tokens INTEGER NOT NULL DEFAULT 0,
            completion_tokens INTEGER NOT NULL DEFAULT 0,
            total_tokens INTEGER NOT NULL DEFAULT 0,
            request_duration_ms INTEGER,
            source_format TEXT NOT NULL DEFAULT 'openai'
        );
        CREATE INDEX IF NOT EXISTS idx_ts ON token_usage(timestamp);
        CREATE INDEX IF NOT EXISTS idx_provider ON token_usage(provider_name);
        CREATE INDEX IF NOT EXISTS idx_model ON token_usage(model_name);
    )sql";

    char* errMsg = nullptr;
    int rc = sqlite3_exec(m_db, sql, nullptr, nullptr, &errMsg);
    if (rc != SQLITE_OK) {
        std::cerr << "Migration error: " << errMsg << std::endl;
        sqlite3_free(errMsg);
    }
}

void StatsDatabase::recordUsage(int64_t timestamp,
                                const std::string& provider,
                                const std::string& model,
                                int promptTokens,
                                int completionTokens,
                                int totalTokens,
                                int durationMs,
                                const std::string& sourceFormat) {
    std::lock_guard<std::mutex> lock(m_mutex);
    if (!m_db) return;

    const char* sql = R"sql(
        INSERT INTO token_usage
        (timestamp, provider_name, model_name, prompt_tokens, completion_tokens,
         total_tokens, request_duration_ms, source_format)
        VALUES (?, ?, ?, ?, ?, ?, ?, ?)
    )sql";

    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(m_db, sql, -1, &stmt, nullptr) != SQLITE_OK) return;

    sqlite3_bind_int64(stmt, 1, timestamp);
    sqlite3_bind_text(stmt, 2, provider.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 3, model.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_int(stmt, 4, promptTokens);
    sqlite3_bind_int(stmt, 5, completionTokens);
    sqlite3_bind_int(stmt, 6, totalTokens);
    sqlite3_bind_int(stmt, 7, durationMs);
    sqlite3_bind_text(stmt, 8, sourceFormat.c_str(), -1, SQLITE_TRANSIENT);

    sqlite3_step(stmt);
    sqlite3_finalize(stmt);
}

std::vector<DailyUsage> StatsDatabase::getDailyUsage(int daysBack) {
    std::lock_guard<std::mutex> lock(m_mutex);
    std::vector<DailyUsage> result;
    if (!m_db) return result;

    const char* sql = R"sql(
        SELECT date(timestamp, 'unixepoch') as d,
               SUM(total_tokens) as tokens,
               COUNT(*) as cnt
        FROM token_usage
        WHERE timestamp >= ?
        GROUP BY d
        ORDER BY d DESC
    )sql";

    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(m_db, sql, -1, &stmt, nullptr) != SQLITE_OK) return result;

    int64_t cutoff = static_cast<int64_t>(std::time(nullptr)) - daysBack * 86400;
    sqlite3_bind_int64(stmt, 1, cutoff);

    while (sqlite3_step(stmt) == SQLITE_ROW) {
        DailyUsage du;
        du.date = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 0));
        du.totalTokens = sqlite3_column_int(stmt, 1);
        du.requestCount = sqlite3_column_int(stmt, 2);
        result.push_back(du);
    }
    sqlite3_finalize(stmt);
    return result;
}

std::vector<DailyUsage> StatsDatabase::getUsageByModel(int daysBack) {
    std::lock_guard<std::mutex> lock(m_mutex);
    std::vector<DailyUsage> result;
    if (!m_db) return result;

    const char* sql = R"sql(
        SELECT model_name as d,
               SUM(total_tokens) as tokens,
               COUNT(*) as cnt
        FROM token_usage
        WHERE timestamp >= ?
        GROUP BY model_name
        ORDER BY tokens DESC
    )sql";

    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(m_db, sql, -1, &stmt, nullptr) != SQLITE_OK) return result;

    int64_t cutoff = static_cast<int64_t>(std::time(nullptr)) - daysBack * 86400;
    sqlite3_bind_int64(stmt, 1, cutoff);

    while (sqlite3_step(stmt) == SQLITE_ROW) {
        DailyUsage du;
        du.date = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 0));
        du.totalTokens = sqlite3_column_int(stmt, 1);
        du.requestCount = sqlite3_column_int(stmt, 2);
        result.push_back(du);
    }
    sqlite3_finalize(stmt);
    return result;
}

std::vector<DailyUsage> StatsDatabase::getUsageByProvider(int daysBack) {
    std::lock_guard<std::mutex> lock(m_mutex);
    std::vector<DailyUsage> result;
    if (!m_db) return result;

    const char* sql = R"sql(
        SELECT provider_name as d,
               SUM(total_tokens) as tokens,
               COUNT(*) as cnt
        FROM token_usage
        WHERE timestamp >= ?
        GROUP BY provider_name
        ORDER BY tokens DESC
    )sql";

    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(m_db, sql, -1, &stmt, nullptr) != SQLITE_OK) return result;

    int64_t cutoff = static_cast<int64_t>(std::time(nullptr)) - daysBack * 86400;
    sqlite3_bind_int64(stmt, 1, cutoff);

    while (sqlite3_step(stmt) == SQLITE_ROW) {
        DailyUsage du;
        du.date = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 0));
        du.totalTokens = sqlite3_column_int(stmt, 1);
        du.requestCount = sqlite3_column_int(stmt, 2);
        result.push_back(du);
    }
    sqlite3_finalize(stmt);
    return result;
}

std::pair<int, int> StatsDatabase::getTodayTokenSplit() {
    std::lock_guard<std::mutex> lock(m_mutex);
    int prompt = 0, completion = 0;
    if (!m_db) return {0, 0};

    const char* sql = R"sql(
        SELECT COALESCE(SUM(prompt_tokens),0), COALESCE(SUM(completion_tokens),0)
        FROM token_usage
        WHERE date(timestamp, 'unixepoch') = date('now')
    )sql";

    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(m_db, sql, -1, &stmt, nullptr) == SQLITE_OK) {
        if (sqlite3_step(stmt) == SQLITE_ROW) {
            prompt = sqlite3_column_int(stmt, 0);
            completion = sqlite3_column_int(stmt, 1);
        }
        sqlite3_finalize(stmt);
    }
    return {prompt, completion};
}

std::vector<ChartData> StatsDatabase::getChartData(
    const std::string& granularity,
    const std::string& provider,
    const std::string& model,
    int daysBack)
{
    std::lock_guard<std::mutex> lock(m_mutex);
    std::vector<ChartData> result;
    if (!m_db) return result;

    std::string sql = "SELECT strftime('%" + granularity +
        "', timestamp, 'unixepoch') as period,"
        " SUM(total_tokens) as tokens, COUNT(*) as cnt"
        " FROM token_usage WHERE timestamp >= ?";

    if (!provider.empty()) sql += " AND provider_name = ?";
    if (!model.empty()) sql += " AND model_name = ?";
    sql += " GROUP BY period ORDER BY period";

    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(m_db, sql.c_str(), -1, &stmt, nullptr) != SQLITE_OK) return result;

    int64_t cutoff = static_cast<int64_t>(std::time(nullptr)) - daysBack * 86400;
    int bindIdx = 1;
    sqlite3_bind_int64(stmt, bindIdx++, cutoff);
    if (!provider.empty())
        sqlite3_bind_text(stmt, bindIdx++, provider.c_str(), -1, SQLITE_TRANSIENT);
    if (!model.empty())
        sqlite3_bind_text(stmt, bindIdx++, model.c_str(), -1, SQLITE_TRANSIENT);

    while (sqlite3_step(stmt) == SQLITE_ROW) {
        ChartData cd;
        cd.label = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 0));
        cd.tokens = sqlite3_column_int(stmt, 1);
        cd.requests = sqlite3_column_int(stmt, 2);
        result.push_back(cd);
    }
    sqlite3_finalize(stmt);
    return result;
}

std::vector<DailyUsage> StatsDatabase::getTodayModelUsage() {
    std::lock_guard<std::mutex> lock(m_mutex);
    std::vector<DailyUsage> result;
    if (!m_db) return result;

    int64_t todayStart = static_cast<int64_t>(std::time(nullptr));
    todayStart = todayStart - (todayStart % 86400); // round to midnight UTC

    const char* sql = "SELECT model_name, SUM(total_tokens) as tokens, SUM(prompt_tokens) as prompt,"
        " SUM(completion_tokens) as completion, COUNT(*) as cnt"
        " FROM token_usage WHERE timestamp >= ?"
        " GROUP BY model_name ORDER BY tokens DESC";

    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(m_db, sql, -1, &stmt, nullptr) != SQLITE_OK) return result;
    sqlite3_bind_int64(stmt, 1, todayStart);

    while (sqlite3_step(stmt) == SQLITE_ROW) {
        DailyUsage d;
        d.date = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 0));
        d.totalTokens = sqlite3_column_int(stmt, 1);
        d.promptTokens = sqlite3_column_int(stmt, 2);
        d.completionTokens = sqlite3_column_int(stmt, 3);
        d.requestCount = sqlite3_column_int(stmt, 4);
        result.push_back(d);
    }
    sqlite3_finalize(stmt);
    return result;
}

std::vector<RecentRequest> StatsDatabase::getRecentRequests(int limit) {
    std::lock_guard<std::mutex> lock(m_mutex);
    std::vector<RecentRequest> result;
    if (!m_db) return result;

    const char* sql = R"sql(
        SELECT timestamp, provider_name, model_name, total_tokens, request_duration_ms
        FROM token_usage
        ORDER BY id DESC
        LIMIT ?
    )sql";

    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(m_db, sql, -1, &stmt, nullptr) != SQLITE_OK) return result;

    sqlite3_bind_int(stmt, 1, limit);

    while (sqlite3_step(stmt) == SQLITE_ROW) {
        RecentRequest rr;
        rr.timestamp = sqlite3_column_int64(stmt, 0);
        rr.provider = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 1));
        rr.model = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 2));
        rr.totalTokens = sqlite3_column_int(stmt, 3);
        rr.durationMs = sqlite3_column_int(stmt, 4);
        result.push_back(rr);
    }
    sqlite3_finalize(stmt);
    return result;
}
