#include "api_router.h"
#include <algorithm>
#include <ctime>
#include <random>
#include <cctype>

void ApiRouter::addCorsHeaders(httplib::Response& res) {
    res.set_header("Access-Control-Allow-Origin", "*");
    res.set_header("Access-Control-Allow-Methods", "GET, POST, PUT, DELETE, OPTIONS");
    res.set_header("Access-Control-Allow-Headers", "Content-Type, Authorization, x-api-key");
    res.set_header("Access-Control-Max-Age", "86400");
}

std::string ApiRouter::maskApiKey(const std::string& key) {
    if (key.length() <= 7) return "****";
    return key.substr(0, 3) + "..." + key.substr(key.length() - 4);
}

void ApiRouter::setup(httplib::Server& server,
                       ConfigManager* config,
                       ProviderManager* providers,
                       RequestForwarder* forwarder,
                       StatsDatabase* stats) {
    m_providers = providers;

    // ============ API Key Middleware ============
    server.set_pre_routing_handler([config](const httplib::Request& req, httplib::Response& res) {
        // Skip CORS preflight and health check
        if (req.method == "OPTIONS") return httplib::Server::HandlerResponse::Unhandled;
        if (req.path == "/health" || req.path == "/") return httplib::Server::HandlerResponse::Unhandled;

        std::string localKey = config->getLocalApiKey();
        if (localKey.empty()) return httplib::Server::HandlerResponse::Unhandled;

        // Check Authorization: Bearer <key>
        if (req.has_header("Authorization")) {
            std::string auth = req.get_header_value("Authorization");
            if (auth.size() > 7 && auth.substr(0, 7) == "Bearer " && auth.substr(7) == localKey)
                return httplib::Server::HandlerResponse::Unhandled;
        }

        // Check x-api-key header
        if (req.has_header("x-api-key")) {
            if (req.get_header_value("x-api-key") == localKey)
                return httplib::Server::HandlerResponse::Unhandled;
        }

        addCorsHeaders(res);
        res.status = 401;
        nlohmann::json err;
        err["error"]["message"] = "Unauthorized: valid API key required";
        err["error"]["type"] = "authentication_error";
        res.set_content(err.dump(), "application/json");
        return httplib::Server::HandlerResponse::Handled;
    });

    // ============ CORS Preflight ============
    server.Options(R"(.*)", [](const httplib::Request&, httplib::Response& res) {
        addCorsHeaders(res);
        res.status = 204;
    });

    // ============ Health ============
    server.Get("/health", [providers](const httplib::Request&, httplib::Response& res) {
        addCorsHeaders(res);
        auto plist = providers->listAll();
        nlohmann::json body;
        body["status"] = "ok";
        body["providers"] = plist.size();
        res.set_content(body.dump(), "application/json");
    });

    // ============ OpenAI-Compatible Endpoints ============

    server.Post("/v1/chat/completions", [forwarder, config](const httplib::Request& req, httplib::Response& res) {
        addCorsHeaders(res);
        if (config->getLogRequests()) {
            std::cout << "[OpenAI] " << req.body.size() << " bytes" << std::endl;
        }

        nlohmann::json body;
        try {
            body = nlohmann::json::parse(req.body);
        } catch (...) {
            nlohmann::json err;
            err["error"]["message"] = "Invalid JSON body";
            err["error"]["type"] = "invalid_request";
            res.status = 400;
            res.set_content(err.dump(), "application/json");
            return;
        }

        if (body.value("stream", false)) {
            // Streaming: pass through SSE chunks from upstream
            auto fwd = forwarder;
            auto reqBody = std::move(body);
            res.set_chunked_content_provider(
                "text/event-stream",
                [fwd, reqBody](size_t offset, httplib::DataSink& sink) -> bool {
                    if (offset > 0) return false;
                    fwd->forwardOpenAIStream(reqBody,
                        [&sink](const char* data, size_t len) -> bool {
                            return sink.write(data, len);
                        });
                    sink.done();
                    return false;
                },
                nullptr);
        } else {
            auto [status, respBody] = forwarder->forwardOpenAI(body);
            res.status = status;
            res.set_content(respBody.dump(), "application/json");
        }
    });

    server.Get("/v1/models", [providers](const httplib::Request&, httplib::Response& res) {
        addCorsHeaders(res);
        nlohmann::json body;
        body["object"] = "list";
        nlohmann::json data = nlohmann::json::array();

        auto models = providers->listAllModels();
        for (const auto& [modelId, providerName] : models) {
            nlohmann::json m;
            m["id"] = modelId;
            m["object"] = "model";
            m["created"] = 0;
            m["owned_by"] = providerName;
            data.push_back(m);
        }
        body["data"] = data;
        res.set_content(body.dump(), "application/json");
    });

    // ============ Anthropic-Compatible Endpoint ============

    server.Post("/v1/messages", [forwarder, config](const httplib::Request& req, httplib::Response& res) {
        addCorsHeaders(res);
        if (config->getLogRequests()) {
            std::cout << "[Anthropic] " << req.body.size() << " bytes" << std::endl;
        }

        nlohmann::json body;
        try {
            body = nlohmann::json::parse(req.body);
        } catch (...) {
            nlohmann::json err;
            err["error"]["message"] = "Invalid JSON body";
            err["error"]["type"] = "invalid_request";
            res.status = 400;
            res.set_content(err.dump(), "application/json");
            return;
        }

        if (body.value("stream", false)) {
            // Streaming: pass through SSE chunks from upstream
            auto fwd = forwarder;
            auto reqBody = std::move(body);
            res.set_chunked_content_provider(
                "text/event-stream",
                [fwd, reqBody](size_t offset, httplib::DataSink& sink) -> bool {
                    if (offset > 0) return false;
                    fwd->forwardAnthropicStream(reqBody,
                        [&sink](const char* data, size_t len) -> bool {
                            return sink.write(data, len);
                        });
                    sink.done();
                    return false;
                },
                nullptr);
        } else {
            auto [status, respBody] = forwarder->forwardAnthropic(body);
            res.status = status;
            res.set_content(respBody.dump(), "application/json");
        }
    });

    // ============ Management API - Providers ============

    server.Get("/api/providers", [providers](const httplib::Request&, httplib::Response& res) {
        addCorsHeaders(res);
        nlohmann::json arr = nlohmann::json::array();
        auto plist = providers->listAll();
        for (const auto& p : plist) {
            nlohmann::json jp;
            jp["id"] = p.id;
            jp["name"] = p.name;
            jp["api_key"] = maskApiKey(p.api_key);
            jp["openai_url"] = p.openai_url;
            jp["anthropic_url"] = p.anthropic_url;
            jp["models"] = p.models;
            arr.push_back(jp);
        }
        res.set_content(arr.dump(), "application/json");
    });

    server.Post("/api/providers", [providers](const httplib::Request& req, httplib::Response& res) {
        addCorsHeaders(res);
        try {
            auto body = nlohmann::json::parse(req.body);
            Provider p;
            p.name = body.value("name", "");
            p.api_key = body.value("api_key", "");
            p.openai_url = body.value("openai_url", "");
            p.anthropic_url = body.value("anthropic_url", "");
            if (body.contains("models") && body["models"].is_array()) {
                for (const auto& m : body["models"]) {
                    p.models.push_back(m.get<std::string>());
                }
            }

            if (p.name.empty() || (p.openai_url.empty() && p.anthropic_url.empty()) || p.models.empty()) {
                nlohmann::json err;
                err["error"] = "name, at least one URL (openai_url/ anthropic_url), and models are required";
                res.status = 400;
                res.set_content(err.dump(), "application/json");
                return;
            }

            std::string id = providers->addProvider(p);
            nlohmann::json resp;
            resp["id"] = id;
            resp["status"] = "ok";
            res.set_content(resp.dump(), "application/json");
        } catch (...) {
            nlohmann::json err;
            err["error"] = "Invalid JSON body";
            res.status = 400;
            res.set_content(err.dump(), "application/json");
        }
    });

    server.Put(R"(/api/providers/(.*))", [providers](const httplib::Request& req, httplib::Response& res) {
        addCorsHeaders(res);
        std::string id = req.matches[1];
        try {
            auto body = nlohmann::json::parse(req.body);
            Provider p;
            p.name = body.value("name", "");
            p.api_key = body.value("api_key", "");
            p.openai_url = body.value("openai_url", "");
            p.anthropic_url = body.value("anthropic_url", "");
            if (body.contains("models") && body["models"].is_array()) {
                for (const auto& m : body["models"]) {
                    p.models.push_back(m.get<std::string>());
                }
            }

            if (providers->updateProvider(id, p)) {
                nlohmann::json resp;
                resp["status"] = "ok";
                res.set_content(resp.dump(), "application/json");
            } else {
                nlohmann::json err;
                err["error"] = "Provider not found";
                res.status = 404;
                res.set_content(err.dump(), "application/json");
            }
        } catch (...) {
            nlohmann::json err;
            err["error"] = "Invalid JSON body";
            res.status = 400;
            res.set_content(err.dump(), "application/json");
        }
    });

    server.Delete(R"(/api/providers/(.*))", [providers](const httplib::Request& req, httplib::Response& res) {
        addCorsHeaders(res);
        std::string id = req.matches[1];
        if (providers->deleteProvider(id)) {
            nlohmann::json resp;
            resp["status"] = "ok";
            res.set_content(resp.dump(), "application/json");
        } else {
            nlohmann::json err;
            err["error"] = "Provider not found";
            res.status = 404;
            res.set_content(err.dump(), "application/json");
        }
    });

    server.Get(R"(/api/providers/(.*)/models)", [providers](const httplib::Request& req, httplib::Response& res) {
        addCorsHeaders(res);
        std::string id = req.matches[1];
        auto plist = providers->listAll();
        for (const auto& p : plist) {
            if (p.id == id) {
                nlohmann::json arr = p.models;
                res.set_content(arr.dump(), "application/json");
                return;
            }
        }
        nlohmann::json err;
        err["error"] = "Provider not found";
        res.status = 404;
        res.set_content(err.dump(), "application/json");
    });

    // ============ Management API - Stats ============

    server.Get("/api/stats/daily", [stats](const httplib::Request& req, httplib::Response& res) {
        addCorsHeaders(res);
        int days = 7;
        if (req.has_param("days")) {
            try { days = std::stoi(req.get_param_value("days")); } catch (...) {}
        }
        auto data = stats->getDailyUsage(days);
        nlohmann::json arr = nlohmann::json::array();
        for (const auto& d : data) {
            nlohmann::json jd;
            jd["date"] = d.date;
            jd["totalTokens"] = d.totalTokens;
            jd["requestCount"] = d.requestCount;
            arr.push_back(jd);
        }
        res.set_content(arr.dump(), "application/json");
    });

    server.Get("/api/stats/by-model", [stats](const httplib::Request& req, httplib::Response& res) {
        addCorsHeaders(res);
        int days = 30;
        if (req.has_param("days")) {
            try { days = std::stoi(req.get_param_value("days")); } catch (...) {}
        }
        auto data = stats->getUsageByModel(days);
        nlohmann::json arr = nlohmann::json::array();
        for (const auto& d : data) {
            nlohmann::json jd;
            jd["date"] = d.date;
            jd["totalTokens"] = d.totalTokens;
            jd["requestCount"] = d.requestCount;
            arr.push_back(jd);
        }
        res.set_content(arr.dump(), "application/json");
    });

    server.Get("/api/stats/by-provider", [stats](const httplib::Request& req, httplib::Response& res) {
        addCorsHeaders(res);
        int days = 30;
        if (req.has_param("days")) {
            try { days = std::stoi(req.get_param_value("days")); } catch (...) {}
        }
        auto data = stats->getUsageByProvider(days);
        nlohmann::json arr = nlohmann::json::array();
        for (const auto& d : data) {
            nlohmann::json jd;
            jd["date"] = d.date;
            jd["totalTokens"] = d.totalTokens;
            jd["requestCount"] = d.requestCount;
            arr.push_back(jd);
        }
        res.set_content(arr.dump(), "application/json");
    });

    server.Get("/api/stats/recent", [stats](const httplib::Request& req, httplib::Response& res) {
        addCorsHeaders(res);
        int limit = 50;
        if (req.has_param("limit")) {
            try { limit = std::stoi(req.get_param_value("limit")); } catch (...) {}
        }
        auto data = stats->getRecentRequests(limit);
        nlohmann::json arr = nlohmann::json::array();
        for (const auto& r : data) {
            nlohmann::json jr;
            jr["timestamp"] = r.timestamp;
            jr["provider"] = r.provider;
            jr["model"] = r.model;
            jr["totalTokens"] = r.totalTokens;
            jr["durationMs"] = r.durationMs;
            arr.push_back(jr);
        }
        res.set_content(arr.dump(), "application/json");
    });

    // ============ Management API - Settings ============

    server.Get("/api/settings", [config](const httplib::Request&, httplib::Response& res) {
        addCorsHeaders(res);
        nlohmann::json s;
        s["port"] = config->getPort();
        s["default_timeout_sec"] = config->getDefaultTimeoutSec();
        s["log_requests"] = config->getLogRequests();
        s["language"] = config->getLanguage();
        s["theme"] = config->getTheme();
        s["local_api_key"] = config->getLocalApiKey();
        res.set_content(s.dump(), "application/json");
    });

    server.Put("/api/settings", [config](const httplib::Request& req, httplib::Response& res) {
        addCorsHeaders(res);
        try {
            auto body = nlohmann::json::parse(req.body);
            if (body.contains("port")) config->setPort(body["port"].get<int>());
            if (body.contains("default_timeout_sec")) config->setDefaultTimeoutSec(body["default_timeout_sec"].get<int>());
            if (body.contains("log_requests")) config->setLogRequests(body["log_requests"].get<bool>());
            if (body.contains("language")) config->setLanguage(body["language"].get<std::string>());
            if (body.contains("theme")) config->setTheme(body["theme"].get<std::string>());
            if (body.contains("local_api_key")) config->setLocalApiKey(body["local_api_key"].get<std::string>());
            config->save(config->getConfigPath());

            nlohmann::json resp;
            resp["status"] = "ok";
            res.set_content(resp.dump(), "application/json");
        } catch (...) {
            nlohmann::json err;
            err["error"] = "Invalid JSON body";
            res.status = 400;
            res.set_content(err.dump(), "application/json");
        }
    });

    // ============ Root ============
    server.Get("/", [](const httplib::Request&, httplib::Response& res) {
        addCorsHeaders(res);
        res.set_content("LMGate LLM API Gateway v1.1.0 - Use the native GUI application for management.", "text/plain");
    });
}

std::string ApiRouter::generateRandomKey(int length) {
    static const char hex[] = "0123456789abcdef";
    static std::mt19937 rng(std::random_device{}());
    std::uniform_int_distribution<int> dist(0, 15);
    std::string key(length, '0');
    for (int i = 0; i < length; i++) key[i] = hex[dist(rng)];
    return key;
}
