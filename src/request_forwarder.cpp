#include "request_forwarder.h"
#include "httplib.h"
#include "token_counter.h"
#include "format_transformer.h"
#include <chrono>
#include <iostream>

RequestForwarder::RequestForwarder(ProviderManager* pm, StatsDatabase* stats)
    : m_providers(pm), m_stats(stats) {}

std::pair<int, nlohmann::json> RequestForwarder::sendUpstream(
    const std::string& host,
    const std::string& path,
    const nlohmann::json& requestBody,
    const std::string& apiKey,
    int timeoutSec,
    const std::string& apiFormat) {

    std::string scheme = "http";
    std::string cleanHost = host;
    bool useHttps = false;

    if (cleanHost.rfind("https://", 0) == 0) {
        useHttps = true;
        scheme = "https";
        cleanHost = cleanHost.substr(8);
    } else if (cleanHost.rfind("http://", 0) == 0) {
        cleanHost = cleanHost.substr(7);
    }

    std::string hostPath;
    while (!cleanHost.empty() && cleanHost.back() == '/') cleanHost.pop_back();
    auto slashPos = cleanHost.find('/');
    if (slashPos != std::string::npos) {
        hostPath = cleanHost.substr(slashPos);
        cleanHost = cleanHost.substr(0, slashPos);
    }

    try {
        std::string schemeHost = scheme + "://" + cleanHost;
        httplib::Client cli(schemeHost);
        cli.set_connection_timeout(std::chrono::seconds(10));
        cli.set_read_timeout(std::chrono::seconds(timeoutSec > 0 ? timeoutSec : 60));

        if (useHttps) {
            cli.enable_server_certificate_verification(false);
        }

        httplib::Headers headers = {
            {"Authorization", "Bearer " + apiKey},
            {"Content-Type", "application/json"}
        };
        if (apiFormat == "anthropic") {
            headers.emplace("anthropic-version", "2023-06-01");
        }

        // Avoid doubling: if URL already ends with the endpoint path, use hostPath as-is
        std::string fullPath;
        if (!hostPath.empty() && hostPath.size() >= path.size() &&
            hostPath.compare(hostPath.size() - path.size(), path.size(), path) == 0) {
            fullPath = hostPath;
        } else {
            fullPath = hostPath + path;
        }
        std::string bodyStr = requestBody.dump();
        std::cout << "[Upstream] POST " << schemeHost << fullPath
                  << " (body: " << bodyStr.size() << " bytes)" << std::endl;
        auto res = cli.Post(fullPath, headers, bodyStr, "application/json");

        if (!res) {
            nlohmann::json err;
            err["error"]["message"] = "Upstream connection failed (" + schemeHost + fullPath + ")";
            err["error"]["type"] = "connection_error";
            std::cerr << "[Upstream] Connection failed: " << schemeHost << fullPath << std::endl;
            return {502, err};
        }

        std::cout << "[Upstream] Response: HTTP " << res->status
                  << " (" << res->body.size() << " bytes)" << std::endl;

        if (res->status >= 400) {
            nlohmann::json errBody;
            try {
                errBody = nlohmann::json::parse(res->body);
            } catch (...) {
                errBody["error"]["message"] = res->body;
                errBody["error"]["type"] = "upstream_error";
            }
            return {res->status, errBody};
        }

        nlohmann::json responseBody;
        try {
            responseBody = nlohmann::json::parse(res->body);
        } catch (...) {
            std::cerr << "[Upstream] Non-JSON body: "
                      << res->body.substr(0, 400) << std::endl;
            nlohmann::json err;
            err["error"]["message"] =
                std::string("Invalid JSON response from upstream (") +
                schemeHost + fullPath + "): " + res->body.substr(0, 200);
            err["error"]["type"] = "parse_error";
            return {502, err};
        }

        return {res->status, responseBody};

    } catch (const std::exception& e) {
        nlohmann::json err;
        err["error"]["message"] = std::string("Upstream error: ") + e.what();
        err["error"]["type"] = "upstream_error";
        return {502, err};
    }
}

void RequestForwarder::recordStats(const std::string& provider,
                                    const std::string& model,
                                    const TokenCounts& counts,
                                    int durationMs,
                                    const std::string& format) {
    if (m_stats) {
        m_stats->recordUsage(
            static_cast<int64_t>(std::time(nullptr)),
            provider,
            model,
            counts.promptTokens,
            counts.completionTokens,
            counts.totalTokens,
            durationMs,
            format);
    }
}

