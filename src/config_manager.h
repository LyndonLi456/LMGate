#pragma once
#include <string>
#include <vector>
#include <mutex>
#include "json.hpp"

struct Provider {
    std::string id;
    std::string name;
    std::string api_key;
    std::string openai_url;
    std::string anthropic_url;
    std::vector<std::string> models;
};

class ConfigManager {
public:
    bool load(const std::string& path);
    bool save(const std::string& path);

    int getPort() const;
    void setPort(int port);

    std::vector<Provider> getProviders() const;
    void setProviders(const std::vector<Provider>& providers);

    bool getLogRequests() const;
    void setLogRequests(bool enabled);

    int getDefaultTimeoutSec() const;
    void setDefaultTimeoutSec(int sec);

    std::string getConfigPath() const { return m_path; }

    std::string getLanguage() const;
    void setLanguage(const std::string& lang);

    std::string getTheme() const;
    void setTheme(const std::string& theme);

    std::string getLocalApiKey() const;
    void setLocalApiKey(const std::string& key);

private:
    nlohmann::json m_config;
    std::string m_path;
    mutable std::mutex m_mutex;
};
