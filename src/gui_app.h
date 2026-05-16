#pragma once
#include "config_manager.h"
#include "provider_manager.h"
#include "stats_db.h"
#include "i18n.h"
#include <string>
#include <vector>
#include <map>
#include <atomic>
#include <thread>

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <shellapi.h>
#include <GL/gl.h>

struct ImVec2;
struct ImFont;

class GuiApp {
public:
    GuiApp(ConfigManager* config, ProviderManager* providers, StatsDatabase* stats);

    bool init(const char* title, int width, int height);
    int run();
    void shutdown();

private:
    void applyTheme();
    void renderDashboard();
    void renderProviders();
    void renderModels();
    void renderSettings();
    void renderHeader();
    void renderSidebar();
    void showToast(const char* msg, bool isError = false);

    // Texture loading
    void loadTextures();
    void loadSidebarIcons();
    void freeTextures();
    GLuint loadPngTexture(const char* path);
    GLuint loadIcoTexture(const char* path);

    // System tray
    bool createTrayIcon();
    void removeTrayIcon();
    void showTrayMenu();
    friend LRESULT CALLBACK mainWndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);
    static GuiApp* s_instance;

    ConfigManager* m_config;
    ProviderManager* m_providers;
    StatsDatabase* m_stats;
    I18n m_i18n;

    // Theme
    bool m_darkTheme = false; // default to light theme in v2.0

    // Sidebar navigation
    int m_activeTab = 0;           // 0=Dash,1=Prov,2=Models,3=Lang,4=Theme,5=Settings

    // Toast
    char m_toastMsg[256] = {};
    float m_toastTimer = 0;
    bool m_toastError = false;

    // Provider editor
    bool m_showProviderModal = false;
    bool m_providerEditMode = false;
    std::string m_editProviderId;
    char m_provNameBuf[64] = {};
    char m_provKeyBuf[256] = {};
    char m_provOpenaiUrlBuf[512] = {};
    char m_provAnthropicUrlBuf[512] = {};
    char m_provModelsBuf[4096] = {};

    // Delete confirm
    bool m_showDeleteConfirm = false;
    std::string m_deleteProviderId;
    std::string m_deleteProviderName;

    // Settings
    int m_portBuf = 8080;
    int m_timeoutBuf = 60;
    bool m_logRequests = true;
    int m_langIdx = 0;
    int m_settingsCategory = 0;
    char m_settingsSearch[64] = {};
    char m_localApiKeyBuf[128] = {};
    int m_runResult = 0;

    // Card background textures (OpenGL)
    GLuint m_cardTex[4] = {};
    GLuint m_sunIcon = 0;
    GLuint m_moonIcon = 0;
    bool m_texturesLoaded = false;

    // Window control icon textures (minimize, maximize, restore, close)
    GLuint m_winMinIcon = 0;
    GLuint m_winMaxIcon = 0;
    GLuint m_winRestoreIcon = 0;
    GLuint m_winCloseIcon = 0;

    // Sidebar icon textures [6 menu items][2 themes: 0=light, 1=dark]
    GLuint m_sidebarIcons[6][2] = {};
    bool m_sidebarIconsLoaded = false;

    // Larger font for card values and header brand
    ImFont* m_largeFont = nullptr;

    // Model speed test
    std::map<std::string, float> m_speedLatency;   // model_full_id -> ms
    std::string m_speedRunning;                     // model_full_id currently testing
    std::atomic<bool> m_speedBatchActive{false};
    std::vector<std::string> m_speedQueue;
    std::atomic<int> m_speedTotal{0};
    std::atomic<int> m_speedCompleted{0};
    std::thread m_speedThread;
    void speedTestRunner();

    // Collapsed state for provider groups in model list
    std::map<std::string, bool> m_providerCollapsed;

    // Window
    HWND m_hwnd = nullptr;
    HGLRC m_glContext = nullptr;
    bool m_running = false;
    bool m_minimizedToTray = false;
    int m_windowWidth = 1100;
    int m_windowHeight = 700;

    // Tray
    NOTIFYICONDATAW m_nid = {};
    bool m_trayCreated = false;
};