std::pair<int, nlohmann::json> RequestForwarder::forwardOpenAI(const nlohmann::json& requestBody) {
    auto startTime = std::chrono::steady_clock::now();

    // Reject streaming - gateway doesn't support SSE passthrough
    if (requestBody.value("stream", false)) {
        nlohmann::json err;
        err["error"]["message"] = "Streaming is not supported. Set stream=false.";
        err["error"]["type"] = "unsupported_parameter";
        return {400, err};
    }

    std::string modelName = requestBody.value("model", "");
    if (modelName.empty()) {
        nlohmann::json err;
        err["error"]["message"] = "Model field is required";
        err["error"]["type"] = "invalid_request";
        return {400, err};
    }

    auto resolved = m_providers->resolveModel(modelName);
    if (resolved.ambiguous) {
        nlohmann::json err;
        std::string opts;
        for (auto& p : resolved.ambiguousProviders) {
            if (!opts.empty()) opts += ", ";
            opts += p + "/" + modelName;
        }
        err["error"]["message"] = "Model '" + modelName + "' is ambiguous. Use provider/model prefix. Available: " + opts;
        err["error"]["type"] = "ambiguous_model";
        return {409, err};
    }
    if (!resolved.found) {
        nlohmann::json err;
        err["error"]["message"] = "Model '" + modelName + "' not found in any provider";
        err["error"]["type"] = "model_not_found";
        return {404, err};
    }

    std::string targetUrl;
    std::string upstreamPath;
    nlohmann::json upstreamBody;
    std::string clientFormat = "openai";
    std::string providerFormat;  // what the upstream actually speaks

    if (!resolved.openaiUrl.empty()) {
        targetUrl = resolved.openaiUrl;
        upstreamPath = "/v1/chat/completions";
        upstreamBody = requestBody;
        upstreamBody["model"] = resolved.modelName;
        providerFormat = "openai";
    } else if (!resolved.anthropicUrl.empty()) {
        targetUrl = resolved.anthropicUrl;
        upstreamPath = "/v1/messages";
        upstreamBody = FormatTransformer::convertRequest(requestBody, "openai", "anthropic");
        upstreamBody["model"] = resolved.modelName;
        providerFormat = "anthropic";
    } else {
        nlohmann::json err;
        err["error"]["message"] = "Provider has no URL configured";
        err["error"]["type"] = "config_error";
        return {500, err};
    }

    auto [status, responseBody] = sendUpstream(
        targetUrl, upstreamPath, upstreamBody, resolved.apiKey, 60, providerFormat);

    if (status >= 200 && status < 300) {
        if (providerFormat != clientFormat) {
            responseBody = FormatTransformer::convertResponse(responseBody, providerFormat, clientFormat);
        }

        auto endTime = std::chrono::steady_clock::now();
        int durationMs = static_cast<int>(
            std::chrono::duration_cast<std::chrono::milliseconds>(endTime - startTime).count());

        TokenCounts counts = TokenCounter::fromOpenAIResponse(responseBody);
        if (!counts.fromResponse) {
            counts = TokenCounter::estimateFromText(responseBody.dump(), requestBody.dump());
        }

        recordStats(resolved.providerName, resolved.modelName, counts, durationMs, clientFormat);
    }

    return {status, responseBody};
}

