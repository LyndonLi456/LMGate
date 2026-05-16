#include "httplib.h"
#include "gui_app.h"
#include "png_loader.h"
#include "api_router.h"
#include "imgui.h"
#include "imgui_impl_win32.h"
#include "imgui_impl_opengl3.h"

#include <cstdio>
#include <ctime>
#include <chrono>
#include <algorithm>
#include <sstream>
#include <iomanip>
#include <cstdlib>
#include <fstream>
#include <cmath>

GuiApp* GuiApp::s_instance = nullptr;

extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);

static constexpr UINT WM_TRAYCB = WM_APP + 1;
static constexpr UINT WM_TRAY_TASKBARCREATED = WM_APP + 2;

// ---- Window Procedure ----

LRESULT WINAPI mainWndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    if (ImGui_ImplWin32_WndProcHandler(hWnd, msg, wParam, lParam))
        return true;

    auto* app = GuiApp::s_instance;

    switch (msg) {
    case WM_GETMINMAXINFO: {
        MINMAXINFO* mmi = (MINMAXINFO*)lParam;
        mmi->ptMinTrackSize.x = 800;
        mmi->ptMinTrackSize.y = 500;
        return 0;
    }
    case WM_NCHITTEST: {
        // Let DefWindowProc handle resize borders and caption buttons
        LRESULT hit = DefWindowProcW(hWnd, msg, wParam, lParam);
        // But redirect caption area (draggable) to client since we have no native title bar
        if (hit == HTCAPTION) return HTCLIENT;
        return hit;
    }
    case WM_SIZE:
        if (wParam == SIZE_MINIMIZED && app) {
            app->m_minimizedToTray = true;
            ShowWindow(hWnd, SW_HIDE);
            return 0;
        }
        if (app && wParam == SIZE_RESTORED && app->m_minimizedToTray) {
            app->m_minimizedToTray = false;
        }
        return 0;
    case WM_CLOSE:
        if (app) {
            app->m_minimizedToTray = true;
            ShowWindow(hWnd, SW_HIDE);
        }
        return 0;
    case WM_DESTROY:
        PostQuitMessage(0);
        return 0;
    case WM_TRAYCB:
        if (lParam == WM_LBUTTONUP || lParam == WM_LBUTTONDBLCLK) {
            if (app) {
                app->m_minimizedToTray = false;
                ShowWindow(hWnd, SW_SHOW);
                SetForegroundWindow(hWnd);
            }
        } else if (lParam == WM_RBUTTONUP) {
            if (app) app->showTrayMenu();
        }
        return 0;
    }
    return DefWindowProcW(hWnd, msg, wParam, lParam);
}

// ---- OpenGL Context Helpers ----

static HGLRC createGLContext(HWND hWnd) {
    HDC hdc = GetDC(hWnd);
    PIXELFORMATDESCRIPTOR pfd = {};
    pfd.nSize = sizeof(pfd);
    pfd.nVersion = 1;
    pfd.dwFlags = PFD_DRAW_TO_WINDOW | PFD_SUPPORT_OPENGL | PFD_DOUBLEBUFFER;
    pfd.iPixelType = PFD_TYPE_RGBA;
    pfd.cColorBits = 32;
    pfd.cDepthBits = 24;
    pfd.cStencilBits = 8;
    pfd.iLayerType = PFD_MAIN_PLANE;

    int pixelFormat = ChoosePixelFormat(hdc, &pfd);
    SetPixelFormat(hdc, pixelFormat, &pfd);

    HGLRC hglrc = wglCreateContext(hdc);
    wglMakeCurrent(hdc, hglrc);
    ReleaseDC(hWnd, hdc);
    return hglrc;
}

static void destroyGLContext(HWND hWnd, HGLRC hglrc) {
    HDC hdc = GetDC(hWnd);
    wglMakeCurrent(nullptr, nullptr);
    wglDeleteContext(hglrc);
    ReleaseDC(hWnd, hdc);
}

// ---- Texture Loading Helpers ----

GLuint GuiApp::loadPngTexture(const char* path) {
    PngImage img;
    if (!pngLoad(path, img)) return 0;

    GLuint tex = 0;
    glGenTextures(1, &tex);
    glBindTexture(GL_TEXTURE_2D, tex);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, img.width, img.height, 0, GL_RGBA, GL_UNSIGNED_BYTE, img.pixels.data());
    glBindTexture(GL_TEXTURE_2D, 0);
    return tex;
}

GLuint GuiApp::loadIcoTexture(const char* path) {
    // Load ICO as HICON, then extract bitmap data to RGBA for GL texture
    HANDLE hIcon = LoadImageW(nullptr,
        std::wstring(path, path + strlen(path)).c_str(),
        IMAGE_ICON, 64, 64, LR_LOADFROMFILE);
    if (!hIcon) return 0;

    ICONINFO ii = {};
    if (!GetIconInfo((HICON)hIcon, &ii)) { DestroyIcon((HICON)hIcon); return 0; }

    BITMAP bm = {};
    GetObjectW(ii.hbmColor, sizeof(bm), &bm);

    BITMAPINFO bi = {};
    bi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bi.bmiHeader.biWidth = bm.bmWidth;
    bi.bmiHeader.biHeight = -bm.bmHeight; // top-down
    bi.bmiHeader.biPlanes = 1;
    bi.bmiHeader.biBitCount = 32;
    bi.bmiHeader.biCompression = BI_RGB;

    HDC hdc = GetDC(nullptr);
    std::vector<uint8_t> pixels(bm.bmWidth * bm.bmHeight * 4);
    GetDIBits(hdc, ii.hbmColor, 0, bm.bmHeight, pixels.data(), &bi, DIB_RGB_COLORS);
    ReleaseDC(nullptr, hdc);

    DeleteObject(ii.hbmColor);
    if (ii.hbmMask) DeleteObject(ii.hbmMask);
    DestroyIcon((HICON)hIcon);

    GLuint tex = 0;
    glGenTextures(1, &tex);
    glBindTexture(GL_TEXTURE_2D, tex);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, bm.bmWidth, bm.bmHeight, 0, GL_BGRA_EXT, GL_UNSIGNED_BYTE, pixels.data());
    glBindTexture(GL_TEXTURE_2D, 0);
    return tex;
}

void GuiApp::loadTextures() {
    if (m_texturesLoaded) return;
    const char* exeDir = ".";

    char path[512];
    for (int i = 0; i < 4; i++) {
        snprintf(path, sizeof(path), "%s/card_%d.png", exeDir, i + 1);
        m_cardTex[i] = loadPngTexture(path);
    }
    snprintf(path, sizeof(path), "%s/sun.ico", exeDir);
    m_sunIcon = loadIcoTexture(path);
    snprintf(path, sizeof(path), "%s/moon.ico", exeDir);
    m_moonIcon = loadIcoTexture(path);

    m_texturesLoaded = true;
    loadSidebarIcons();

    // Window control icons
    m_winMinIcon = loadPngTexture("./最小化.png");
    m_winMaxIcon = loadPngTexture("./最大化.png");
    m_winRestoreIcon = loadPngTexture("./恢复.png");
    m_winCloseIcon = loadPngTexture("./关闭窗口.png");
}

void GuiApp::loadSidebarIcons() {
    if (m_sidebarIconsLoaded) return;
    const char* names[6] = {
        "仪表盘", "供应商", "模型", "语言", "主题", "设置"
    };
    const char* themes[2] = {"light", "dark"};
    for (int i = 0; i < 6; i++) {
        for (int t = 0; t < 2; t++) {
            char path[256];
            // Handle the typo filename: 设置loght.png
            if (i == 5 && t == 0)
                snprintf(path, sizeof(path), "./设置loght.png");
            else
                snprintf(path, sizeof(path), "./%s%s.png", names[i], themes[t]);
            m_sidebarIcons[i][t] = loadPngTexture(path);
        }
    }
    m_sidebarIconsLoaded = true;
}

void GuiApp::freeTextures() {
    if (m_texturesLoaded) {
        for (int i = 0; i < 4; i++) {
            if (m_cardTex[i]) { glDeleteTextures(1, &m_cardTex[i]); m_cardTex[i] = 0; }
        }
        if (m_sunIcon) { glDeleteTextures(1, &m_sunIcon); m_sunIcon = 0; }
        if (m_moonIcon) { glDeleteTextures(1, &m_moonIcon); m_moonIcon = 0; }
        m_texturesLoaded = false;
    }
    if (m_sidebarIconsLoaded) {
        for (int i = 0; i < 6; i++) {
            for (int t = 0; t < 2; t++) {
                if (m_sidebarIcons[i][t]) {
                    glDeleteTextures(1, &m_sidebarIcons[i][t]);
                    m_sidebarIcons[i][t] = 0;
                }
            }
        }
        m_sidebarIconsLoaded = false;
    }
    if (m_winMinIcon) { glDeleteTextures(1, &m_winMinIcon); m_winMinIcon = 0; }
    if (m_winMaxIcon) { glDeleteTextures(1, &m_winMaxIcon); m_winMaxIcon = 0; }
    if (m_winRestoreIcon) { glDeleteTextures(1, &m_winRestoreIcon); m_winRestoreIcon = 0; }
    if (m_winCloseIcon) { glDeleteTextures(1, &m_winCloseIcon); m_winCloseIcon = 0; }
}

// ---- Helpers ----

static std::string formatTimestamp(int64_t ts) {
    time_t t = static_cast<time_t>(ts);
    std::tm* tm = std::localtime(&t);
    std::ostringstream oss;
    oss << std::setfill('0')
        << (tm->tm_year + 1900) << "-"
        << std::setw(2) << (tm->tm_mon + 1) << "-"
        << std::setw(2) << tm->tm_mday << " "
        << std::setw(2) << tm->tm_hour << ":"
        << std::setw(2) << tm->tm_min;
    return oss.str();
}

static std::string formatNumber(int n) {
    if (n >= 1000000) {
        char buf[16]; snprintf(buf, sizeof(buf), "%.1fM", n / 1000000.0f);
        return buf;
    }
    if (n >= 1000) {
        char buf[16]; snprintf(buf, sizeof(buf), "%.1fK", n / 1000.0f);
        return buf;
    }
    return std::to_string(n);
}

// ---- Constructor & Lifecycle ----

GuiApp::GuiApp(ConfigManager* config, ProviderManager* providers, StatsDatabase* stats)
    : m_config(config), m_providers(providers), m_stats(stats),
      m_i18n(I18n::fromString(config->getLanguage())) {
    m_portBuf = config->getPort();
    m_timeoutBuf = config->getDefaultTimeoutSec();
    m_logRequests = config->getLogRequests();
    m_langIdx = (m_i18n.getLang() == Lang::EN) ? 1 : 0;
    m_darkTheme = (config->getTheme() == "dark");
    snprintf(m_localApiKeyBuf, sizeof(m_localApiKeyBuf), "%s", config->getLocalApiKey().c_str());
    s_instance = this;
}

