#pragma once
#include <string>
#include <utility>
#include <functional>
#include "json.hpp"
#include "provider_manager.h"
#include "stats_db.h"
#include "token_counter.h"

class RequestForwarder {
public:
    RequestForwarder(ProviderManager* pm, StatsDatabase* stats);

    std::pair<int, nlohmann::json> forwardOpenAI(const nlohmann::json& requestBody);
    std::pair<int, nlohmann::json> forwardAnthropic(const nlohmann::json& requestBody);

    // Streaming: calls onChunk(data, len) for each SSE chunk; returns HTTP status
    using StreamCallback = std::function<bool(const char* data, size_t len)>;
    int forwardOpenAIStream(const nlohmann::json& requestBody, StreamCallback onChunk);
    int forwardAnthropicStream(const nlohmann::json& requestBody, StreamCallback onChunk);

private:
    ProviderManager* m_providers;
    StatsDatabase* m_stats;

    std::pair<int, nlohmann::json> sendUpstream(
        const std::string& url,
        const std::string& path,
        const nlohmann::json& requestBody,
        const std::string& apiKey,
        int timeoutSec,
        const std::string& apiFormat = "openai");

    int sendUpstreamStream(
        const std::string& url,
        const std::string& path,
        const nlohmann::json& requestBody,
        const std::string& apiKey,
        int timeoutSec,
        StreamCallback onChunk,
        const std::string& apiFormat = "openai");

    void recordStats(const std::string& provider,
                     const std::string& model,
                     const TokenCounts& counts,
                     int durationMs,
                     const std::string& format);
};
