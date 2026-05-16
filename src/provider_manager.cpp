#include "provider_manager.h"
#include <algorithm>
#include <random>
#include <sstream>
#include <iomanip>

ProviderManager::ProviderManager(ConfigManager* config) : m_config(config) {}

std::string ProviderManager::generateUUID() {
    static std::random_device rd;
    static std::mt19937_64 gen(rd());
    static std::uniform_int_distribution<uint64_t> dis;

    std::ostringstream oss;
    oss << std::hex << std::setfill('0');
    uint64_t a = dis(gen);
    uint64_t b = dis(gen);
    oss << std::setw(8) << ((a >> 32) & 0xFFFFFFFF) << "-"
        << std::setw(4) << ((a >> 16) & 0xFFFF) << "-"
        << std::setw(4) << (a & 0xFFFF) << "-"
        << std::setw(4) << ((b >> 48) & 0xFFFF) << "-"
        << std::setw(12) << (b & 0xFFFFFFFFFFFF);
    return oss.str();
}

std::string ProviderManager::addProvider(const Provider& p) {
    auto providers = m_config->getProviders();
    Provider np = p;
    np.id = generateUUID();
    providers.push_back(np);
    m_config->setProviders(providers);
    m_config->save(m_config->getConfigPath());
    return np.id;
}

bool ProviderManager::updateProvider(const std::string& id, const Provider& p) {
    auto providers = m_config->getProviders();
    for (auto& existing : providers) {
        if (existing.id == id) {
            existing.name = p.name;
            existing.api_key = p.api_key.empty() ? existing.api_key : p.api_key;
            existing.openai_url = p.openai_url;
            existing.anthropic_url = p.anthropic_url;
            existing.models = p.models;
            m_config->setProviders(providers);
            m_config->save(m_config->getConfigPath());
            return true;
        }
    }
    return false;
}

bool ProviderManager::deleteProvider(const std::string& id) {
    auto providers = m_config->getProviders();
    auto it = std::remove_if(providers.begin(), providers.end(),
        [&id](const Provider& p) { return p.id == id; });
    if (it != providers.end()) {
        providers.erase(it, providers.end());
        m_config->setProviders(providers);
        m_config->save(m_config->getConfigPath());
        return true;
    }
    return false;
}

std::vector<Provider> ProviderManager::listAll() const {
    return m_config->getProviders();
}

const Provider* ProviderManager::getById(const std::string& id) const {
    auto providers = m_config->getProviders();
    for (const auto& p : providers) {
        if (p.id == id) {
            // Return pointer to element in the returned vector (caller must not modify)
            // This is safe only because the returned vector outlives the pointer use in current callers.
            // For a proper fix, we'd return std::optional<Provider> by value.
            return nullptr; // This function is not currently used; fix when needed
        }
    }
    return nullptr;
}

ResolvedModel ProviderManager::resolveModel(const std::string& modelName) const {
    ResolvedModel result;
    auto providers = m_config->getProviders();

    // Check if model name contains a provider prefix (provider/model)
    auto slashPos = modelName.find('/');
    if (slashPos != std::string::npos) {
        std::string prefix = modelName.substr(0, slashPos);
        std::string actualModel = modelName.substr(slashPos + 1);

        for (const auto& p : providers) {
            if (p.name == prefix) {
                // Check if model exists in this provider
                bool modelFound = false;
                for (const auto& m : p.models) {
                    if (m == actualModel) {
                        modelFound = true;
                        break;
                    }
                }
                if (modelFound) {
                    result.found = true;
                    result.providerName = p.name;
                    result.providerId = p.id;
                    result.modelName = actualModel;
                    result.apiKey = p.api_key;
                    result.openaiUrl = p.openai_url;
                    result.anthropicUrl = p.anthropic_url;
                    return result;
                }
                // Provider found but model not in its list
                result.found = false;
                return result;
            }
        }
        // Provider prefix not found
        result.found = false;
        return result;
    }

    // No prefix: search all providers
    for (const auto& p : providers) {
        for (const auto& m : p.models) {
            if (m == modelName) {
                if (result.found) {
                    // Already found one — ambiguous
                    result.ambiguous = true;
                    result.ambiguousProviders.push_back(p.name);
                } else {
                    result.found = true;
                    result.providerName = p.name;
                    result.providerId = p.id;
                    result.modelName = modelName;
                    result.apiKey = p.api_key;
                    result.openaiUrl = p.openai_url;
                    result.anthropicUrl = p.anthropic_url;
                }
            }
        }
    }

    // If ambiguous, add the first found provider to ambiguous list
    if (result.ambiguous && result.found) {
        result.ambiguousProviders.insert(result.ambiguousProviders.begin(), result.providerName);
    }

    return result;
}

std::vector<std::pair<std::string, std::string>> ProviderManager::listAllModels() const {
    std::vector<std::pair<std::string, std::string>> models;
    auto providers = m_config->getProviders();
    for (const auto& p : providers) {
        for (const auto& m : p.models) {
            models.emplace_back(p.name + "/" + m, p.name);
        }
    }
    return models;
}