bool GuiApp::init(const char* title, int width, int height) {
    m_windowWidth = width;
    m_windowHeight = height;

    // Build full path to logo.ico from config path (exe dir)
    std::string configPath = m_config->getConfigPath();
    std::string exeDir = configPath;
    auto pos = exeDir.find_last_of("/\\");
    if (pos != std::string::npos) exeDir = exeDir.substr(0, pos + 1);
    std::string logoPath = exeDir + "logo.ico";
    int wLogoLen = MultiByteToWideChar(CP_UTF8, 0, logoPath.c_str(), -1, nullptr, 0);
    std::vector<wchar_t> wLogoPath(wLogoLen);
    MultiByteToWideChar(CP_UTF8, 0, logoPath.c_str(), -1, wLogoPath.data(), wLogoLen);

    // Load logo icon for window class (taskbar icon)
    HICON hLogo = (HICON)LoadImageW(nullptr, wLogoPath.data(), IMAGE_ICON,
        GetSystemMetrics(SM_CXICON), GetSystemMetrics(SM_CYICON), LR_LOADFROMFILE);
    HICON hLogoSm = (HICON)LoadImageW(nullptr, wLogoPath.data(), IMAGE_ICON,
        GetSystemMetrics(SM_CXSMICON), GetSystemMetrics(SM_CYSMICON), LR_LOADFROMFILE);

    WNDCLASSEXW wc = {};
    wc.cbSize = sizeof(wc);
    wc.lpfnWndProc = mainWndProc;
    wc.hInstance = GetModuleHandle(nullptr);
    wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
    wc.hIcon = hLogo;
    wc.hIconSm = hLogoSm;
    wc.lpszClassName = L"LMGateWindow";
    wc.style = CS_OWNDC;
    RegisterClassExW(&wc);

    // WS_POPUP: no native title bar; WS_THICKFRAME: resize border;
    // WS_MAXIMIZEBOX/WS_MINIMIZEBOX: caption buttons via DWM
    DWORD style = WS_POPUP | WS_THICKFRAME | WS_SYSMENU | WS_MAXIMIZEBOX | WS_MINIMIZEBOX;
    RECT rect = { 0, 0, width, height };
    AdjustWindowRect(&rect, style, FALSE);

    int wchars = MultiByteToWideChar(CP_UTF8, 0, title, -1, nullptr, 0);
    std::vector<wchar_t> wtitle(wchars);
    MultiByteToWideChar(CP_UTF8, 0, title, -1, wtitle.data(), wchars);

    m_hwnd = CreateWindowExW(0, L"LMGateWindow", wtitle.data(),
        style,
        CW_USEDEFAULT, CW_USEDEFAULT, rect.right - rect.left, rect.bottom - rect.top,
        nullptr, nullptr, wc.hInstance, nullptr);

    if (!m_hwnd) return false;

    m_glContext = createGLContext(m_hwnd);
    if (!m_glContext) return false;

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.IniFilename = nullptr;

    // Load CJK font
    ImFontConfig fontCfg;
    fontCfg.MergeMode = false;
    const char* fontPaths[] = {
        "C:\\Windows\\Fonts\\msyh.ttc",
        "C:\\Windows\\Fonts\\simsun.ttc",
        "C:\\Windows\\Fonts\\msgothic.ttc",
    };
    bool fontLoaded = false;
    for (const char* fp : fontPaths) {
        std::ifstream test(fp, std::ios::binary);
        if (test.good()) {
            io.Fonts->AddFontFromFileTTF(fp, 18.0f, nullptr,
                io.Fonts->GetGlyphRangesChineseFull());
            fontLoaded = true;
            break;
        }
    }
    if (!fontLoaded) {
        io.Fonts->AddFontDefault();
    }

    // Larger font for card values and header brand
    {
        ImFontConfig largeCfg;
        largeCfg.MergeMode = false;
        for (const char* fp : fontPaths) {
            std::ifstream test(fp, std::ios::binary);
            if (test.good()) {
                m_largeFont = io.Fonts->AddFontFromFileTTF(fp, 28.0f, nullptr,
                    io.Fonts->GetGlyphRangesChineseFull());
                break;
            }
        }
    }

    applyTheme();

    ImGui_ImplWin32_Init(m_hwnd);
    ImGui_ImplOpenGL3_Init("#version 130");

    // Load textures for card backgrounds and icons
    loadTextures();

    // Create tray icon using logo.ico
    createTrayIcon();

    m_running = true;
    return true;
}

void GuiApp::shutdown() {
    m_running = false;
    m_speedBatchActive = false;
    if (m_speedThread.joinable()) m_speedThread.join();
    removeTrayIcon();
    freeTextures();
    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplWin32_Shutdown();
    ImGui::DestroyContext();

    if (m_glContext) {
        destroyGLContext(m_hwnd, m_glContext);
        m_glContext = nullptr;
    }
    if (m_hwnd) {
        DestroyWindow(m_hwnd);
        m_hwnd = nullptr;
    }
}

void GuiApp::showToast(const char* msg, bool isError) {
    snprintf(m_toastMsg, sizeof(m_toastMsg), "%s", msg);
    m_toastTimer = 3.0f;
    m_toastError = isError;
}

// ---- Tray Icon ----

bool GuiApp::createTrayIcon() {
    if (m_trayCreated) return true;

    // Build full path to logo.ico from config path (exe dir)
    std::string configPath = m_config->getConfigPath();
    std::string exeDir = configPath;
    auto pos = exeDir.find_last_of("/\\");
    if (pos != std::string::npos) exeDir = exeDir.substr(0, pos + 1);
    std::string logoPath = exeDir + "logo.ico";
    int wLogoLen = MultiByteToWideChar(CP_UTF8, 0, logoPath.c_str(), -1, nullptr, 0);
    std::vector<wchar_t> wLogoPath(wLogoLen);
    MultiByteToWideChar(CP_UTF8, 0, logoPath.c_str(), -1, wLogoPath.data(), wLogoLen);

    HICON hIcon = (HICON)LoadImageW(nullptr, wLogoPath.data(), IMAGE_ICON,
        GetSystemMetrics(SM_CXSMICON), GetSystemMetrics(SM_CYSMICON), LR_LOADFROMFILE);

    memset(&m_nid, 0, sizeof(m_nid));
    m_nid.cbSize = sizeof(NOTIFYICONDATAW);
    m_nid.hWnd = m_hwnd;
    m_nid.uID = 1;
    m_nid.uFlags = NIF_ICON | NIF_MESSAGE | NIF_TIP;
    m_nid.uCallbackMessage = WM_TRAYCB;
    m_nid.hIcon = hIcon;
    wcscpy_s(m_nid.szTip, L"LMGate - LLM API Gateway");

    m_trayCreated = Shell_NotifyIconW(NIM_ADD, &m_nid) != FALSE;
    // Don't destroy hIcon — the shell owns it now
    return m_trayCreated;
}

void GuiApp::removeTrayIcon() {
    if (m_trayCreated) {
        Shell_NotifyIconW(NIM_DELETE, &m_nid);
        m_trayCreated = false;
    }
}

