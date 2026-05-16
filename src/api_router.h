#pragma once
#include "httplib.h"
#include "config_manager.h"
#include "provider_manager.h"
#include "request_forwarder.h"
#include "stats_db.h"

class ApiRouter {
public:
    void setup(httplib::Server& server,
               ConfigManager* config,
               ProviderManager* providers,
               RequestForwarder* forwarder,
               StatsDatabase* stats);

    static std::string generateRandomKey(int length = 32);

private:
    static void addCorsHeaders(httplib::Response& res);
    static std::string maskApiKey(const std::string& key);

    ProviderManager* m_providers = nullptr;
};