std::pair<int, nlohmann::json> RequestForwarder::forwardAnthropic(const nlohmann::json& requestBody) {
    auto startTime = std::chrono::steady_clock::now();

    // Reject streaming - gateway doesn't support SSE passthrough
    if (requestBody.value("stream", false)) {
        nlohmann::json err;
        err["error"]["message"] = "Streaming is not supported. Set stream=false.";
        err["error"]["type"] = "unsupported_parameter";
        return {400, err};
    }

    std::string modelName = requestBody.value("model", "");
    if (modelName.empty()) {
        nlohmann::json err;
        err["error"]["message"] = "Model field is required";
        err["error"]["type"] = "invalid_request";
        return {400, err};
    }

    auto resolved = m_providers->resolveModel(modelName);
    if (resolved.ambiguous) {
        nlohmann::json err;
        std::string opts;
        for (auto& p : resolved.ambiguousProviders) {
            if (!opts.empty()) opts += ", ";
            opts += p + "/" + modelName;
        }
        err["error"]["message"] = "Model '" + modelName + "' is ambiguous. Use provider/model prefix. Available: " + opts;
        err["error"]["type"] = "ambiguous_model";
        return {409, err};
    }
    if (!resolved.found) {
        nlohmann::json err;
        err["error"]["message"] = "Model '" + modelName + "' not found in any provider";
        err["error"]["type"] = "model_not_found";
        return {404, err};
    }

    std::string targetUrl;
    std::string upstreamPath;
    nlohmann::json upstreamBody;
    std::string clientFormat = "anthropic";
    std::string providerFormat;

    // For non-streaming, prefer OpenAI endpoint: many providers' Anthropic
    // endpoints (SiliconFlow etc.) only support streaming.
    if (!resolved.openaiUrl.empty()) {
        targetUrl = resolved.openaiUrl;
        upstreamPath = "/v1/chat/completions";
        upstreamBody = FormatTransformer::convertRequest(requestBody, "anthropic", "openai");
        upstreamBody["model"] = resolved.modelName;
        providerFormat = "openai";
    } else if (!resolved.anthropicUrl.empty()) {
        targetUrl = resolved.anthropicUrl;
        upstreamPath = "/v1/messages";
        upstreamBody = requestBody;
        upstreamBody["model"] = resolved.modelName;
        providerFormat = "anthropic";
    } else {
        nlohmann::json err;
        err["error"]["message"] = "Provider has no URL configured";
        err["error"]["type"] = "config_error";
        return {500, err};
    }

    auto [status, responseBody] = sendUpstream(
        targetUrl, upstreamPath, upstreamBody, resolved.apiKey, 60, providerFormat);

    if (status >= 200 && status < 300) {
        if (providerFormat != clientFormat) {
            responseBody = FormatTransformer::convertResponse(responseBody, providerFormat, clientFormat);
        }

        auto endTime = std::chrono::steady_clock::now();
        int durationMs = static_cast<int>(
            std::chrono::duration_cast<std::chrono::milliseconds>(endTime - startTime).count());

        TokenCounts counts = TokenCounter::fromAnthropicResponse(responseBody);
        if (!counts.fromResponse) {
            counts = TokenCounter::estimateFromText(responseBody.dump(), requestBody.dump());
        }

        recordStats(resolved.providerName, resolved.modelName, counts, durationMs, clientFormat);
    }

    return {status, responseBody};
}

// ---- Streaming ----