void GuiApp::showTrayMenu() {
    HMENU hMenu = CreatePopupMenu();
    auto& _ = m_i18n;

    std::string showLabel = _.t("tray.show");
    std::string exitLabel = _.t("tray.exit");

    int wchars1 = MultiByteToWideChar(CP_UTF8, 0, showLabel.c_str(), -1, nullptr, 0);
    std::vector<wchar_t> wshow(wchars1);
    MultiByteToWideChar(CP_UTF8, 0, showLabel.c_str(), -1, wshow.data(), wchars1);

    int wchars2 = MultiByteToWideChar(CP_UTF8, 0, exitLabel.c_str(), -1, nullptr, 0);
    std::vector<wchar_t> wexit(wchars2);
    MultiByteToWideChar(CP_UTF8, 0, exitLabel.c_str(), -1, wexit.data(), wchars2);

    AppendMenuW(hMenu, MF_STRING, 1, wshow.data());
    AppendMenuW(hMenu, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(hMenu, MF_STRING, 2, wexit.data());

    POINT pt;
    GetCursorPos(&pt);
    SetForegroundWindow(m_hwnd);
    int cmd = TrackPopupMenu(hMenu, TPM_RETURNCMD | TPM_NONOTIFY,
        pt.x, pt.y, 0, m_hwnd, nullptr);
    DestroyMenu(hMenu);

    if (cmd == 1) {
        m_minimizedToTray = false;
        ShowWindow(m_hwnd, SW_SHOW);
        SetForegroundWindow(m_hwnd);
    } else if (cmd == 2) {
        m_running = false;
        PostQuitMessage(0);
    }
}

// ---- Theme ----

void GuiApp::applyTheme() {
    ImGuiStyle& st = ImGui::GetStyle();
    st = ImGuiStyle();

    st.WindowRounding = 10.0f;
    st.ChildRounding = 10.0f;
    st.FrameRounding = 8.0f;
    st.PopupRounding = 10.0f;
    st.ScrollbarRounding = 10.0f;
    st.GrabRounding = 8.0f;
    st.TabRounding = 8.0f;
    st.WindowPadding = ImVec2(12, 10);
    st.FramePadding = ImVec2(12, 8);
    st.ItemSpacing = ImVec2(10, 8);
    st.ItemInnerSpacing = ImVec2(8, 6);
    st.IndentSpacing = 22.0f;
    st.ScrollbarSize = 14.0f;
    st.GrabMinSize = 12.0f;
    st.WindowBorderSize = 0.0f;
    st.ChildBorderSize = 1.0f;
    st.FrameBorderSize = 1.0f;
    st.TabBorderSize = 1.0f;

    if (m_darkTheme) {
        ImGui::StyleColorsDark();
        st.Colors[ImGuiCol_WindowBg] = ImVec4(0.06f, 0.07f, 0.10f, 1.00f);
        st.Colors[ImGuiCol_ChildBg] = ImVec4(0.08f, 0.09f, 0.13f, 1.00f);
        st.Colors[ImGuiCol_PopupBg] = ImVec4(0.10f, 0.11f, 0.16f, 0.96f);
        st.Colors[ImGuiCol_Border] = ImVec4(0.15f, 0.17f, 0.24f, 1.00f);
        st.Colors[ImGuiCol_FrameBg] = ImVec4(0.11f, 0.12f, 0.18f, 1.00f);
        st.Colors[ImGuiCol_FrameBgHovered] = ImVec4(0.16f, 0.18f, 0.26f, 1.00f);
        st.Colors[ImGuiCol_FrameBgActive] = ImVec4(0.20f, 0.22f, 0.32f, 1.00f);
        st.Colors[ImGuiCol_TitleBg] = ImVec4(0.08f, 0.09f, 0.13f, 1.00f);
        st.Colors[ImGuiCol_TitleBgActive] = ImVec4(0.08f, 0.09f, 0.13f, 1.00f);
        st.Colors[ImGuiCol_TitleBgCollapsed] = ImVec4(0.08f, 0.09f, 0.13f, 1.00f);
        st.Colors[ImGuiCol_MenuBarBg] = ImVec4(0.10f, 0.11f, 0.16f, 1.00f);
        st.Colors[ImGuiCol_ScrollbarBg] = ImVec4(0.06f, 0.07f, 0.10f, 0.60f);
        st.Colors[ImGuiCol_ScrollbarGrab] = ImVec4(0.18f, 0.20f, 0.30f, 1.00f);
        st.Colors[ImGuiCol_ScrollbarGrabHovered] = ImVec4(0.25f, 0.28f, 0.40f, 1.00f);
        st.Colors[ImGuiCol_ScrollbarGrabActive] = ImVec4(0.35f, 0.38f, 0.55f, 1.00f);
        st.Colors[ImGuiCol_CheckMark] = ImVec4(0.42f, 0.54f, 1.00f, 1.00f);
        st.Colors[ImGuiCol_SliderGrab] = ImVec4(0.42f, 0.54f, 1.00f, 1.00f);
        st.Colors[ImGuiCol_SliderGrabActive] = ImVec4(0.55f, 0.65f, 1.00f, 1.00f);
        st.Colors[ImGuiCol_Button] = ImVec4(0.14f, 0.16f, 0.24f, 1.00f);
        st.Colors[ImGuiCol_ButtonHovered] = ImVec4(0.22f, 0.26f, 0.40f, 1.00f);
        st.Colors[ImGuiCol_ButtonActive] = ImVec4(0.42f, 0.54f, 1.00f, 1.00f);
        st.Colors[ImGuiCol_Header] = ImVec4(0.14f, 0.16f, 0.24f, 1.00f);
        st.Colors[ImGuiCol_HeaderHovered] = ImVec4(0.22f, 0.26f, 0.40f, 1.00f);
        st.Colors[ImGuiCol_HeaderActive] = ImVec4(0.28f, 0.34f, 0.50f, 1.00f);
        st.Colors[ImGuiCol_Separator] = ImVec4(0.15f, 0.17f, 0.24f, 1.00f);
        st.Colors[ImGuiCol_Tab] = ImVec4(0.10f, 0.12f, 0.18f, 1.00f);
        st.Colors[ImGuiCol_TabHovered] = ImVec4(0.22f, 0.26f, 0.40f, 1.00f);
        st.Colors[ImGuiCol_TabActive] = ImVec4(0.42f, 0.54f, 1.00f, 1.00f);
        st.Colors[ImGuiCol_TabUnfocused] = ImVec4(0.08f, 0.09f, 0.13f, 1.00f);
        st.Colors[ImGuiCol_TabUnfocusedActive] = ImVec4(0.18f, 0.22f, 0.36f, 1.00f);
        st.Colors[ImGuiCol_TableHeaderBg] = ImVec4(0.10f, 0.12f, 0.18f, 1.00f);
        st.Colors[ImGuiCol_TableBorderStrong] = ImVec4(0.15f, 0.17f, 0.24f, 1.00f);
        st.Colors[ImGuiCol_TableBorderLight] = ImVec4(0.10f, 0.11f, 0.16f, 1.00f);
        st.Colors[ImGuiCol_TextSelectedBg] = ImVec4(0.42f, 0.54f, 1.00f, 0.40f);
    } else {
        ImGui::StyleColorsLight();
        st.Colors[ImGuiCol_WindowBg] = ImVec4(0.96f, 0.96f, 0.98f, 1.00f);
        st.Colors[ImGuiCol_ChildBg] = ImVec4(0.93f, 0.93f, 0.96f, 1.00f);
        st.Colors[ImGuiCol_PopupBg] = ImVec4(0.96f, 0.96f, 0.98f, 0.96f);
        st.Colors[ImGuiCol_Border] = ImVec4(0.78f, 0.80f, 0.85f, 1.00f);
        st.Colors[ImGuiCol_FrameBg] = ImVec4(0.88f, 0.89f, 0.92f, 1.00f);
        st.Colors[ImGuiCol_FrameBgHovered] = ImVec4(0.82f, 0.83f, 0.88f, 1.00f);
        st.Colors[ImGuiCol_FrameBgActive] = ImVec4(0.76f, 0.78f, 0.85f, 1.00f);
        st.Colors[ImGuiCol_TitleBg] = ImVec4(0.92f, 0.92f, 0.95f, 1.00f);
        st.Colors[ImGuiCol_TitleBgActive] = ImVec4(0.92f, 0.92f, 0.95f, 1.00f);
        st.Colors[ImGuiCol_TitleBgCollapsed] = ImVec4(0.92f, 0.92f, 0.95f, 1.00f);
        st.Colors[ImGuiCol_MenuBarBg] = ImVec4(0.90f, 0.90f, 0.93f, 1.00f);
        st.Colors[ImGuiCol_ScrollbarBg] = ImVec4(0.90f, 0.90f, 0.93f, 0.60f);
        st.Colors[ImGuiCol_ScrollbarGrab] = ImVec4(0.70f, 0.70f, 0.75f, 1.00f);
        st.Colors[ImGuiCol_ScrollbarGrabHovered] = ImVec4(0.60f, 0.60f, 0.65f, 1.00f);
        st.Colors[ImGuiCol_ScrollbarGrabActive] = ImVec4(0.50f, 0.50f, 0.55f, 1.00f);
        st.Colors[ImGuiCol_CheckMark] = ImVec4(0.26f, 0.46f, 0.96f, 1.00f);
        st.Colors[ImGuiCol_SliderGrab] = ImVec4(0.26f, 0.46f, 0.96f, 1.00f);
        st.Colors[ImGuiCol_Button] = ImVec4(0.82f, 0.83f, 0.88f, 1.00f);
        st.Colors[ImGuiCol_ButtonHovered] = ImVec4(0.72f, 0.74f, 0.82f, 1.00f);
        st.Colors[ImGuiCol_ButtonActive] = ImVec4(0.26f, 0.46f, 0.96f, 0.40f);
        st.Colors[ImGuiCol_Header] = ImVec4(0.84f, 0.85f, 0.90f, 1.00f);
        st.Colors[ImGuiCol_HeaderHovered] = ImVec4(0.74f, 0.76f, 0.84f, 1.00f);
        st.Colors[ImGuiCol_HeaderActive] = ImVec4(0.64f, 0.66f, 0.76f, 1.00f);
        st.Colors[ImGuiCol_Separator] = ImVec4(0.78f, 0.80f, 0.85f, 1.00f);
        st.Colors[ImGuiCol_Tab] = ImVec4(0.86f, 0.87f, 0.91f, 1.00f);
        st.Colors[ImGuiCol_TabHovered] = ImVec4(0.72f, 0.74f, 0.82f, 1.00f);
        st.Colors[ImGuiCol_TabActive] = ImVec4(0.26f, 0.46f, 0.96f, 0.30f);
        st.Colors[ImGuiCol_TableHeaderBg] = ImVec4(0.88f, 0.89f, 0.92f, 1.00f);
        st.Colors[ImGuiCol_TableBorderStrong] = ImVec4(0.78f, 0.80f, 0.85f, 1.00f);
        st.Colors[ImGuiCol_TableBorderLight] = ImVec4(0.86f, 0.87f, 0.91f, 1.00f);
        st.Colors[ImGuiCol_TextSelectedBg] = ImVec4(0.26f, 0.46f, 0.96f, 0.30f);
    }
}

// ---- Dashboard ----

void GuiApp::renderDashboard() {
    auto& _ = m_i18n;
    auto providers = m_providers->listAll();

    // Build provider name list for filter dropdowns
    std::vector<std::string> providerNames;
    providerNames.push_back(_.t("dash.filterAll"));
    for (auto& p : providers) providerNames.push_back(p.name);

    // Get today's stats
    auto [todayPrompt, todayCompletion] = m_stats->getTodayTokenSplit();
    int todayTotal = todayPrompt + todayCompletion;
    int todayReqs = 0;
    auto daily = m_stats->getDailyUsage(1);
    if (!daily.empty()) todayReqs = daily[0].requestCount;
    int activeCount = (int)providers.size();
    int totalModels = 0;
    for (auto& p : providers) totalModels += (int)p.models.size();

    // Compute avg token/request and max consumption model
    int avgTokenPerReq = 0;
    if (todayReqs > 0) avgTokenPerReq = todayTotal / todayReqs;
    std::string topModelName = "-";
    {
        auto modelUsage = m_stats->getTodayModelUsage();
        if (!modelUsage.empty()) topModelName = modelUsage[0].date;
    }

    // ---- Summary Cards ----
    float avail = ImGui::GetContentRegionAvail().x;
    float cardW = (avail - 36) / 4.0f;
    float cardH = 105.0f;
    float rounding = 14.0f;

    ImVec2 basePos = ImGui::GetCursorScreenPos();

    // Helper to draw one card
    auto drawCard = [&](int idx, const char* label, const char* val1, const char* val2,
                        ImU32 tl, ImU32 tr, ImU32 bl, ImU32 br,
                        bool isTwoLine, const char* bigText, bool useLargeFont = false) {
        ImVec2 p0(basePos.x + idx * (cardW + 12), basePos.y);
        ImVec2 p1(p0.x + cardW, p0.y + cardH);
        ImDrawList* dl = ImGui::GetWindowDrawList();

        // Gradient background
        dl->AddRectFilledMultiColor(p0, p1, tl, tr, br, bl);
        dl->AddRect(p0, p1, IM_COL32(255, 255, 255, 30), rounding);

        // PNG texture overlay
        if (m_cardTex[idx]) {
            dl->AddImageRounded((ImTextureID)(uintptr_t)m_cardTex[idx],
                ImVec2(p0.x + 4, p0.y + 4), ImVec2(p1.x - 4, p1.y - 4),
                ImVec2(0, 0), ImVec2(1, 1), IM_COL32(255, 255, 255, 40),
                rounding - 2.0f);
        }

        // Label
        dl->AddText(ImVec2(p0.x + 16, p0.y + 14),
            IM_COL32(255, 255, 255, 190), label);

        if (useLargeFont && m_largeFont) ImGui::PushFont(m_largeFont);

        if (isTwoLine && val1 && val2) {
            char line1[64], line2[64];
            snprintf(line1, sizeof(line1), "%s %s", _.t("dash.todayInput"), val1);
            snprintf(line2, sizeof(line2), "%s %s", _.t("dash.todayOutput"), val2);
            dl->AddText(ImVec2(p0.x + 16, p0.y + 42), IM_COL32(255, 255, 255, 230), line1);
            dl->AddText(ImVec2(p0.x + 16, p0.y + 66), IM_COL32(255, 255, 255, 230), line2);
        } else if (bigText) {
            float textW = ImGui::CalcTextSize(bigText).x;
            dl->AddText(ImVec2(p0.x + (cardW - textW) * 0.5f, p0.y + (useLargeFont ? 42.0f : 54.0f)),
                IM_COL32(255, 255, 255, 255), bigText);
        } else {
            float textW = ImGui::CalcTextSize(val1).x;
            dl->AddText(ImVec2(p0.x + (cardW - textW) * 0.5f, p0.y + (useLargeFont ? 42.0f : 54.0f)),
                IM_COL32(255, 255, 255, 255), val1);
        }

        if (useLargeFont && m_largeFont) ImGui::PopFont();
    };

    // Card 1: Today's Token Usage — blue-green gradient (#4facfe -> #00f2fe)
    drawCard(0, _.t("dash.todayTokens"),
        formatNumber(todayPrompt).c_str(), formatNumber(todayCompletion).c_str(),
        IM_COL32(0x4F, 0xAC, 0xFE, 255), IM_COL32(0x00, 0xF2, 0xFE, 255),
        IM_COL32(0x00, 0xF2, 0xFE, 255), IM_COL32(0x4F, 0xAC, 0xFE, 255),
        true, nullptr);

    // Card 2: Today's Request Count — deep blue-purple gradient (#667eea -> #764ba2)
    drawCard(1, _.t("dash.todayReqs"),
        formatNumber(todayReqs).c_str(), nullptr,
        IM_COL32(0x66, 0x7E, 0xEA, 255), IM_COL32(0x76, 0x4B, 0xA2, 255),
        IM_COL32(0x76, 0x4B, 0xA2, 255), IM_COL32(0x66, 0x7E, 0xEA, 255),
        false, nullptr, true);

    // Card 3: Avg Token/Request — yellow-orange gradient (#f6d365 -> #fda085)
    {
        std::string avgStr = formatNumber(avgTokenPerReq);
        drawCard(2, _.t("dash.avgTokenPerReq"),
            avgStr.c_str(), nullptr,
            IM_COL32(0xF6, 0xD3, 0x65, 255), IM_COL32(0xFD, 0xA0, 0x85, 255),
            IM_COL32(0xFD, 0xA0, 0x85, 255), IM_COL32(0xF6, 0xD3, 0x65, 255),
            false, nullptr, true);
    }

    // Card 4: Top Model — red-pink gradient (#ff9a9e -> #fecfef)
    {
        std::string displayName = topModelName;
        if (displayName.size() > 24) displayName = displayName.substr(0, 22) + "...";
        drawCard(3, _.t("dash.maxModel"), displayName.c_str(), nullptr,
            IM_COL32(0xFF, 0x9A, 0x9E, 255), IM_COL32(0xFE, 0xCF, 0xEF, 255),
            IM_COL32(0xFE, 0xCF, 0xEF, 255), IM_COL32(0xFF, 0x9A, 0x9E, 255),
            false, displayName.c_str(), true);
    }

    // Move cursor past cards
    ImGui::SetCursorScreenPos(ImVec2(basePos.x, basePos.y + cardH + 16));
    ImGui::Spacing();

    // ---- Recent Requests ----
    auto recent = m_stats->getRecentRequests(20);
    ImGui::BeginChild("##recent", ImVec2(ImGui::GetContentRegionAvail().x, 0), ImGuiChildFlags_Border);
    ImGui::TextColored(ImVec4(0.55f, 0.57f, 0.65f, 1.0f), "%s", _.t("dash.recentReqs"));
    ImGui::Separator();
    ImGui::Spacing();

    if (ImGui::BeginTable("##recentTable", 5,
        ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_ScrollY,
        ImVec2(0, -1))) {
        ImGui::TableSetupColumn(_.t("dash.colTime"));
        ImGui::TableSetupColumn(_.t("dash.colProvider"));
        ImGui::TableSetupColumn(_.t("dash.colModel"));
        ImGui::TableSetupColumn(_.t("dash.colTokens"));
        ImGui::TableSetupColumn(_.t("dash.colDuration"));
        ImGui::TableHeadersRow();

        for (auto& r : recent) {
            ImGui::TableNextRow();
            ImGui::TableSetColumnIndex(0); ImGui::Text("%s", formatTimestamp(r.timestamp).c_str());
            ImGui::TableSetColumnIndex(1); ImGui::Text("%s", r.provider.c_str());
            ImGui::TableSetColumnIndex(2); ImGui::Text("%s", r.model.c_str());
            ImGui::TableSetColumnIndex(3); ImGui::Text("%d", r.totalTokens);
            ImGui::TableSetColumnIndex(4); ImGui::Text("%dms", r.durationMs);
        }
        ImGui::EndTable();
    }
    ImGui::EndChild();
}

// ---- Providers ----

void GuiApp::renderProviders() {
    auto& _ = m_i18n;
    auto providers = m_providers->listAll();

    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.26f, 0.46f, 0.96f, 1.0f));
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.32f, 0.54f, 1.00f, 1.0f));
    ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.18f, 0.36f, 0.80f, 1.0f));
    if (ImGui::Button(_.t("prov.add"), ImVec2(140, 38))) {
        m_showProviderModal = true;
        m_providerEditMode = false;
        m_editProviderId.clear();
        memset(m_provNameBuf, 0, sizeof(m_provNameBuf));
        memset(m_provKeyBuf, 0, sizeof(m_provKeyBuf));
        memset(m_provOpenaiUrlBuf, 0, sizeof(m_provOpenaiUrlBuf));
        memset(m_provAnthropicUrlBuf, 0, sizeof(m_provAnthropicUrlBuf));
        memset(m_provModelsBuf, 0, sizeof(m_provModelsBuf));
    }
    ImGui::PopStyleColor(3);
    ImGui::SameLine();

    char countBuf[128];
    snprintf(countBuf, sizeof(countBuf), _.t("prov.count"), providers.size());
    ImGui::TextColored(ImVec4(0.50f, 0.52f, 0.60f, 1.0f), "  %s", countBuf);
    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();

    ImGui::BeginChild("##providersTable", ImVec2(0, 0), ImGuiChildFlags_Border);

    ImGui::PushStyleVar(ImGuiStyleVar_CellPadding, ImVec2(8, 10));
    if (ImGui::BeginTable("##pTable", 6, ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_Resizable)) {
        ImGui::TableSetupColumn(_.t("prov.colName"));
        ImGui::TableSetupColumn(_.t("prov.colOpenaiUrl"));
        ImGui::TableSetupColumn(_.t("prov.colAnthropicUrl"));
        ImGui::TableSetupColumn(_.t("prov.colModelCount"));
        ImGui::TableSetupColumn(_.t("prov.colApiKey"));
        ImGui::TableSetupColumn(_.t("prov.colActions"));
        ImGui::TableHeadersRow();

        for (auto& p : providers) {
            ImGui::TableNextRow();
            ImGui::TableSetColumnIndex(0);
            ImGui::TextColored(ImVec4(0.42f, 0.54f, 1.00f, 1.0f), "%s", p.name.c_str());
            ImGui::TableSetColumnIndex(1);
            ImGui::TextColored(ImVec4(0.50f, 0.52f, 0.60f, 1.0f), "%s",
                p.openai_url.empty() ? "-" : p.openai_url.c_str());
            ImGui::TableSetColumnIndex(2);
            ImGui::TextColored(ImVec4(0.50f, 0.52f, 0.60f, 1.0f), "%s",
                p.anthropic_url.empty() ? "-" : p.anthropic_url.c_str());
            ImGui::TableSetColumnIndex(3); ImGui::Text("%zu", p.models.size());
            ImGui::TableSetColumnIndex(4);
            ImGui::TextColored(ImVec4(0.50f, 0.52f, 0.60f, 1.0f), "%.3s...%.*s",
                p.api_key.c_str(),
                static_cast<int>(std::min<size_t>(4, p.api_key.size())),
                p.api_key.c_str() + (p.api_key.size() > 4 ? p.api_key.size() - 4 : 0));
            ImGui::TableSetColumnIndex(5);
            if (ImGui::Button((std::string(_.t("prov.edit")) + "##" + p.id).c_str(), ImVec2(50, 26))) {
                m_showProviderModal = true; m_providerEditMode = true;
                m_editProviderId = p.id;
                snprintf(m_provNameBuf, sizeof(m_provNameBuf), "%s", p.name.c_str());
                memset(m_provKeyBuf, 0, sizeof(m_provKeyBuf));
                snprintf(m_provOpenaiUrlBuf, sizeof(m_provOpenaiUrlBuf), "%s", p.openai_url.c_str());
                snprintf(m_provAnthropicUrlBuf, sizeof(m_provAnthropicUrlBuf), "%s", p.anthropic_url.c_str());
                std::string ms; for (auto& m : p.models) { if (!ms.empty()) ms += "\n"; ms += m; }
                snprintf(m_provModelsBuf, sizeof(m_provModelsBuf), "%s", ms.c_str());
            }
            ImGui::SameLine();
            if (ImGui::Button((std::string(_.t("prov.delete")) + "##" + p.id).c_str(), ImVec2(50, 26))) {
                m_showDeleteConfirm = true; m_deleteProviderId = p.id; m_deleteProviderName = p.name;
            }
        }
        ImGui::EndTable();
        ImGui::PopStyleVar();
    }
    ImGui::EndChild();

    // Provider Modal
    const char* modalTitle = m_providerEditMode ? _.t("prov.editTitle") : _.t("prov.addTitle");
    if (m_showProviderModal) {
        ImGui::OpenPopup(modalTitle);
        m_showProviderModal = false;
    }
    ImVec2 center = ImGui::GetMainViewport()->GetCenter();
    ImGui::SetNextWindowPos(center, ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
    ImGui::SetNextWindowSize(ImVec2(560, 580), ImGuiCond_Appearing);

    if (ImGui::BeginPopupModal(modalTitle, nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::InputText(_.t("prov.colName"), m_provNameBuf, sizeof(m_provNameBuf));
        ImGui::TextColored(ImVec4(0.50f, 0.50f, 0.55f, 1.0f), "%s", _.t("prov.nameHint"));
        ImGui::Spacing();

        ImGui::InputText(m_providerEditMode ? _.t("prov.keyLabelEdit") : _.t("prov.keyLabel"),
            m_provKeyBuf, sizeof(m_provKeyBuf), ImGuiInputTextFlags_Password);
        ImGui::Spacing();

        ImGui::Separator();
        ImGui::TextColored(ImVec4(0.42f, 0.54f, 1.00f, 1.0f), "%s", _.t("prov.urlSection"));
        ImGui::Spacing();

        ImGui::InputTextWithHint(_.t("prov.openaiUrlLabel"), "https://api.openai.com/v1",
            m_provOpenaiUrlBuf, sizeof(m_provOpenaiUrlBuf));
        ImGui::TextColored(ImVec4(0.50f, 0.50f, 0.55f, 1.0f), "%s", _.t("prov.openaiUrlHint"));
        ImGui::Spacing();

        ImGui::InputTextWithHint(_.t("prov.anthropicUrlLabel"), "https://api.anthropic.com",
            m_provAnthropicUrlBuf, sizeof(m_provAnthropicUrlBuf));
        ImGui::TextColored(ImVec4(0.50f, 0.50f, 0.55f, 1.0f), "%s", _.t("prov.anthropicUrlHint"));
        ImGui::Spacing();

        ImGui::Separator();
        ImGui::Text("%s", _.t("prov.modelsLabel"));
        ImGui::InputTextMultiline("##models", m_provModelsBuf, sizeof(m_provModelsBuf), ImVec2(-1, 100));
        ImGui::TextColored(ImVec4(0.50f, 0.50f, 0.55f, 1.0f), "%s", _.t("prov.modelsFormat"));
        ImGui::Spacing(); ImGui::Separator();

        if (ImGui::Button(_.t("prov.cancel"), ImVec2(90, 32))) ImGui::CloseCurrentPopup();
        ImGui::SameLine();
        const char* actionLabel = m_providerEditMode ? _.t("prov.save") : _.t("prov.addBtn");
        if (ImGui::Button(actionLabel, ImVec2(90, 32))) {
            std::string name(m_provNameBuf), key(m_provKeyBuf);
            std::string openaiUrl(m_provOpenaiUrlBuf), anthropicUrl(m_provAnthropicUrlBuf);

            std::vector<std::string> models;
            std::istringstream iss(m_provModelsBuf);
            std::string line;
            while (std::getline(iss, line)) {
                size_t s = line.find_first_not_of(" \t\r\n"), e = line.find_last_not_of(" \t\r\n");
                if (s != std::string::npos) models.push_back(line.substr(s, e - s + 1));
            }

            if (name.empty() || (openaiUrl.empty() && anthropicUrl.empty()) || models.empty()) {
                showToast(_.t("prov.fillAll"), true);
            } else if (!m_providerEditMode && key.empty()) {
                showToast(_.t("prov.fillKey"), true);
            } else {
                bool valid = true;
                for (char c : name) if (!((c>='a'&&c<='z')||(c>='0'&&c<='9')||c=='-')) { valid=false; break; }
                if (!valid) {
                    showToast(_.t("prov.invalidName"), true);
                } else {
                    Provider p; p.name=name; p.api_key=key;
                    p.openai_url=openaiUrl; p.anthropic_url=anthropicUrl;
                    p.models=models;
                    if (m_providerEditMode) {
                        if (key.empty()) p.api_key = "";
                        m_providers->updateProvider(m_editProviderId, p);
                        showToast(_.t("prov.updated"));
                    } else {
                        m_providers->addProvider(p);
                        showToast(_.t("prov.added"));
                    }
                    ImGui::CloseCurrentPopup();
                }
            }
        }
        ImGui::EndPopup();
    }

    // Delete Confirm
    if (m_showDeleteConfirm) { ImGui::OpenPopup(_.t("prov.confirmDelete")); m_showDeleteConfirm = false; }
    ImGui::SetNextWindowPos(center, ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
    if (ImGui::BeginPopupModal(_.t("prov.confirmDelete"), nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        char buf[256];
        snprintf(buf, sizeof(buf), _.t("prov.confirmMsg"), m_deleteProviderName.c_str());
        ImGui::Text("%s", buf);
        ImGui::TextColored(ImVec4(0.50f,0.52f,0.60f,1.0f), "%s", _.t("prov.confirmSub"));
        ImGui::Spacing();
        if (ImGui::Button(_.t("prov.cancel"), ImVec2(90, 32))) ImGui::CloseCurrentPopup();
        ImGui::SameLine();
        if (ImGui::Button(_.t("prov.confirmBtn"), ImVec2(110, 32))) {
            m_providers->deleteProvider(m_deleteProviderId);
            showToast(_.t("prov.deleted"));
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }
}

// ---- Models ----

void GuiApp::renderModels() {
    auto& _ = m_i18n;
    auto providers = m_providers->listAll();
    static char searchBuf[128] = {};

    // Search + Expand/Collapse buttons
    ImGui::InputTextWithHint("##search", _.t("models.search"), searchBuf, sizeof(searchBuf));
    ImGui::SameLine();
    if (ImGui::Button(_.t("models.expandAll"), ImVec2(80, 52))) {
        for (auto& p : providers) m_providerCollapsed[p.id] = false;
    }
    ImGui::SameLine();
    if (ImGui::Button(_.t("models.collapseAll"), ImVec2(80, 52))) {
        for (auto& p : providers) m_providerCollapsed[p.id] = true;
    }

    // Test All button
    ImGui::SameLine();
    if (m_speedBatchActive) {
        ImGui::BeginDisabled();
        char progBuf[64];
        snprintf(progBuf, sizeof(progBuf), _.t("models.testAllProgress"),
            m_speedCompleted.load(), m_speedTotal.load());
        ImGui::Button(progBuf, ImVec2(160, 52));
        ImGui::EndDisabled();
    } else {
        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.22f, 0.55f, 0.35f, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.28f, 0.68f, 0.42f, 1.0f));
        if (ImGui::Button(_.t("models.testAll"), ImVec2(160, 52))) {
            // Build test queue from all models
            m_speedQueue.clear();
            for (auto& pr : providers) {
                for (auto& md : pr.models) {
                    m_speedQueue.push_back(pr.name + "/" + md);
                }
            }
            m_speedTotal = static_cast<int>(m_speedQueue.size());
            m_speedCompleted = 0;
            m_speedBatchActive = true;
            if (m_speedThread.joinable()) m_speedThread.join();
            m_speedThread = std::thread(&GuiApp::speedTestRunner, this);
        }
        ImGui::PopStyleColor(2);
    }

    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();

    std::string search(searchBuf);
    bool anyVisible = false;

    ImGui::BeginChild("##modelsTree", ImVec2(0, 0), ImGuiChildFlags_Border);

    if (providers.empty()) {
        ImGui::SetCursorPosY(ImGui::GetCursorPosY() + 40);
        ImGui::TextColored(ImVec4(0.40f, 0.40f, 0.45f, 1.0f), "  %s", _.t("models.noModels"));
    }

    for (auto& p : providers) {
        // Filter models by search
        bool hasMatch = search.empty();
        if (!hasMatch) {
            for (auto& m : p.models) {
                std::string full = p.name + "/" + m;
                std::string lf = full; std::transform(lf.begin(), lf.end(), lf.begin(), ::tolower);
                std::string ls = search; std::transform(ls.begin(), ls.end(), ls.begin(), ::tolower);
                if (lf.find(ls) != std::string::npos) { hasMatch = true; break; }
            }
        }
        if (!hasMatch) continue;
        anyVisible = true;

        // Provider header
        bool defaultOpen = true;
        auto it = m_providerCollapsed.find(p.id);
        if (it != m_providerCollapsed.end()) defaultOpen = !it->second;

        ImGui::SetNextItemOpen(defaultOpen);
        bool nodeOpen = ImGui::TreeNodeEx(p.id.c_str(),
            ImGuiTreeNodeFlags_SpanAvailWidth | ImGuiTreeNodeFlags_DefaultOpen,
            "%s  (%zu models)", p.name.c_str(), p.models.size());
        m_providerCollapsed[p.id] = !nodeOpen;

        if (nodeOpen) {
            // Model table for this provider
            if (ImGui::BeginTable(("##mt_" + p.id).c_str(), 6,
                ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_Resizable)) {
                ImGui::PushStyleVar(ImGuiStyleVar_CellPadding, ImVec2(8, 10));
                ImGui::TableSetupColumn(_.t("models.colId"));
                ImGui::TableSetupColumn(_.t("models.colUpstream"));
                ImGui::TableSetupColumn(_.t("models.colUrls"));
                ImGui::TableSetupColumn("##speedBtn");
                ImGui::TableSetupColumn("##copyBtn");
                ImGui::TableSetupColumn("##speedResult");
                ImGui::TableHeadersRow();

                for (auto& m : p.models) {
                    std::string fullId = p.name + "/" + m;
                    // Skip if doesn't match search
                    if (!search.empty()) {
                        std::string lf = fullId;
                        std::transform(lf.begin(), lf.end(), lf.begin(), ::tolower);
                        std::string ls = search;
                        std::transform(ls.begin(), ls.end(), ls.begin(), ::tolower);
                        if (lf.find(ls) == std::string::npos) continue;
                    }

                    ImGui::TableNextRow();
                    ImGui::TableSetColumnIndex(0);
                    ImGui::TextColored(ImVec4(0.42f, 0.54f, 1.00f, 1.0f), "%s", fullId.c_str());
                    ImGui::TableSetColumnIndex(1);
                    ImGui::TextColored(ImVec4(0.50f, 0.52f, 0.60f, 1.0f), "%s", m.c_str());
                    ImGui::TableSetColumnIndex(2);
                    std::string urls;
                    if (!p.openai_url.empty()) urls += "O";
                    if (!p.anthropic_url.empty()) urls += urls.empty() ? "A" : "+A";
                    ImGui::TextColored(ImVec4(0.50f, 0.52f, 0.60f, 1.0f), "%s", urls.empty() ? "-" : urls.c_str());

                    // Speed test button
                    ImGui::TableSetColumnIndex(3);
                    bool isRunning = (m_speedRunning == fullId);
                    if (isRunning) {
                        ImGui::BeginDisabled();
                        ImGui::Button(_.t("models.speedTesting"), ImVec2(70, 44));
                        ImGui::EndDisabled();
                    } else {
                        if (ImGui::Button((std::string(_.t("models.speedTest")) + "##" + fullId).c_str(),
                            ImVec2(70, 44))) {
                            m_speedRunning = fullId;
                        }
                    }

                    // Copy Model ID button
                    ImGui::TableSetColumnIndex(4);
                    if (ImGui::Button((std::string(_.t("models.copyId")) + "##" + fullId).c_str(),
                        ImVec2(60, 44))) {
                        ImGui::SetClipboardText(fullId.c_str());
                        showToast(_.t("models.copied"));
                    }

                    // Speed test result
                    ImGui::TableSetColumnIndex(5);
                    auto rit = m_speedLatency.find(fullId);
                    if (rit != m_speedLatency.end()) {
                        if (rit->second < 0) {
                            ImGui::TextColored(ImVec4(0.97f, 0.44f, 0.44f, 1.0f), "%s", _.t("models.speedError"));
                        } else {
                            char buf[32];
                            snprintf(buf, sizeof(buf), _.t("models.speedResult"), rit->second);
                            ImVec4 col = (rit->second < 500) ? ImVec4(0.29f, 0.87f, 0.50f, 1.0f) :
                                (rit->second < 1500) ? ImVec4(1.0f, 0.75f, 0.30f, 1.0f) :
                                ImVec4(0.97f, 0.44f, 0.44f, 1.0f);
                            ImGui::TextColored(col, "%s", buf);
                        }
                    } else {
                        ImGui::TextColored(ImVec4(0.35f, 0.35f, 0.40f, 1.0f), "-");
                    }
                }
                ImGui::EndTable();
                ImGui::PopStyleVar();
            }
            ImGui::TreePop();
        }
    }

    if (!anyVisible && !providers.empty()) {
        ImGui::TextColored(ImVec4(0.40f, 0.40f, 0.45f, 1.0f), "%s",
            search.empty() ? _.t("models.noModels") : _.t("models.noMatch"));
    }

    ImGui::EndChild();
}

// ---- Batch Speed Test Runner ----

void GuiApp::speedTestRunner() {
    auto providers = m_providers->listAll();
    while (m_speedBatchActive && !m_speedQueue.empty()) {
        std::string fullId = m_speedQueue.back();
        m_speedQueue.pop_back();

        size_t slash = fullId.find('/');
        if (slash == std::string::npos) { m_speedCompleted++; continue; }
        std::string provName = fullId.substr(0, slash);

        for (auto& p : providers) {
            if (p.name == provName) {
                std::string testUrl;
                if (!p.openai_url.empty()) testUrl = p.openai_url;
                else if (!p.anthropic_url.empty()) testUrl = p.anthropic_url;

                if (!testUrl.empty()) {
                    std::string host = testUrl;
                    int port = 443;

                    if (host.find("https://") == 0) host = host.substr(8);
                    else if (host.find("http://") == 0) { host = host.substr(7); port = 80; }

                    size_t col = host.find(':');
                    size_t sl = host.find('/');
                    if (col != std::string::npos && (sl == std::string::npos || col < sl)) {
                        port = std::stoi(host.substr(col + 1));
                        host = host.substr(0, col);
                    } else if (sl != std::string::npos) {
                        host = host.substr(0, sl);
                    }

                    try {
                        auto start = std::chrono::steady_clock::now();
                        httplib::Client cli(host, port);
                        cli.set_connection_timeout(std::chrono::seconds(5));
                        cli.set_read_timeout(std::chrono::seconds(5));
                        auto res = cli.Head("/");
                        auto end = std::chrono::steady_clock::now();
                        float ms = std::chrono::duration<float, std::milli>(end - start).count();
                        m_speedLatency[fullId] = res ? ms : -1.0f;
                    } catch (...) {
                        m_speedLatency[fullId] = -1.0f;
                    }
                } else {
                    m_speedLatency[fullId] = -1.0f;
                }
                break;
            }
        }
        m_speedCompleted++;
    }
    m_speedBatchActive = false;
}

// ---- Settings (VS Code-style) ----

void GuiApp::renderSettings() {
    auto& _ = m_i18n;
    float availW = ImGui::GetContentRegionAvail().x;

    // Search bar
    ImGui::PushStyleColor(ImGuiCol_FrameBg,
        m_darkTheme ? ImVec4(0.10f, 0.11f, 0.16f, 1.0f) : ImVec4(0.85f, 0.86f, 0.90f, 1.0f));
    ImGui::InputTextWithHint("##settingsSearch", _.t("settings.searchPlaceholder"),
        m_settingsSearch, sizeof(m_settingsSearch));
    ImGui::PopStyleColor();
    ImGui::Spacing();

    // Layout: sidebar + content
    float sidebarW = 180.0f;
    float contentW = availW - sidebarW - 20;

    // ---- Sidebar ----
    ImGui::BeginChild("##settingsSidebar", ImVec2(sidebarW, 0), ImGuiChildFlags_Border);
    ImGui::Spacing();

    const char* categories[] = {
        _.t("settings.general"),
        _.t("settings.network"),
    };

    for (int i = 0; i < 2; i++) {
        bool selected = (m_settingsCategory == i);
        if (selected) {
            ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.26f, 0.46f, 0.96f, 0.30f));
            ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.26f, 0.46f, 0.96f, 0.40f));
        } else {
            ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0, 0, 0, 0));
            ImGui::PushStyleColor(ImGuiCol_ButtonHovered,
                m_darkTheme ? ImVec4(0.20f, 0.20f, 0.28f, 1.0f) : ImVec4(0.80f, 0.80f, 0.85f, 1.0f));
        }
        ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.26f, 0.46f, 0.96f, 0.20f));
        ImGui::PushStyleVar(ImGuiStyleVar_ButtonTextAlign, ImVec2(0.0f, 0.5f));

        if (ImGui::Button(categories[i], ImVec2(sidebarW - 20, 40))) {
            m_settingsCategory = i;
        }
        ImGui::PopStyleVar();
        ImGui::PopStyleColor(3);
    }

    ImGui::EndChild();
    ImGui::SameLine(16);

    // ---- Content Panel ----
    ImGui::BeginChild("##settingsContent", ImVec2(contentW, 0), ImGuiChildFlags_Border);
    ImGui::Spacing();

    std::string search(m_settingsSearch);
    auto matchesSearch = [&](const char* key) -> bool {
        if (search.empty()) return true;
        std::string label = _.t(key);
        std::string ls = search;
        std::transform(label.begin(), label.end(), label.begin(), ::tolower);
        std::transform(ls.begin(), ls.end(), ls.begin(), ::tolower);
        return label.find(ls) != std::string::npos;
    };

    auto settingRow = [&](const char* labelKey, const char* descKey, bool visible = true) -> bool {
        if (!visible) return false;
        const char* desc = descKey ? _.t(descKey) : "";
        ImGui::TextColored(ImVec4(0.55f, 0.57f, 0.65f, 1.0f), "%s", desc);
        ImGui::SameLine();
        return true;
    };

    // Helper: perform save
    auto doSave = [&]() {
        m_config->setPort(m_portBuf);
        m_config->setDefaultTimeoutSec(m_timeoutBuf);
        m_config->setLogRequests(m_logRequests);
        m_config->setLanguage(I18n::toString(m_i18n.getLang()));
        m_config->setTheme(m_darkTheme ? "dark" : "light");
        m_config->setLocalApiKey(m_localApiKeyBuf);
        m_config->save(m_config->getConfigPath());
        showToast(_.t("settings.saved"));
    };

    // ---- General Settings ----
    if (m_settingsCategory == 0) {
        if (matchesSearch("settings.port") || matchesSearch("settings.portDesc") ||
            matchesSearch("settings.portHint")) {
            settingRow("settings.port", "settings.portDesc");
            ImGui::SetNextItemWidth(160);
            ImGui::InputInt(("##port##" + std::string(_.t("settings.port"))).c_str(), &m_portBuf);
            if (m_portBuf < 1) m_portBuf = 1; if (m_portBuf > 65535) m_portBuf = 65535;
            ImGui::TextColored(ImVec4(0.50f, 0.50f, 0.55f, 1.0f), "%s", _.t("settings.portHint"));
            ImGui::Spacing(); ImGui::Separator(); ImGui::Spacing();
        }

        if (matchesSearch("settings.timeout") || matchesSearch("settings.timeoutDesc")) {
            settingRow("settings.timeout", "settings.timeoutDesc");
            ImGui::SetNextItemWidth(160);
            ImGui::InputInt(("##timeout##" + std::string(_.t("settings.timeout"))).c_str(), &m_timeoutBuf);
            if (m_timeoutBuf < 5) m_timeoutBuf = 5; if (m_timeoutBuf > 600) m_timeoutBuf = 600;
            ImGui::Spacing(); ImGui::Separator(); ImGui::Spacing();
        }

        if (matchesSearch("settings.logReqs") || matchesSearch("settings.logReqsDesc")) {
            settingRow("settings.logReqs", "settings.logReqsDesc");
            ImGui::Checkbox(("##logReqs##" + std::string(_.t("settings.logReqs"))).c_str(), &m_logRequests);
            ImGui::Spacing(); ImGui::Separator(); ImGui::Spacing();
        }

        // Local API URL
        if (matchesSearch("settings.localUrl") || matchesSearch("settings.localUrlDesc")) {
            ImGui::Spacing();
            ImGui::TextColored(ImVec4(0.70f, 0.72f, 0.80f, 1.0f), "%s", _.t("settings.localUrl"));
            ImGui::Spacing();
            ImGui::TextColored(ImVec4(0.55f, 0.57f, 0.65f, 1.0f), "%s", _.t("settings.localUrlDesc"));
            ImGui::Spacing();

            char localUrl[128];
            snprintf(localUrl, sizeof(localUrl), "http://localhost:%d", m_portBuf);
            ImGui::SetNextItemWidth(300);
            ImGui::InputText("##localUrl", localUrl, strlen(localUrl), ImGuiInputTextFlags_ReadOnly);

            ImGui::SameLine();
            if (ImGui::Button(_.t("settings.copyUrl"), ImVec2(90, 28))) {
                ImGui::SetClipboardText(localUrl);
                showToast(_.t("settings.copied"));
            }
            ImGui::Spacing(); ImGui::Separator(); ImGui::Spacing();
        }

        // OpenAI-compatible URL
        if (matchesSearch("settings.openaiUrl") || matchesSearch("settings.openaiUrlDesc")) {
            ImGui::Spacing();
            ImGui::TextColored(ImVec4(0.70f, 0.72f, 0.80f, 1.0f), "%s", _.t("settings.openaiUrl"));
            ImGui::Spacing();
            ImGui::TextColored(ImVec4(0.55f, 0.57f, 0.65f, 1.0f), "%s", _.t("settings.openaiUrlDesc"));
            ImGui::Spacing();

            char openaiUrl[128];
            snprintf(openaiUrl, sizeof(openaiUrl), "http://localhost:%d/v1", m_portBuf);
            ImGui::SetNextItemWidth(300);
            ImGui::InputText("##openaiUrl", openaiUrl, strlen(openaiUrl), ImGuiInputTextFlags_ReadOnly);

            ImGui::SameLine();
            if (ImGui::Button((_.t("settings.copyUrl") + std::string("##openai")).c_str(), ImVec2(90, 28))) {
                ImGui::SetClipboardText(openaiUrl);
                showToast(_.t("settings.copied"));
            }
            ImGui::Spacing(); ImGui::Separator(); ImGui::Spacing();
        }

        // Anthropic-compatible URL
        if (matchesSearch("settings.anthropicUrl") || matchesSearch("settings.anthropicUrlDesc")) {
            ImGui::Spacing();
            ImGui::TextColored(ImVec4(0.70f, 0.72f, 0.80f, 1.0f), "%s", _.t("settings.anthropicUrl"));
            ImGui::Spacing();
            ImGui::TextColored(ImVec4(0.55f, 0.57f, 0.65f, 1.0f), "%s", _.t("settings.anthropicUrlDesc"));
            ImGui::Spacing();

            char anthroUrl[128];
            snprintf(anthroUrl, sizeof(anthroUrl), "http://localhost:%d", m_portBuf);
            ImGui::SetNextItemWidth(300);
            ImGui::InputText("##anthroUrl", anthroUrl, strlen(anthroUrl), ImGuiInputTextFlags_ReadOnly);

            ImGui::SameLine();
            if (ImGui::Button((_.t("settings.copyUrl") + std::string("##anthro")).c_str(), ImVec2(90, 28))) {
                ImGui::SetClipboardText(anthroUrl);
                showToast(_.t("settings.copied"));
            }
            ImGui::Spacing(); ImGui::Separator(); ImGui::Spacing();
        }

        // Local API Key
        if (matchesSearch("settings.localApiKeyTitle") || matchesSearch("settings.localApiKeyLabel") ||
            matchesSearch("settings.localApiKeyDesc")) {
            ImGui::Spacing();
            ImGui::TextColored(ImVec4(0.70f, 0.72f, 0.80f, 1.0f), "%s", _.t("settings.localApiKeyTitle"));
            ImGui::Spacing();
            ImGui::TextColored(ImVec4(0.55f, 0.57f, 0.65f, 1.0f), "%s", _.t("settings.localApiKeyDesc"));
            ImGui::Spacing();

            ImGui::SetNextItemWidth(380);
            ImGui::InputText(("##localApiKey##" + std::string(_.t("settings.localApiKeyLabel"))).c_str(),
                m_localApiKeyBuf, sizeof(m_localApiKeyBuf));

            ImGui::SameLine();
            if (ImGui::Button(_.t("settings.generateKey"), ImVec2(90, 28))) {
                std::string rkey = ApiRouter::generateRandomKey(32);
                snprintf(m_localApiKeyBuf, sizeof(m_localApiKeyBuf), "%s", rkey.c_str());
            }
            ImGui::Spacing(); ImGui::Separator(); ImGui::Spacing();
        }

        // Save buttons
        ImGui::Spacing();
        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.26f, 0.46f, 0.96f, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.32f, 0.54f, 1.00f, 1.0f));
        if (ImGui::Button(_.t("settings.saveBtn"), ImVec2(120, 38))) {
            doSave();
        }
        ImGui::PopStyleColor(2);
        ImGui::SameLine();
        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.22f, 0.55f, 0.35f, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.28f, 0.68f, 0.42f, 1.0f));
        if (ImGui::Button(_.t("settings.saveRestartBtn"), ImVec2(150, 38))) {
            doSave();
            showToast(_.t("settings.savedRestart"));
            m_runResult = 1;
            m_running = false;
        }
        ImGui::PopStyleColor(2);
    }

    // ---- Network Settings ----
    if (m_settingsCategory == 1) {
        if (matchesSearch("settings.port") || matchesSearch("settings.portDesc") ||
            matchesSearch("settings.portHint")) {
            settingRow("settings.port", "settings.portDesc");
            ImGui::SetNextItemWidth(160);
            ImGui::InputInt(("##port_net##" + std::string(_.t("settings.port"))).c_str(), &m_portBuf);
            if (m_portBuf < 1) m_portBuf = 1; if (m_portBuf > 65535) m_portBuf = 65535;
            ImGui::TextColored(ImVec4(0.50f, 0.50f, 0.55f, 1.0f), "%s", _.t("settings.portHint"));
            ImGui::Spacing(); ImGui::Separator(); ImGui::Spacing();
        }

        if (matchesSearch("settings.timeout") || matchesSearch("settings.timeoutDesc")) {
            settingRow("settings.timeout", "settings.timeoutDesc");
            ImGui::SetNextItemWidth(160);
            ImGui::InputInt(("##timeout_net##" + std::string(_.t("settings.timeout"))).c_str(), &m_timeoutBuf);
            if (m_timeoutBuf < 5) m_timeoutBuf = 5; if (m_timeoutBuf > 600) m_timeoutBuf = 600;
            ImGui::Spacing(); ImGui::Separator(); ImGui::Spacing();
        }

        // Save buttons
        ImGui::Spacing();
        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.26f, 0.46f, 0.96f, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.32f, 0.54f, 1.00f, 1.0f));
        if (ImGui::Button(_.t("settings.saveBtn"), ImVec2(120, 38))) {
            doSave();
        }
        ImGui::PopStyleColor(2);
        ImGui::SameLine();
        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.22f, 0.55f, 0.35f, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.28f, 0.68f, 0.42f, 1.0f));
        if (ImGui::Button(_.t("settings.saveRestartBtn"), ImVec2(150, 38))) {
            doSave();
            showToast(_.t("settings.savedRestart"));
            m_runResult = 1;
            m_running = false;
        }
        ImGui::PopStyleColor(2);
    }

    ImGui::EndChild();
}

