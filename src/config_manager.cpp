#include "config_manager.h"
#include <fstream>
#include <iostream>

bool ConfigManager::load(const std::string& path) {
    std::lock_guard<std::mutex> lock(m_mutex);
    m_path = path;
    try {
        std::ifstream f(path);
        if (!f.is_open()) {
            // Create default config
            m_config = {
                {"port", 8080},
                {"providers", nlohmann::json::array()},
                {"settings", {
                    {"log_requests", true},
                    {"default_timeout_sec", 60}
                }}
            };
            return false;
        }
        m_config = nlohmann::json::parse(f);
        if (!m_config.contains("port")) m_config["port"] = 8080;
        if (!m_config.contains("providers")) m_config["providers"] = nlohmann::json::array();
        if (!m_config.contains("settings")) {
            m_config["settings"] = {{"log_requests", true}, {"default_timeout_sec", 60}};
        }
        return true;
    } catch (const std::exception& e) {
        std::cerr << "Config load error: " << e.what() << std::endl;
        m_config = {{"port", 8080}, {"providers", nlohmann::json::array()},
                    {"settings", {{"log_requests", true}, {"default_timeout_sec", 60}}}};
        return false;
    }
}

bool ConfigManager::save(const std::string& path) {
    std::lock_guard<std::mutex> lock(m_mutex);
    try {
        std::ofstream f(path);
        if (!f.is_open()) return false;
        f << m_config.dump(2);
        return true;
    } catch (const std::exception& e) {
        std::cerr << "Config save error: " << e.what() << std::endl;
        return false;
    }
}

int ConfigManager::getPort() const {
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_config.value("port", 8080);
}

void ConfigManager::setPort(int port) {
    std::lock_guard<std::mutex> lock(m_mutex);
    m_config["port"] = port;
}

std::vector<Provider> ConfigManager::getProviders() const {
    std::lock_guard<std::mutex> lock(m_mutex);
    std::vector<Provider> result;
    if (!m_config.contains("providers")) return result;
    for (const auto& jp : m_config["providers"]) {
        Provider p;
        p.id = jp.value("id", "");
        p.name = jp.value("name", "");
        p.api_key = jp.value("api_key", "");
        p.openai_url = jp.value("openai_url", "");
        p.anthropic_url = jp.value("anthropic_url", "");
        // Backwards compat: old configs had base_url + compatibility
        if (p.openai_url.empty() && p.anthropic_url.empty() && jp.contains("base_url")) {
            std::string compat = jp.value("compatibility", "openai");
            if (compat == "anthropic")
                p.anthropic_url = jp.value("base_url", "");
            else
                p.openai_url = jp.value("base_url", "");
        }
        if (jp.contains("models") && jp["models"].is_array()) {
            for (const auto& m : jp["models"]) {
                p.models.push_back(m.get<std::string>());
            }
        }
        result.push_back(p);
    }
    return result;
}

void ConfigManager::setProviders(const std::vector<Provider>& providers) {
    std::lock_guard<std::mutex> lock(m_mutex);
    nlohmann::json arr = nlohmann::json::array();
    for (const auto& p : providers) {
        nlohmann::json jp;
        jp["id"] = p.id;
        jp["name"] = p.name;
        jp["api_key"] = p.api_key;
        jp["openai_url"] = p.openai_url;
        jp["anthropic_url"] = p.anthropic_url;
        jp["models"] = p.models;
        arr.push_back(jp);
    }
    m_config["providers"] = arr;
}

bool ConfigManager::getLogRequests() const {
    std::lock_guard<std::mutex> lock(m_mutex);
    if (!m_config.contains("settings")) return true;
    return m_config["settings"].value("log_requests", true);
}

void ConfigManager::setLogRequests(bool enabled) {
    std::lock_guard<std::mutex> lock(m_mutex);
    m_config["settings"]["log_requests"] = enabled;
}

int ConfigManager::getDefaultTimeoutSec() const {
    std::lock_guard<std::mutex> lock(m_mutex);
    if (!m_config.contains("settings")) return 60;
    return m_config["settings"].value("default_timeout_sec", 60);
}

void ConfigManager::setDefaultTimeoutSec(int sec) {
    std::lock_guard<std::mutex> lock(m_mutex);
    m_config["settings"]["default_timeout_sec"] = sec;
}

std::string ConfigManager::getLanguage() const {
    std::lock_guard<std::mutex> lock(m_mutex);
    if (!m_config.contains("settings")) return "zh";
    return m_config["settings"].value("language", "zh");
}

void ConfigManager::setLanguage(const std::string& lang) {
    std::lock_guard<std::mutex> lock(m_mutex);
    m_config["settings"]["language"] = lang;
}

std::string ConfigManager::getTheme() const {
    std::lock_guard<std::mutex> lock(m_mutex);
    if (!m_config.contains("settings")) return "dark";
    return m_config["settings"].value("theme", "dark");
}

void ConfigManager::setTheme(const std::string& theme) {
    std::lock_guard<std::mutex> lock(m_mutex);
    m_config["settings"]["theme"] = theme;
}

std::string ConfigManager::getLocalApiKey() const {
    std::lock_guard<std::mutex> lock(m_mutex);
    if (!m_config.contains("settings")) return "";
    return m_config["settings"].value("local_api_key", "");
}

void ConfigManager::setLocalApiKey(const std::string& key) {
    std::lock_guard<std::mutex> lock(m_mutex);
    if (key.empty())
        m_config["settings"].erase("local_api_key");
    else
        m_config["settings"]["local_api_key"] = key;
}
