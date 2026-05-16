#include "format_transformer.h"
#include <ctime>

nlohmann::json FormatTransformer::convertRequest(const nlohmann::json& body,
                                                   const std::string& from,
                                                   const std::string& to) {
    if (from == to) return body;

    nlohmann::json result;

    // openai -> anthropic
    if (from == "openai" && to == "anthropic") {
        result["model"] = body.value("model", "");
        result["max_tokens"] = body.value("max_tokens", 1024);
        if (body.contains("temperature")) result["temperature"] = body["temperature"];
        if (body.contains("top_p")) result["top_p"] = body["top_p"];
        if (body.contains("stop")) {
            if (body["stop"].is_array()) result["stop_sequences"] = body["stop"];
            else if (body["stop"].is_string()) result["stop_sequences"] = {body["stop"].get<std::string>()};
        }

        // Extract system message and remaining messages
        nlohmann::json messages = nlohmann::json::array();
        for (const auto& msg : body["messages"]) {
            if (msg["role"] == "system") {
                if (msg.contains("content")) {
                    if (msg["content"].is_array()) {
                        result["system"] = msg["content"];
                    } else {
                        result["system"] = msg["content"].get<std::string>();
                    }
                }
            } else {
                nlohmann::json m;
                m["role"] = msg["role"];
                if (msg.contains("content")) m["content"] = msg["content"];
                if (msg.contains("name")) m["name"] = msg["name"];
                messages.push_back(m);
            }
        }
        result["messages"] = messages;
        return result;
    }

    // anthropic -> openai
    if (from == "anthropic" && to == "openai") {
        result["model"] = body.value("model", "");
        result["max_tokens"] = body.value("max_tokens", 1024);
        if (body.contains("temperature")) result["temperature"] = body["temperature"];
        if (body.contains("top_p")) result["top_p"] = body["top_p"];
        if (body.contains("stop_sequences")) result["stop"] = body["stop_sequences"];

        nlohmann::json messages = nlohmann::json::array();

        // Handle system message
        if (body.contains("system")) {
            nlohmann::json sysMsg;
            sysMsg["role"] = "system";
            if (body["system"].is_array()) {
                sysMsg["content"] = body["system"];
            } else {
                sysMsg["content"] = body["system"].get<std::string>();
            }
            messages.push_back(sysMsg);
        }

        // Handle regular messages
        for (const auto& msg : body["messages"]) {
            nlohmann::json m;
            m["role"] = msg["role"];
            if (msg.contains("content")) m["content"] = msg["content"];
            if (msg.contains("name")) m["name"] = msg["name"];
            messages.push_back(m);
        }

        result["messages"] = messages;
        return result;
    }

    return body;
}

nlohmann::json FormatTransformer::convertResponse(const nlohmann::json& upstreamBody,
                                                    const std::string& from,
                                                    const std::string& to) {
    if (from == to) return upstreamBody;

    nlohmann::json result;

    // anthropic response -> openai format
    if (from == "anthropic" && to == "openai") {
        result["id"] = upstreamBody.value("id", "msg_000000");
        result["object"] = "chat.completion";
        result["created"] = std::time(nullptr);
        result["model"] = upstreamBody.value("model", "");

        // Extract content
        std::string content;
        if (upstreamBody.contains("content") && upstreamBody["content"].is_array()) {
            for (const auto& block : upstreamBody["content"]) {
                if (block.contains("text")) {
                    content += block["text"].get<std::string>();
                }
            }
        }

        nlohmann::json choice;
        choice["index"] = 0;
        choice["message"]["role"] = "assistant";
        choice["message"]["content"] = content;
        choice["finish_reason"] = mapStopReasonToFinish(
            upstreamBody.value("stop_reason", "end_turn"));

        result["choices"] = nlohmann::json::array({choice});

        // Pass through usage
        if (upstreamBody.contains("usage")) {
            const auto& u = upstreamBody["usage"];
            result["usage"] = {
                {"prompt_tokens", u.value("input_tokens", 0)},
                {"completion_tokens", u.value("output_tokens", 0)},
                {"total_tokens", u.value("input_tokens", 0) + u.value("output_tokens", 0)}
            };
        }

        return result;
    }

    // openai response -> anthropic format
    if (from == "openai" && to == "anthropic") {
        result["id"] = upstreamBody.value("id", "msg_000000");
        result["type"] = "message";
        result["role"] = "assistant";
        result["model"] = upstreamBody.value("model", "");

        std::string content;
        std::string finishReason;
        if (upstreamBody.contains("choices") && !upstreamBody["choices"].empty()) {
            const auto& choice = upstreamBody["choices"][0];
            if (choice.contains("message") && choice["message"].contains("content")) {
                content = choice["message"]["content"].get<std::string>();
            }
            finishReason = choice.value("finish_reason", "stop");
        }

        result["content"] = nlohmann::json::array({
            {{"type", "text"}, {"text", content}}
        });
        result["stop_reason"] = mapFinishReasonToStop(finishReason);

        // Pass through usage
        if (upstreamBody.contains("usage")) {
            const auto& u = upstreamBody["usage"];
            result["usage"] = {
                {"input_tokens", u.value("prompt_tokens", 0)},
                {"output_tokens", u.value("completion_tokens", 0)}
            };
        }

        return result;
    }

    return upstreamBody;
}

std::string FormatTransformer::mapStopReasonToFinish(const std::string& sr) {
    if (sr == "end_turn") return "stop";
    if (sr == "max_tokens") return "length";
    if (sr == "stop_sequence") return "stop";
    if (sr == "tool_use") return "tool_calls";
    return "stop";
}

std::string FormatTransformer::mapFinishReasonToStop(const std::string& fr) {
    if (fr == "stop") return "end_turn";
    if (fr == "length") return "max_tokens";
    if (fr == "tool_calls") return "tool_use";
    return "end_turn";
}