// ---- Header Bar ----

void GuiApp::renderHeader() {
    auto& _ = m_i18n;
    float barH = 40.0f;
    float availW = ImGui::GetContentRegionAvail().x;

    ImDrawList* dl = ImGui::GetWindowDrawList();
    ImVec2 mainPos = ImGui::GetCursorScreenPos();
    ImVec2 p0 = mainPos;
    ImVec2 p1(p0.x + availW, p0.y + barH);

    // Background — matches WindowBg exactly for seamless blend
    ImU32 headerBg = m_darkTheme ? IM_COL32(15, 17, 26, 255) : IM_COL32(245, 245, 250, 255);
    dl->AddRectFilled(p0, p1, headerBg);

    // Bottom separator — very faint
    dl->AddLine(ImVec2(p0.x, p1.y), ImVec2(p1.x, p1.y),
        m_darkTheme ? IM_COL32(35, 38, 55, 180) : IM_COL32(210, 212, 220, 180));

    // --- Left: Brand + Version ---
    if (m_largeFont) ImGui::PushFont(m_largeFont);
    ImGui::SetCursorScreenPos(ImVec2(p0.x + 16, p0.y + 4));
    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.26f, 0.46f, 0.96f, 1.0f));
    ImGui::Text("LMGate");
    ImGui::PopStyleColor();
    float brandW = ImGui::CalcTextSize("LMGate").x;
    if (m_largeFont) ImGui::PopFont();

    // Version (small, after LMGate)
    ImGui::SetCursorScreenPos(ImVec2(p0.x + 16 + brandW + 8, p0.y + 14));
    ImGui::PushStyleColor(ImGuiCol_Text,
        m_darkTheme ? ImVec4(0.45f, 0.47f, 0.55f, 1.0f) : ImVec4(0.55f, 0.55f, 0.60f, 1.0f));
    ImGui::Text("v1.1.0");
    ImGui::PopStyleColor();

    // --- Right side: Info + Clock, vertically stacked, right-aligned ---
    auto providers = m_providers->listAll();
    int totalModels = 0;
    for (auto& p : providers) totalModels += static_cast<int>(p.models.size());

    auto now = std::chrono::system_clock::now();
    std::time_t t = std::chrono::system_clock::to_time_t(now);
    std::tm* tm = std::localtime(&t);
    char timeBuf[32];
    snprintf(timeBuf, sizeof(timeBuf), "%04d-%02d-%02d %02d:%02d:%02d",
        tm->tm_year + 1900, tm->tm_mon + 1, tm->tm_mday,
        tm->tm_hour, tm->tm_min, tm->tm_sec);

    char info[128];
    snprintf(info, sizeof(info), _.t("menu.info"),
        static_cast<int>(providers.size()), totalModels, m_config->getPort());

    // Window control button area (rightmost)
    float btnW = 36.0f;
    float btnH = barH - 4;
    float btnAreaW = btnW * 3 + 4;
    float btnStartX = p1.x - btnAreaW;
    float btnY = p0.y + 2;

    // Info text (top line, right-aligned above clock)
    float infoW = ImGui::CalcTextSize(info).x;
    ImGui::SetCursorScreenPos(ImVec2(btnStartX - infoW - 16, p0.y + 2));
    ImGui::PushStyleColor(ImGuiCol_Text,
        m_darkTheme ? ImVec4(0.55f, 0.57f, 0.65f, 1.0f) : ImVec4(0.50f, 0.50f, 0.55f, 1.0f));
    ImGui::Text("%s", info);
    ImGui::PopStyleColor();

    // Clock text (bottom line, right-aligned)
    float timeW = ImGui::CalcTextSize(timeBuf).x;
    ImGui::SetCursorScreenPos(ImVec2(btnStartX - timeW - 16, p0.y + 22));
    ImGui::PushStyleColor(ImGuiCol_Text,
        m_darkTheme ? ImVec4(0.72f, 0.74f, 0.82f, 1.0f) : ImVec4(0.40f, 0.40f, 0.45f, 1.0f));
    ImGui::Text("%s", timeBuf);
    ImGui::PopStyleColor();

    // --- Window control buttons with PNG icons ---
    struct WinBtn { GLuint icon; bool isClose; int action; }; // action: 0=min, 1=max/restore, 2=close
    WINDOWPLACEMENT wp = { sizeof(wp) };
    GetWindowPlacement(m_hwnd, &wp);
    bool isMaximized = (wp.showCmd == SW_MAXIMIZE);

    WinBtn btns[] = {
        { m_winMinIcon, false, 0 },
        { isMaximized ? m_winRestoreIcon : m_winMaxIcon, false, 1 },
        { m_winCloseIcon, true, 2 },
    };

    for (int bi = 0; bi < 3; bi++) {
        float bx = btnStartX + bi * btnW;
        ImVec2 btnMin(bx, btnY);
        ImVec2 btnMax(bx + btnW, btnY + btnH);
        bool hovered = ImGui::IsMouseHoveringRect(btnMin, btnMax);

        // Hover background
        if (hovered) {
            ImU32 btnBg = btns[bi].isClose ? IM_COL32(0xE8, 0x11, 0x23, 255)
                                           : (m_darkTheme ? IM_COL32(60, 64, 80, 255) : IM_COL32(210, 212, 220, 255));
            ImDrawFlags corners = btns[bi].isClose ? ImDrawFlags_RoundCornersTopRight : 0;
            dl->AddRectFilled(btnMin, btnMax, btnBg, 4.0f, corners);
        }

        // Icon
        if (btns[bi].icon) {
            float iconPad = 8.0f;
            ImVec2 iconMin(bx + iconPad, btnY + iconPad);
            ImVec2 iconMax(bx + btnW - iconPad, btnY + btnH - iconPad);
            ImU32 iconCol = (hovered && btns[bi].isClose) ? IM_COL32(255, 255, 255, 255)
                            : (m_darkTheme ? IM_COL32(200, 202, 215, 255) : IM_COL32(100, 102, 115, 255));
            dl->AddImage((ImTextureID)(uintptr_t)btns[bi].icon, iconMin, iconMax,
                ImVec2(0, 0), ImVec2(1, 1), iconCol);
        }

        // Click handling
        if (hovered && ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
            switch (btns[bi].action) {
            case 0: // Minimize
                ShowWindow(m_hwnd, SW_MINIMIZE);
                break;
            case 1: // Maximize/Restore
                if (isMaximized)
                    ShowWindow(m_hwnd, SW_RESTORE);
                else
                    ShowWindow(m_hwnd, SW_MAXIMIZE);
                break;
            case 2: // Close (minimize to tray)
                m_minimizedToTray = true;
                ShowWindow(m_hwnd, SW_HIDE);
                break;
            }
        }
    }

    // Make header draggable (only non-button area)
    if (ImGui::IsMouseHoveringRect(p0, ImVec2(btnStartX, p1.y)) &&
        ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
        ReleaseCapture();
        SendMessageW(m_hwnd, WM_NCLBUTTONDOWN, HTCAPTION, 0);
    }
    // Double-click header to toggle maximize
    if (ImGui::IsMouseHoveringRect(p0, ImVec2(btnStartX, p1.y)) &&
        ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) {
        if (isMaximized)
            ShowWindow(m_hwnd, SW_RESTORE);
        else
            ShowWindow(m_hwnd, SW_MAXIMIZE);
    }

    ImGui::SetCursorScreenPos(ImVec2(p0.x, p0.y + barH));
}

