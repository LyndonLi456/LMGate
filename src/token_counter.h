#pragma once
#include <string>
#include "json.hpp"

struct TokenCounts {
    int promptTokens = 0;
    int completionTokens = 0;
    int totalTokens = 0;
    bool fromResponse = false;
};

class TokenCounter {
public:
    static TokenCounts fromOpenAIResponse(const nlohmann::json& body);
    static TokenCounts fromAnthropicResponse(const nlohmann::json& body);
    static TokenCounts estimateFromText(const std::string& responseText,
                                         const std::string& promptText);
};
