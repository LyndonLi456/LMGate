#pragma once
#include <string>
#include "json.hpp"

class FormatTransformer {
public:
    // Convert request body between formats
    // from/to: "openai" or "anthropic"
    static nlohmann::json convertRequest(const nlohmann::json& body,
                                          const std::string& from,
                                          const std::string& to);

    // Convert response body back to original format
    static nlohmann::json convertResponse(const nlohmann::json& upstreamBody,
                                           const std::string& from,
                                           const std::string& to);

private:
    static std::string mapStopReasonToFinish(const std::string& sr);
    static std::string mapFinishReasonToStop(const std::string& fr);
};