// ---- Sidebar ----

void GuiApp::renderSidebar() {
    auto& _ = m_i18n;
    float sidebarW = 200.0f;

    ImDrawList* dl = ImGui::GetWindowDrawList();
    ImVec2 sidebarOrigin = ImGui::GetCursorScreenPos();

    struct MenuItem {
        int index;
        const char* key;
        int action;  // 0=page, 1=toggle_theme, 2=toggle_lang
    };

    MenuItem items[] = {
        {0, "tab.dashboard", 0},
        {1, "tab.providers", 0},
        {2, "tab.models", 0},
        {3, "tab.language", 2},
        {4, "tab.theme", 1},
        {5, "tab.settings", 0},
    };

    ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 10.0f);
    ImGui::SetCursorPos(ImVec2(0, 8));

    for (int i = 0; i < 6; i++) {
        auto& item = items[i];
        bool isActive = (m_activeTab == item.index);

        int themeIdx = m_darkTheme ? 1 : 0;
        GLuint iconTex = m_sidebarIcons[item.index][themeIdx];

        ImU32 textColor;
        if (isActive) {
            textColor = IM_COL32(255, 255, 255, 255);
        } else {
            textColor = m_darkTheme ? IM_COL32(180, 182, 195, 255) : IM_COL32(70, 72, 85, 255);
        }

        ImVec2 btnPos = ImGui::GetCursorScreenPos();
        float btnW = sidebarW - 24;
        float btnH = 44.0f;
        float btnStartX = sidebarOrigin.x + 10;

        // Background highlight
        if (isActive) {
            ImU32 activeBg = m_darkTheme ? IM_COL32(0x6B, 0x8A, 0xFF, 255) : IM_COL32(0x1E, 0x90, 0xFF, 255);
            dl->AddRectFilled(ImVec2(btnStartX, btnPos.y),
                ImVec2(btnStartX + btnW, btnPos.y + btnH),
                activeBg, 8.0f);
        } else if (ImGui::IsMouseHoveringRect(
            ImVec2(sidebarOrigin.x, btnPos.y),
            ImVec2(sidebarOrigin.x + sidebarW, btnPos.y + btnH))) {
            ImU32 hoverBg = m_darkTheme ? IM_COL32(50, 55, 75, 100) : IM_COL32(220, 225, 235, 180);
            dl->AddRectFilled(ImVec2(btnStartX, btnPos.y),
                ImVec2(btnStartX + btnW, btnPos.y + btnH),
                hoverBg, 8.0f);
        }

        // Icon
        float iconSize = 22.0f;
        float iconX = btnStartX + 14;
        float iconY = btnPos.y + (btnH - iconSize) * 0.5f;
        if (iconTex) {
            dl->AddImage((ImTextureID)(uintptr_t)iconTex,
                ImVec2(iconX, iconY), ImVec2(iconX + iconSize, iconY + iconSize),
                ImVec2(0, 0), ImVec2(1, 1),
                isActive ? IM_COL32(255, 255, 255, 255) : IM_COL32(255, 255, 255, 200));
        } else {
            dl->AddCircleFilled(ImVec2(iconX + iconSize/2, iconY + iconSize/2),
                8.0f, isActive ? IM_COL32(0x6B, 0x8A, 0xFF, 255) : IM_COL32(150, 152, 160, 200));
        }

        // Text
        const char* label = _.t(item.key);
        dl->AddText(ImVec2(iconX + iconSize + 14, btnPos.y + (btnH - ImGui::GetTextLineHeight()) * 0.5f),
            textColor, label);

        // Invisible button for click handling
        ImGui::SetCursorScreenPos(btnPos);
        if (ImGui::InvisibleButton(("##sidebarBtn" + std::to_string(i)).c_str(),
            ImVec2(sidebarW - 10, btnH))) {
            if (item.action == 1) {
                m_darkTheme = !m_darkTheme;
                applyTheme();
            } else if (item.action == 2) {
                Lang newLang = (m_i18n.getLang() == Lang::ZH) ? Lang::EN : Lang::ZH;
                m_i18n.setLang(newLang);
                m_langIdx = (newLang == Lang::EN) ? 1 : 0;
            } else {
                m_activeTab = item.index;
            }
        }

        ImGui::SetCursorScreenPos(ImVec2(btnPos.x, btnPos.y + btnH + 2));
    }

    ImGui::PopStyleVar();
}