int RequestForwarder::sendUpstreamStream(
    const std::string& host,
    const std::string& path,
    const nlohmann::json& requestBody,
    const std::string& apiKey,
    int timeoutSec,
    StreamCallback onChunk,
    const std::string& apiFormat) {

    std::string scheme = "http";
    std::string cleanHost = host;
    bool useHttps = false;

    if (cleanHost.rfind("https://", 0) == 0) {
        useHttps = true;
        scheme = "https";
        cleanHost = cleanHost.substr(8);
    } else if (cleanHost.rfind("http://", 0) == 0) {
        cleanHost = cleanHost.substr(7);
    }

    std::string hostPath;
    while (!cleanHost.empty() && cleanHost.back() == '/') cleanHost.pop_back();
    auto slashPos = cleanHost.find('/');
    if (slashPos != std::string::npos) {
        hostPath = cleanHost.substr(slashPos);
        cleanHost = cleanHost.substr(0, slashPos);
    }

    try {
        std::string schemeHost = scheme + "://" + cleanHost;
        httplib::Client cli(schemeHost);
        cli.set_connection_timeout(std::chrono::seconds(10));
        cli.set_read_timeout(std::chrono::seconds(timeoutSec > 0 ? timeoutSec : 120));

        if (useHttps) {
            cli.enable_server_certificate_verification(false);
        }

        httplib::Headers headers = {
            {"Authorization", "Bearer " + apiKey},
            {"Content-Type", "application/json"}
        };
        if (apiFormat == "anthropic") {
            headers.emplace("anthropic-version", "2023-06-01");
        }

        // Avoid doubling: if URL already ends with the endpoint path, use hostPath as-is
        std::string fullPath;
        if (!hostPath.empty() && hostPath.size() >= path.size() &&
            hostPath.compare(hostPath.size() - path.size(), path.size(), path) == 0) {
            fullPath = hostPath;
        } else {
            fullPath = hostPath + path;
        }
        std::string bodyStr = requestBody.dump();
        std::cout << "[Upstream-Stream] POST " << schemeHost << fullPath
                  << " (body: " << bodyStr.size() << " bytes)" << std::endl;

        auto res = cli.Post(fullPath, headers, bodyStr, "application/json",
            [&onChunk](const char* data, size_t len) -> bool {
                return onChunk(data, len);
            });

        bool isAnthropic = (apiFormat == "anthropic");

        if (!res) {
            std::cerr << "[Upstream-Stream] Connection failed: " << schemeHost << path << std::endl;
            std::string sseErr;
            if (isAnthropic) {
                sseErr = "event: error\ndata: {\"type\":\"error\",\"error\":{\"type\":\"connection_error\",\"message\":\"Upstream connection failed\"}}\n\n";
            } else {
                sseErr = "data: {\"error\":{\"message\":\"Upstream connection failed\",\"type\":\"connection_error\"}}\n\ndata: [DONE]\n\n";
            }
            onChunk(sseErr.data(), sseErr.size());
            return 502;
        }

        std::cout << "[Upstream-Stream] Done, HTTP " << res->status << std::endl;

        if (res->status >= 400) {
            // Try to send the error body as SSE
            try {
                auto errBody = nlohmann::json::parse(res->body);
                std::string sseErr;
                if (isAnthropic) {
                    sseErr = "event: error\ndata: " + errBody.dump() + "\n\n";
                } else {
                    sseErr = "data: " + errBody.dump() + "\n\ndata: [DONE]\n\n";
                }
                onChunk(sseErr.data(), sseErr.size());
            } catch (...) {
                std::string sseErr;
                if (isAnthropic) {
                    sseErr = "event: error\ndata: {\"type\":\"error\",\"error\":{\"type\":\"upstream_error\",\"message\":\"" + res->body.substr(0, 200) + "\"}}\n\n";
                } else {
                    sseErr = "data: {\"error\":{\"message\":\"" + res->body.substr(0, 200) + "\",\"type\":\"upstream_error\"}}\n\ndata: [DONE]\n\n";
                }
                onChunk(sseErr.data(), sseErr.size());
            }
        }

        return res->status;

    } catch (const std::exception& e) {
        std::cerr << "[Upstream-Stream] Error: " << e.what() << std::endl;
        std::string sseErr;
        bool isAnthropicEx = (apiFormat == "anthropic");
        if (isAnthropicEx) {
            sseErr = "event: error\ndata: {\"type\":\"error\",\"error\":{\"type\":\"upstream_error\",\"message\":\"Upstream error: " + std::string(e.what()) + "\"}}\n\n";
        } else {
            sseErr = "data: {\"error\":{\"message\":\"Upstream error: " + std::string(e.what()) + "\",\"type\":\"upstream_error\"}}\n\ndata: [DONE]\n\n";
        }
        onChunk(sseErr.data(), sseErr.size());
        return 502;
    }
}

int RequestForwarder::forwardOpenAIStream(const nlohmann::json& requestBody, StreamCallback onChunk) {
    std::string modelName = requestBody.value("model", "");
    if (modelName.empty()) {
        std::string err = "data: {\"error\":{\"message\":\"Model field is required\",\"type\":\"invalid_request\"}}\n\ndata: [DONE]\n\n";
        onChunk(err.data(), err.size());
        return 400;
    }

    auto resolved = m_providers->resolveModel(modelName);
    if (resolved.ambiguous) {
        std::string opts;
        for (auto& p : resolved.ambiguousProviders) {
            if (!opts.empty()) opts += ", ";
            opts += p + "/" + modelName;
        }
        std::string err = "data: {\"error\":{\"message\":\"Model is ambiguous: " + opts + "\",\"type\":\"ambiguous_model\"}}\n\ndata: [DONE]\n\n";
        onChunk(err.data(), err.size());
        return 409;
    }
    if (!resolved.found) {
        std::string err = "data: {\"error\":{\"message\":\"Model not found\",\"type\":\"model_not_found\"}}\n\ndata: [DONE]\n\n";
        onChunk(err.data(), err.size());
        return 404;
    }

    std::string targetUrl;
    std::string upstreamPath;
    nlohmann::json upstreamBody;

    if (!resolved.openaiUrl.empty()) {
        targetUrl = resolved.openaiUrl;
        upstreamPath = "/v1/chat/completions";
        upstreamBody = requestBody;
        upstreamBody["model"] = resolved.modelName;
    } else if (!resolved.anthropicUrl.empty()) {
        // Cross-format streaming not supported
        std::string err = "data: {\"error\":{\"message\":\"Cross-format streaming not supported for this provider\",\"type\":\"unsupported\"}}\n\ndata: [DONE]\n\n";
        onChunk(err.data(), err.size());
        return 400;
    } else {
        std::string err = "data: {\"error\":{\"message\":\"Provider has no URL configured\",\"type\":\"config_error\"}}\n\ndata: [DONE]\n\n";
        onChunk(err.data(), err.size());
        return 500;
    }

    auto startTime = std::chrono::steady_clock::now();
    size_t totalBytes = 0;

    auto countingCallback = [&](const char* data, size_t len) -> bool {
        totalBytes += len;
        return onChunk(data, len);
    };

    int status = sendUpstreamStream(targetUrl, upstreamPath, upstreamBody, resolved.apiKey, 120, countingCallback);

    auto endTime = std::chrono::steady_clock::now();
    int durationMs = static_cast<int>(
        std::chrono::duration_cast<std::chrono::milliseconds>(endTime - startTime).count());

    TokenCounts counts;
    counts.promptTokens = static_cast<int>(requestBody.dump().size() / 4);
    counts.completionTokens = static_cast<int>(totalBytes / 4);
    counts.totalTokens = counts.promptTokens + counts.completionTokens;
    counts.fromResponse = false;

    recordStats(resolved.providerName, resolved.modelName, counts, durationMs, "openai");

    return status;
}

