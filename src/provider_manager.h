#pragma once
#include <string>
#include <vector>
#include <utility>
#include "config_manager.h"

struct ResolvedModel {
    std::string providerName;
    std::string providerId;
    std::string modelName;
    std::string apiKey;
    std::string openaiUrl;
    std::string anthropicUrl;
    bool found = false;
    bool ambiguous = false;
    std::vector<std::string> ambiguousProviders;
};

class ProviderManager {
public:
    explicit ProviderManager(ConfigManager* config);

    std::string addProvider(const Provider& p);
    bool updateProvider(const std::string& id, const Provider& p);
    bool deleteProvider(const std::string& id);

    std::vector<Provider> listAll() const;
    const Provider* getById(const std::string& id) const;
    ResolvedModel resolveModel(const std::string& modelName) const;

    // Returns {provider_name/model_name, provider_name}
    std::vector<std::pair<std::string, std::string>> listAllModels() const;

private:
    ConfigManager* m_config;
    static std::string generateUUID();
};