// ---- Main Loop ----

int GuiApp::run() {
    if (!m_running) return 0;

    ShowWindow(m_hwnd, SW_SHOW);
    UpdateWindow(m_hwnd);
    HDC hdc = GetDC(m_hwnd);

    auto& _ = m_i18n;

    MSG msg = {};
    while (m_running) {
        while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE)) {
            if (msg.message == WM_QUIT) {
                m_running = false;
                break;
            }
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }
        if (!m_running) break;

        // Handle speed test completion (single, not during batch)
        if (!m_speedRunning.empty() && !m_speedBatchActive) {
            // Parse provider/model from full ID
            size_t slash = m_speedRunning.find('/');
            if (slash != std::string::npos) {
                std::string provName = m_speedRunning.substr(0, slash);
                std::string modelName = m_speedRunning.substr(slash + 1);

                auto providers = m_providers->listAll();
                for (auto& p : providers) {
                    if (p.name == provName) {
                        // Determine URL to test
                        std::string testUrl;
                        if (!p.openai_url.empty()) testUrl = p.openai_url;
                        else if (!p.anthropic_url.empty()) testUrl = p.anthropic_url;

                        if (!testUrl.empty()) {
                            std::string host = testUrl;
                            int port = 443;

                            if (host.find("https://") == 0) host = host.substr(8);
                            else if (host.find("http://") == 0) { host = host.substr(7); port = 80; }

                            size_t col = host.find(':');
                            size_t sl = host.find('/');
                            if (col != std::string::npos && (sl == std::string::npos || col < sl)) {
                                port = std::stoi(host.substr(col + 1));
                                host = host.substr(0, col);
                            } else if (sl != std::string::npos) {
                                host = host.substr(0, sl);
                            }

                            auto start = std::chrono::steady_clock::now();
                            httplib::Client cli(host, port);
                            cli.set_connection_timeout(5);
                            cli.set_read_timeout(5);

                            auto res = cli.Head("/");
                            auto end = std::chrono::steady_clock::now();
                            float ms = std::chrono::duration<float, std::milli>(end - start).count();

                            m_speedLatency[m_speedRunning] = res ? ms : -1.0f;
                        } else {
                            m_speedLatency[m_speedRunning] = -1.0f;
                        }
                        break;
                    }
                }
            }
            m_speedRunning.clear();
        }

        // Skip rendering if minimized to tray
        if (m_minimizedToTray) {
            Sleep(50);
            continue;
        }

        ImGui_ImplOpenGL3_NewFrame();
        ImGui_ImplWin32_NewFrame();
        ImGui::NewFrame();

        RECT rect;
        GetClientRect(m_hwnd, &rect);
        m_windowWidth = rect.right;
        m_windowHeight = rect.bottom;

        ImGui::SetNextWindowPos(ImVec2(0, 0));
        ImGui::SetNextWindowSize(ImVec2(static_cast<float>(m_windowWidth),
            static_cast<float>(m_windowHeight)));

        ImGuiWindowFlags winFlags = ImGuiWindowFlags_NoTitleBar |
            ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoCollapse |
            ImGuiWindowFlags_NoBringToFrontOnFocus;

        ImGui::Begin("MainWindow", nullptr, winFlags);

        // Top header bar
        renderHeader();

        // Sidebar + Content layout
        ImGui::BeginChild("##sidebar", ImVec2(200, 0), ImGuiChildFlags_None);
        renderSidebar();
        ImGui::EndChild();

        ImGui::SameLine(0, 0);
        ImGui::BeginChild("##content", ImVec2(0, 0), ImGuiChildFlags_None);

        // Padding inside content
        ImGui::SetCursorPos(ImVec2(16, 16));

        switch (m_activeTab) {
        case 0: renderDashboard(); break;
        case 1: renderProviders(); break;
        case 2: renderModels();    break;
        case 5: renderSettings();  break;
        default: renderDashboard(); break;
        }
        ImGui::EndChild();

        ImGui::End();

        // Toast
        if (m_toastTimer > 0) {
            m_toastTimer -= ImGui::GetIO().DeltaTime;
            float alpha = std::min(1.0f, m_toastTimer / 0.5f);
            ImGui::SetNextWindowPos(ImVec2(m_windowWidth - 340, m_windowHeight - 60), ImGuiCond_Always);
            ImGui::SetNextWindowBgAlpha(0.92f * alpha);
            ImGui::Begin("##toast", nullptr,
                ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
                ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoFocusOnAppearing |
                ImGuiWindowFlags_AlwaysAutoResize);
            ImGui::PushStyleColor(ImGuiCol_Text,
                m_toastError ? ImVec4(0.97f,0.44f,0.44f,alpha) : ImVec4(0.29f,0.87f,0.50f,alpha));
            ImGui::Text("%s", m_toastMsg);
            ImGui::PopStyleColor();
            ImGui::End();
        }

        ImGui::Render();
        ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
        SwapBuffers(hdc);
    }

    ReleaseDC(m_hwnd, hdc);
    return m_runResult;
}
