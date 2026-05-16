#pragma once
#include <string>
#include <unordered_map>

enum class Lang { ZH, EN };

struct I18nPair { const char* zh; const char* en; };

class I18n {
public:
    I18n(Lang lang) : m_lang(lang) {}

    void setLang(Lang lang) { m_lang = lang; }
    Lang getLang() const { return m_lang; }

    const char* t(const char* key) const {
        auto it = s_dict.find(key);
        if (it == s_dict.end()) return key;
        return m_lang == Lang::ZH ? it->second.zh : it->second.en;
    }

    // Convenience: returns the other lang code for toggle buttons
    const char* otherLangCode() const {
        return m_lang == Lang::ZH ? "EN" : "中文";
    }

    static Lang fromString(const std::string& s) {
        if (s == "en") return Lang::EN;
        return Lang::ZH;
    }
    static std::string toString(Lang lang) {
        return lang == Lang::EN ? "en" : "zh";
    }

private:
    Lang m_lang;
    static inline const std::unordered_map<std::string, I18nPair> s_dict = {
        // Menu / Header bar
        {"menu.info", {"%d 服务商 | %d 模型 | 端口 %d", "%d providers | %d models | port %d"}},
        {"menu.theme", {"主题", "Theme"}},
        {"header.model", {"模型", "Model"}},

        // Tray
        {"tray.show", {"显示窗口", "Show Window"}},
        {"tray.exit", {"退出", "Exit"}},

        // Tabs / Sidebar
        {"tab.dashboard", {"仪表盘", "Dashboard"}},
        {"tab.providers", {"服务商管理", "Providers"}},
        {"tab.models", {"模型列表", "Models"}},
        {"tab.settings", {"设置", "Settings"}},
        {"tab.language", {"语言", "Language"}},
        {"tab.theme", {"主题", "Theme"}},

        // Dashboard
        {"dash.todayTokens", {"今日Token用量", "Today's Token Usage"}},
        {"dash.todayInput", {"输入", "Input"}},
        {"dash.todayOutput", {"输出", "Output"}},
        {"dash.todayReqs", {"今日请求数", "Today's Requests"}},
        {"dash.avgTokenPerReq", {"单位请求Token用量", "Avg Token/Request"}},
        {"dash.maxModel", {"最大消耗模型", "Top Model"}},
        {"dash.activeProviders", {"活跃服务商", "Active Providers"}},
        {"dash.availableModels", {"可用模型", "Available Models"}},
        {"dash.chartTokensTitle", {"Token 用量统计", "Token Usage"}},
        {"dash.chartReqsTitle", {"请求次数统计", "Request Count"}},
        {"dash.chartNoData", {"暂无数据", "No data available"}},
        {"dash.chartAvg", {"区间均值", "Avg"}},
        {"dash.chartMax", {"区间最高", "Max"}},
        {"dash.chartChange", {"区间变化", "Change"}},
        {"dash.recentReqs", {"最近请求", "Recent Requests"}},
        {"dash.colTime", {"时间", "Time"}},
        {"dash.colProvider", {"服务商", "Provider"}},
        {"dash.colModel", {"模型", "Model"}},
        {"dash.colTokens", {"Token", "Tokens"}},
        {"dash.colDuration", {"耗时", "Duration"}},
        {"dash.filterProvider", {"服务商", "Provider"}},
        {"dash.filterModel", {"模型", "Model"}},
        {"dash.filterTime", {"时间单位", "Time Unit"}},
        {"dash.filterAll", {"全部", "All"}},
        {"dash.chartTooltipDate", {"日期", "Date"}},
        {"dash.chartTooltipTokens", {"Token 用量", "Token Usage"}},
        {"dash.chartTooltipInput", {"输入", "Input"}},
        {"dash.chartTooltipOutput", {"输出", "Output"}},
        {"dash.chartTooltipReqs", {"请求次数", "Requests"}},
        {"dash.timeHour", {"小时", "Hour"}},
        {"dash.timeDay", {"日", "Day"}},
        {"dash.timeWeek", {"周", "Week"}},
        {"dash.timeMonth", {"月", "Month"}},
        {"dash.timeYear", {"年", "Year"}},

        // Providers
        {"prov.add", {"+ 添加服务商", "+ Add Provider"}},
        {"prov.count", {"  共 %zu 个服务商", "  %zu provider(s)"}},
        {"prov.colName", {"名称", "Name"}},
        {"prov.colOpenaiUrl", {"OpenAI URL", "OpenAI URL"}},
        {"prov.colAnthropicUrl", {"Anthropic URL", "Anthropic URL"}},
        {"prov.colModelCount", {"模型数", "Models"}},
        {"prov.colApiKey", {"API Key", "API Key"}},
        {"prov.colActions", {"操作", "Actions"}},
        {"prov.edit", {"编辑", "Edit"}},
        {"prov.delete", {"删除", "Delete"}},
        {"prov.addTitle", {"添加服务商", "Add Provider"}},
        {"prov.editTitle", {"编辑服务商", "Edit Provider"}},
        {"prov.nameHint", {"仅限小写字母、数字、连字符", "Lowercase letters, numbers, hyphens only"}},
        {"prov.keyLabel", {"API Key", "API Key"}},
        {"prov.keyLabelEdit", {"API Key (留空不修改)", "API Key (leave empty to keep)"}},
        {"prov.urlSection", {"API 地址 (至少填写一个)", "API URLs (at least one required)"}},
        {"prov.openaiUrlLabel", {"OpenAI 兼容地址", "OpenAI-compatible URL"}},
        {"prov.openaiUrlHint", {"兼容 /v1/chat/completions 的 API 地址", "API endpoint compatible with /v1/chat/completions"}},
        {"prov.anthropicUrlLabel", {"Anthropic 兼容地址", "Anthropic-compatible URL"}},
        {"prov.anthropicUrlHint", {"兼容 /v1/messages 的 API 地址", "API endpoint compatible with /v1/messages"}},
        {"prov.modelsLabel", {"模型列表 (每行一个):", "Models (one per line):"}},
        {"prov.modelsFormat", {"模型将以 服务商名/模型ID 格式暴露，无需添加前缀", "Models exposed as provider/model_id. No prefix needed here."}},
        {"prov.cancel", {"取消", "Cancel"}},
        {"prov.save", {"保存", "Save"}},
        {"prov.addBtn", {"添加", "Add"}},
        {"prov.confirmDelete", {"确认删除", "Confirm Delete"}},
        {"prov.confirmMsg", {"确定要删除服务商 %s 吗？", "Delete provider %s?"}},
        {"prov.confirmSub", {"该操作将同时移除所有关联模型。", "This will also remove all associated models."}},
        {"prov.confirmBtn", {"确认删除", "Confirm Delete"}},
        {"prov.fillAll", {"请填写名称、至少一个URL 和模型列表", "Name, at least one URL, and models are required"}},
        {"prov.fillKey", {"请填写 API Key", "Please enter API Key"}},
        {"prov.invalidName", {"名称只能包含小写字母、数字和连字符", "Name: only lowercase letters, numbers, and hyphens"}},
        {"prov.updated", {"服务商已更新", "Provider updated"}},
        {"prov.added", {"服务商已添加", "Provider added"}},
        {"prov.deleted", {"服务商已删除", "Provider deleted"}},

        // Models
        {"models.search", {"搜索模型...", "Search models..."}},
        {"models.colId", {"模型 ID", "Model ID"}},
        {"models.colProvider", {"服务商", "Provider"}},
        {"models.colUpstream", {"上游模型名", "Upstream Model"}},
        {"models.colUrls", {"支持", "Supports"}},
        {"models.noModels", {"暂无模型", "No models configured"}},
        {"models.noMatch", {"无匹配结果", "No matching results"}},
        {"models.speedTest", {"测速", "Test"}},
        {"models.speedTesting", {"测速中...", "Testing..."}},
        {"models.speedResult", {"%.0f ms", "%.0f ms"}},
        {"models.speedError", {"失败", "Failed"}},
        {"models.expandAll", {"全部展开", "Expand All"}},
        {"models.collapseAll", {"全部折叠", "Collapse All"}},
        {"models.copyId", {"复制ID", "Copy ID"}},
        {"models.copied", {"已复制!", "Copied!"}},
        {"models.testAll", {"一键测速", "Test All"}},
        {"models.testAllProgress", {"测速中 %d/%d...", "Testing %d/%d..."}},

        // Settings
        {"settings.title", {"网关设置", "Gateway Settings"}},
        {"settings.searchPlaceholder", {"搜索设置...", "Search settings..."}},
        {"settings.general", {"通用", "General"}},
        {"settings.appearance", {"外观", "Appearance"}},
        {"settings.network", {"网络", "Network"}},
        {"settings.port", {"监听端口", "Listen Port"}},
        {"settings.portHint", {"修改后需重启生效 (1-65535)", "Requires restart (1-65535)"}},
        {"settings.portDesc", {"HTTP API 服务器监听端口", "HTTP API server listen port"}},
        {"settings.timeout", {"请求超时 (秒)", "Request Timeout (s)"}},
        {"settings.timeoutDesc", {"上游 API 请求超时时间", "Upstream API request timeout"}},
        {"settings.logReqs", {"记录请求日志", "Log Requests"}},
        {"settings.logReqsDesc", {"在标准输出中打印每个请求", "Print each request to stdout"}},
        {"settings.darkTheme", {"深色主题", "Dark Theme"}},
        {"settings.darkThemeDesc", {"使用深色配色方案", "Use dark color scheme"}},
        {"settings.language", {"界面语言", "Interface Language"}},
        {"settings.languageDesc", {"GUI 界面的显示语言", "GUI display language"}},
        {"settings.saveBtn", {"保存设置", "Save Settings"}},
        {"settings.saved", {"设置已保存", "Settings saved"}},
        {"settings.localUrl", {"本地 API 地址", "Local API URL"}},
        {"settings.localUrlDesc", {"网关 HTTP API 访问地址", "Gateway HTTP API access URL"}},
        {"settings.copyUrl", {"复制地址", "Copy URL"}},
        {"settings.copied", {"已复制！", "Copied!"}},
        {"settings.localApiKeyTitle", {"API 鉴权", "API Authentication"}},
        {"settings.localApiKeyLabel", {"本地 API Key", "Local API Key"}},
        {"settings.localApiKeyDesc", {"访问本网关API需要此秘钥（留空则无需鉴权）", "Key required to access gateway API (empty = no auth)"}},
        {"settings.generateKey", {"随机生成", "Generate"}},
        {"settings.saveRestartBtn", {"保存并重启应用", "Save & Restart App"}},
        {"settings.savedRestart", {"设置已保存，正在重启...", "Settings saved, restarting..."}},
        {"settings.openaiUrl", {"OpenAI 兼容地址", "OpenAI-compatible URL"}},
        {"settings.openaiUrlDesc", {"设置 OpenAI 客户端的 base_url 为此地址", "Set as base_url in OpenAI clients"}},
        {"settings.anthropicUrl", {"Anthropic 兼容地址", "Anthropic-compatible URL"}},
        {"settings.anthropicUrlDesc", {"设置 Anthropic 客户端的 base_url 为此地址", "Set as base_url in Anthropic clients"}},
    };
};