int RequestForwarder::forwardAnthropicStream(const nlohmann::json& requestBody, StreamCallback onChunk) {
    std::string modelName = requestBody.value("model", "");
    if (modelName.empty()) {
        std::string err = "event: error\ndata: {\"error\":{\"message\":\"Model field is required\",\"type\":\"invalid_request\"}}\n\n";
        onChunk(err.data(), err.size());
        return 400;
    }

    auto resolved = m_providers->resolveModel(modelName);
    if (resolved.ambiguous) {
        std::string opts;
        for (auto& p : resolved.ambiguousProviders) {
            if (!opts.empty()) opts += ", ";
            opts += p + "/" + modelName;
        }
        std::string err = "event: error\ndata: {\"error\":{\"message\":\"Model is ambiguous: " + opts + "\",\"type\":\"ambiguous_model\"}}\n\n";
        onChunk(err.data(), err.size());
        return 409;
    }
    if (!resolved.found) {
        std::string err = "event: error\ndata: {\"error\":{\"message\":\"Model not found\",\"type\":\"model_not_found\"}}\n\n";
        onChunk(err.data(), err.size());
        return 404;
    }

    std::string targetUrl;
    std::string upstreamPath;
    nlohmann::json upstreamBody;

    if (!resolved.anthropicUrl.empty()) {
        targetUrl = resolved.anthropicUrl;
        upstreamPath = "/v1/messages";
        upstreamBody = requestBody;
        upstreamBody["model"] = resolved.modelName;
    } else if (!resolved.openaiUrl.empty()) {
        // Cross-format streaming not supported
        std::string err = "event: error\ndata: {\"error\":{\"message\":\"Cross-format streaming not supported for this provider\",\"type\":\"unsupported\"}}\n\n";
        onChunk(err.data(), err.size());
        return 400;
    } else {
        std::string err = "event: error\ndata: {\"error\":{\"message\":\"Provider has no URL configured\",\"type\":\"config_error\"}}\n\n";
        onChunk(err.data(), err.size());
        return 500;
    }

    auto startTime = std::chrono::steady_clock::now();
    size_t totalBytes = 0;

    auto countingCallback = [&](const char* data, size_t len) -> bool {
        totalBytes += len;
        return onChunk(data, len);
    };

    int status = sendUpstreamStream(targetUrl, upstreamPath, upstreamBody, resolved.apiKey, 120, countingCallback, "anthropic");

    auto endTime = std::chrono::steady_clock::now();
    int durationMs = static_cast<int>(
        std::chrono::duration_cast<std::chrono::milliseconds>(endTime - startTime).count());

    TokenCounts counts;
    counts.promptTokens = static_cast<int>(requestBody.dump().size() / 4);
    counts.completionTokens = static_cast<int>(totalBytes / 4);
    counts.totalTokens = counts.promptTokens + counts.completionTokens;
    counts.fromResponse = false;

    recordStats(resolved.providerName, resolved.modelName, counts, durationMs, "anthropic");

    return status;
}
