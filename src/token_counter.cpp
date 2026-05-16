#include "token_counter.h"

TokenCounts TokenCounter::fromOpenAIResponse(const nlohmann::json& body) {
    TokenCounts tc;
    if (body.contains("usage")) {
        const auto& u = body["usage"];
        tc.promptTokens = u.value("prompt_tokens", 0);
        tc.completionTokens = u.value("completion_tokens", 0);
        tc.totalTokens = u.value("total_tokens", 0);
        tc.fromResponse = true;
    }
    return tc;
}

TokenCounts TokenCounter::fromAnthropicResponse(const nlohmann::json& body) {
    TokenCounts tc;
    if (body.contains("usage")) {
        const auto& u = body["usage"];
        tc.promptTokens = u.value("input_tokens", 0);
        tc.completionTokens = u.value("output_tokens", 0);
        tc.totalTokens = tc.promptTokens + tc.completionTokens;
        tc.fromResponse = true;
    }
    return tc;
}

TokenCounts TokenCounter::estimateFromText(const std::string& responseText,
                                            const std::string& promptText) {
    TokenCounts tc;
    // Rough heuristic: ~4 characters per token for English text
    tc.promptTokens = static_cast<int>(promptText.size() / 4);
    tc.completionTokens = static_cast<int>(responseText.size() / 4);
    tc.totalTokens = tc.promptTokens + tc.completionTokens;
    tc.fromResponse = false;
    return tc;
}
