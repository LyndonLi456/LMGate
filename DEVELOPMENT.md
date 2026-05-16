# LMGate Development Guide for AI Coding Agents

This document provides all the information an AI coding agent (Claude Code, Cursor, Copilot, etc.) needs to independently understand, build, modify, and extend the LMGate project on any platform.

## Project Overview

**LMGate** is a **cross-platform LLM API Gateway** with a native GUI built in C++17. It acts as a local proxy that unifies multiple LLM provider APIs (OpenAI-compatible and Anthropic-compatible) behind a single local endpoint.

### Core Architecture (Data Flow)

```
Client Apps (Cursor, CLI, etc.)
        │
        ▼
┌───────────────────────────────┐
│  HTTP API Router              │  ← httplib::Server (header-only HTTP library)
│  /v1/chat/completions (OpenAI)│
│  /v1/messages (Anthropic)     │
│  /health                      │
└──────────┬────────────────────┘
           │
           ▼
┌───────────────────────────────┐
│  Request Forwarder            │  ← Routes requests to correct provider
│  - API key matching           │
│  - Format transformation      │
│  - Token counting             │
│  - Stats recording (SQLite)   │
└──────────┬────────────────────┘
           │
           ▼
┌───────────────────────────────┐
│  Provider Manager             │  ← Manages provider configurations
│  - Multiple API providers     │
│  - Model lists per provider   │
└──────────┬────────────────────┘
           │
           ▼
┌───────────────────────────────┐
│  External LLM APIs            │
│  (SiliconFlow, DeepSeek, etc.)│
└───────────────────────────────┘

┌───────────────────────────────┐
│  GUI App (Dear ImGui + OpenGL)│  ← Native desktop GUI
│  - Dashboard with stats       │
│  - Provider CRUD management   │
│  - Model listing & speed test │
│  - Settings (port, timeout)   │
│  - System tray minimize       │
└───────────────────────────────┘
```

### Key Design Decisions

| Decision | Rationale |
|----------|-----------|
| C++17 | Modern enough for clean code, widely supported |
| Dear ImGui (immediate mode) | Zero-dependency GUI, easy to modify |
| SQLite (embedded) | No database server needed, stats stored locally |
| httplib.h (header-only) | No HTTP library build dependencies |
| Single binary | Everything compiled into one executable |
| PNG icons via custom loader | No image library dependency (uses zlib only) |

## Repository Structure

```
LMGate/
├── CMakeLists.txt              # Cross-platform build configuration
├── DEVELOPMENT.md              # This file — AI agent guide
├── README.md                   # English user-facing readme
├── README_zh.md                # Chinese user-facing readme
├── .github/workflows/          # CI/CD (GitHub Actions)
│   └── release.yml
├── resources/                  # PNG icons & ICO files
│   ├── card_1.png ... card_4.png   # Dashboard card backgrounds
│   ├── logo.ico                    # Application icon
│   ├── sun.ico, moon.ico           # Theme toggle icons (legacy)
│   ├── 最小化.png 最大化.png       # Window control: minimize, maximize
│   ├── 恢复.png 关闭窗口.png        # Window control: restore, close
│   ├── 仪表盘light.png 仪表盘dark.png  # Sidebar: dashboard
│   ├── 供应商light.png 供应商dark.png  # Sidebar: providers
│   ├── 模型light.png 模型dark.png      # Sidebar: models
│   ├── 语言light.png 语言dark.png      # Sidebar: language
│   ├── 主题light.png 主题dark.png      # Sidebar: theme
│   ├── 设置loght.png 设置dark.png      # Sidebar: settings (note: typo in filename)
│   └── (sidebar icon naming: <name><theme>.png, e.g. 仪表盘light.png)
├── src/
│   ├── main.cpp                # Entry point, HTTP server setup
│   ├── gui_app.cpp/.h          # Main GUI (Dear ImGui, ~2000 lines)
│   ├── api_router.cpp/.h       # HTTP endpoint registration
│   ├── config_manager.cpp/.h   # JSON config read/write
│   ├── provider_manager.cpp/.h # Multi-provider CRUD operations
│   ├── request_forwarder.cpp/.h# HTTP forwarding with format conversion
│   ├── format_transformer.cpp/.h # OpenAI ↔ Anthropic format mapping
│   ├── token_counter.cpp/.h    # Token usage estimation
│   ├── stats_db.cpp/.h         # SQLite statistics storage
│   ├── png_loader.cpp/.h       # Minimal PNG decoder (8-bit RGB/RGBA only)
│   ├── i18n.h                  # Chinese/English translation map
│   └── webui/                  # Built-in web dashboard
│       ├── index.html
│       ├── app.js
│       └── style.css
└── deps/
    ├── httplib.h               # cpp-httplib (header-only HTTP server/client)
    ├── json.hpp                # nlohmann/json (header-only JSON parser)
    ├── sqlite3.c / sqlite3.h   # SQLite3 amalgamation
    └── imgui/                  # Dear ImGui v1.91.0
        ├── imgui.cpp/.h
        ├── imgui_draw.cpp
        ├── imgui_widgets.cpp
        ├── imgui_tables.cpp
        ├── imgui_internal.h
        ├── imgui_impl_win32.cpp/.h   # Windows backend
        ├── imgui_impl_opengl3.cpp/.h # OpenGL3 renderer
        ├── imgui_impl_glfw.cpp/.h    # Linux/macOS backend (add if needed)
        └── imstb_*.h                 # STB helper headers
```

## Dependency Map

### Required for ALL platforms
| Dependency | Version | Purpose | Source |
|-----------|---------|---------|--------|
| CMake | ≥ 3.16 | Build system | `apt install cmake` / `brew install cmake` / cmake.org |
| C++17 compiler | GCC ≥ 8 / Clang ≥ 7 / MSVC ≥ 2019 | Compilation | System package |
| OpenSSL | ≥ 1.1 | HTTPS requests to LLM APIs | `apt install libssl-dev` / vcpkg / brew |
| zlib | any | PNG decompression, HTTP compression | `apt install zlib1g-dev` / vcpkg / brew |
| OpenGL | any | GUI rendering | System (Linux: `libgl1-mesa-dev`) |

### Platform-specific GUI dependencies
| Platform | Library | Install Command |
|----------|---------|----------------|
| Windows | Win32 API (built-in) + OpenGL | Bundled with Windows SDK |
| Linux | GLFW3 + OpenGL | `sudo apt install libglfw3-dev libgl1-mesa-dev` |
| macOS | GLFW3 + OpenGL | `brew install glfw` |

### Bundled (no install needed)
| Library | File | Notes |
|---------|------|-------|
| cpp-httplib | `deps/httplib.h` | Header-only HTTP library |
| nlohmann/json | `deps/json.hpp` | Header-only JSON parser |
| SQLite3 | `deps/sqlite3.c/.h` | Compiled as static library |
| Dear ImGui | `deps/imgui/` | Compiled as static library |

## Build Instructions

### Windows (MSVC 2022 — Recommended)

```powershell
# Prerequisites: Install Visual Studio 2022 with "Desktop development with C++"
# Install vcpkg for dependency management:
git clone https://github.com/Microsoft/vcpkg.git C:\vcpkg
cd C:\vcpkg && .\bootstrap-vcpkg.bat
.\vcpkg install openssl:x64-windows-static zlib:x64-windows-static
.\vcpkg integrate install

# Build
cmake -B build -DCMAKE_BUILD_TYPE=Release `
    -DCMAKE_TOOLCHAIN_FILE=C:/vcpkg/scripts/buildsystems/vcpkg.cmake
cmake --build build --config Release

# Output: build\Release\llm-gateway.exe (with resources/ subdirectory and webui/ copied alongside)
```

### Windows (MinGW64)

```bash
# Prerequisites: Install MSYS2, then:
pacman -S mingw-w64-x86_64-cmake mingw-w64-x86_64-gcc \
    mingw-w64-x86_64-openssl mingw-w64-x86_64-zlib

# Build
cmake -B build -G "MinGW Makefiles" -DCMAKE_BUILD_TYPE=Release
mingw32-make -C build -j$(nproc)

# Output: build\llm-gateway.exe (with resources/ and webui/ copied alongside)
```

### Linux (Ubuntu/Debian)

```bash
# Install dependencies
sudo apt update
sudo apt install build-essential cmake libssl-dev zlib1g-dev \
    libglfw3-dev libgl1-mesa-dev

# Build (with GUI)
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j$(nproc)

# Build (fully static — for distribution)
cmake -B build -DCMAKE_BUILD_TYPE=Release -DSTATIC_BUILD=ON
cmake --build build -j$(nproc)

# Output: build/llm-gateway (with resources/ and webui/ copied alongside)
```

### macOS

```bash
# Install dependencies
brew install cmake openssl zlib glfw

# Build
cmake -B build -DCMAKE_BUILD_TYPE=Release \
    -DOPENSSL_ROOT_DIR=$(brew --prefix openssl)
cmake --build build -j$(sysctl -n hw.ncpu)

# Output: build/llm-gateway (with resources/ and webui/ copied alongside)
```

### Build Options

| Option | Default | Description |
|--------|---------|-------------|
| `STATIC_BUILD` | OFF | Statically link libgcc/libstdc++ (for portable binaries) |
| `BUILD_GUI` | ON | Build with native GUI; set OFF for headless/server-only mode |

```bash
# Headless build (no GUI dependencies needed)
cmake -B build -DBUILD_GUI=OFF -DCMAKE_BUILD_TYPE=Release
cmake --build build
```

## Source Code Guide

### File-by-File Walkthrough

#### `src/main.cpp` — Entry Point
- Parses command line: `llm-gateway [config_path]`
- Initializes all components (ConfigManager → StatsDatabase → ProviderManager → RequestForwarder → ApiRouter → GuiApp)
- Starts HTTP server on config's port in a background thread
- Runs GUI in main thread; supports restart-on-settings-change (returns exit code 1)
- Exe directory detection: derives config path from `argv[0]`

#### `src/gui_app.h/.cpp` — GUI Application (~2000 lines)
The most complex file. Key methods:
- `init()` — Creates Win32/GLFW window, OpenGL context, Dear ImGui context, loads fonts & textures
- `run()` — Main render loop; delegates to render methods
- `renderHeader()` — Top bar: brand "LMGate" + version + provider stats + clock + window controls
- `renderSidebar()` — Left sidebar (200px): 6 menu items with PNG icons. Items 0-2,5 switch content pages; items 3,4 toggle language/theme inline
- `renderDashboard()` — 4 gradient cards + recent requests table. Cards display: today's tokens, request count, avg token/request, top model
- `renderProviders()` — Provider CRUD table + modal dialog for add/edit
- `renderModels()` — Model tree grouped by provider, with search, expand/collapse, speed test
- `renderSettings()` — VS Code-style settings with sidebar categories (General, Network)
- `applyTheme()` — Sets ~35 ImGui color values for light/dark theme
- `WndProc` — Window message handler: resize, minimize-to-tray, caption hit test

**State Management:**
- `m_activeTab` (0-5): Current content page
- `m_darkTheme`: Current theme (default: false = light)
- `m_largeFont`: 28px font for card values and brand text
- `m_sidebarIcons[6][2]`: PNG textures for sidebar (6 items × 2 themes)
- `m_winMinIcon/MaxIcon/RestoreIcon/CloseIcon`: Window control button textures

**Platform-specific code:**
- Win32: `CreateWindowExW`, WGL context, `WM_NCHITTEST` for custom title bar
- For Linux port: Replace Win32 calls with GLFW equivalents (see Porting Guide below)

#### `src/api_router.cpp/.h` — HTTP Routing
- `setup()`: Registers all HTTP endpoints on the httplib::Server
- Endpoints: `/v1/chat/completions`, `/v1/messages`, `/health`, `/stats`
- CORS headers added to all OPTIONS requests
- Health endpoint returns version string

#### `src/config_manager.cpp/.h` — Configuration
- JSON-based config file (`config.json` next to the executable)
- Fields: port, timeout, language, theme, log_requests, local_api_key
- Provider configs stored as JSON array with: name, api_key, openai_url, anthropic_url, models[], id
- Uses nlohmann/json for parsing

#### `src/provider_manager.cpp/.h` — Provider CRUD
- In-memory provider list synced with ConfigManager
- Methods: `listAll()`, `addProvider()`, `updateProvider()`, `deleteProvider()`
- Generates UUID-like IDs for new providers

#### `src/request_forwarder.cpp/.h` — Request Proxy
- Receives incoming requests, selects provider by API key matching (or default)
- Transforms request format (OpenAI ↔ Anthropic) via FormatTransformer
- Forwards to upstream API using httplib::Client
- Records usage statistics to StatsDatabase
- Handles both streaming (SSE) and non-streaming responses

#### `src/format_transformer.cpp/.h` — Format Conversion
- Converts between OpenAI chat completions format and Anthropic messages format
- Handles: system messages, multi-turn conversations, tool calls
- Preserves temperature, max_tokens, stop sequences, top_p parameters

#### `src/token_counter.cpp/.h` — Token Estimation
- Estimates token count using tiktoken-compatible algorithm (cl100k_base)
- Used for usage tracking and statistics

#### `src/stats_db.cpp/.h` — Statistics Database
- SQLite-based persistent storage for request records
- Schema: token_usage(id, timestamp, provider_name, model_name, prompt_tokens, completion_tokens, total_tokens, request_duration_ms, source_format)
- Query methods: daily usage, by-model, by-provider, chart data (with granularity), recent requests
- All database writes are mutex-protected (thread-safe)

#### `src/png_loader.cpp/.h` — PNG Decoder
- Custom minimal PNG loader (no libpng dependency)
- Supports: 8-bit RGB (color type 2) and RGBA (color type 6)
- Handles PNG filter types 0-4 (None, Sub, Up, Average, Paeth)
- Uses zlib for IDAT decompression
- Windows: uses `MultiByteToWideChar(CP_UTF8, ...) + _wfopen` for UTF-8 filenames

#### `src/i18n.h` — Internationalization
- `std::unordered_map<string, pair<string,string>>` for zh/en translations
- `I18n` class with `t(key)` method
- ~170 translation keys covering all UI text
- Language switch is instant (no restart needed)

#### `src/webui/` — Web Dashboard
- Standalone HTML/JS/CSS web interface
- Communicates with the gateway's HTTP API
- Shows stats, provider management, settings

## Porting to Linux

The current codebase targets Windows with Win32 API for windowing. To port to Linux:

### Steps to Port

1. **Add GLFW dependency**: Add `deps/imgui/imgui_impl_glfw.cpp` and `imgui_impl_glfw.h`

2. **Replace Win32 window creation** in `gui_app.cpp:init()`:
   - Replace `CreateWindowExW` / `RegisterClassExW` with `glfwCreateWindow()`
   - Replace `createGLContext()` with GLFW's context creation
   - Replace `SwapBuffers(hdc)` with `glfwSwapBuffers()`

3. **Replace WndProc** in `gui_app.cpp`:
   - Remove `mainWndProc` function
   - Use GLFW callbacks: `glfwSetWindowSizeCallback`, `glfwSetMouseButtonCallback`, etc.

4. **Replace Win32-specific features**:
   - System tray: Use `libappindicator` or similar on Linux
   - `WM_NCHITTEST` / custom dragging: Use GLFW's built-in window dragging
   - `WM_GETMINMAXINFO` / min window size: Use `glfwSetWindowSizeLimits()`
   - Window control buttons: Reposition for native window decorations

5. **Replace `#include <windows.h>`** with GLFW/GL includes
   - Use `#include <GLFW/glfw3.h>` instead
   - Use `#include <GL/gl.h>` for OpenGL (same on all platforms)

6. **UTF-8 paths**: Linux uses UTF-8 natively — the `pngFopen()` wrapper can be simplified to just `fopen()`

7. **Threading**: Replace `#include <windows.h>` for `Sleep()` with `<thread>` and `std::this_thread::sleep_for()`

### Key Platform Differences

| Feature | Windows | Linux |
|---------|---------|-------|
| Windowing | Win32 API (`CreateWindowExW`) | GLFW (`glfwCreateWindow`) |
| OpenGL context | WGL (`wglCreateContext`) | GLFW (`glfwMakeContextCurrent`) |
| System tray | `Shell_NotifyIcon` | libappindicator / KStatusNotifier |
| Window chrome | Custom-drawn (WS_POPUP) | Native (glfwDefaultWindowHints) |
| Font paths | `C:\Windows\Fonts\msyh.ttc` | `/usr/share/fonts/` or fontconfig |
| File paths | Backslash `\` | Forward slash `/` |
| UTF-8 in fopen | Needs `_wfopen` wrapper | Native support |
| Socket library | `ws2_32` | Built-in (POSIX) |

## GUI Layout Reference

```
┌──────────┬────────────────────────────────────────────┐
│ Header   │  LMGate v1.1.0    3 providers | ...  14:30 │ 40px
├──────────┼────────────────────────────────────────────┤
│ Sidebar  │  Content Area                              │
│ 200px    │                                            │
│          │  ┌─────────┬─────────┬─────────┬─────────┐ │
│ 📊 仪表盘│  │ Card 1  │ Card 2  │ Card 3  │ Card 4  │ │
│ 📦 供应商│  │ Tokens  │ Reqs    │ Avg     │ Top     │ │
│ 📋 模型  │  └─────────┴─────────┴─────────┴─────────┘ │
│ 🌐 语言  │                                            │
│ 🎨 主题  │  ┌──────────────────────────────────────┐  │
│ ⚙  设置  │  │ Recent Requests Table               │  │
│          │  └──────────────────────────────────────┘  │
└──────────┴────────────────────────────────────────────┘
```

## Theme System

Two themes (light default, dark toggleable):
- `applyTheme()` in `gui_app.cpp` sets ~35 `ImGuiCol_` values
- Theme affects: window bg, child bg, buttons, tables, text, scrollbars
- Sidebar active state: `#1E90FF` (light) / `#6B8AFF` (dark)
- Card gradients: 4 different color schemes (blue-green, blue-purple, yellow-orange, red-pink)
- Sidebar icons: 2 sets (light/dark), loaded as OpenGL textures

## Configuration

`config.json` is stored next to the executable:
```json
{
  "port": 10099,
  "providers": [...],
  "settings": {
    "default_timeout_sec": 60,
    "language": "zh",
    "local_api_key": "...",
    "log_requests": true,
    "theme": "light"
  }
}
```

## Adding Features

### Adding a new sidebar menu item
1. Add icon PNG files to `resources/` (both light and dark variants)
2. In `loadSidebarIcons()`: add the item name to the `names[]` array
3. In `renderSidebar()`: add to `items[]` array with desired action
4. In `i18n.h`: add translation key for the label
5. If it's a new page: add a render method and a case in the `switch(m_activeTab)`

### Adding a new provider type
1. Add provider config in the GUI (no code changes needed for API format)
2. For custom API format: add transformation logic in `format_transformer.cpp`

### Adding a new dashboard card
1. Query data from `StatsDatabase` (add new query method if needed)
2. Add a new `drawCard()` call in `renderDashboard()` with desired gradient colors
3. Add translations in `i18n.h`

## Common Build Issues

### "OpenSSL not found" on Windows
```powershell
# Option A: vcpkg (recommended)
vcpkg install openssl:x64-windows-static zlib:x64-windows-static
# Then add -DCMAKE_TOOLCHAIN_FILE=C:/vcpkg/scripts/buildsystems/vcpkg.cmake to cmake

# Option B: Manual (e.g., from Anaconda)
cmake -B build -DCMAKE_PREFIX_PATH=C:/Users/$env:USERNAME/anaconda3/Library
```

### "GLFW not found" on Linux
```bash
sudo apt install libglfw3-dev libgl1-mesa-dev
```

### MSVC: UTF-8 Chinese characters cause compile errors
The `/utf-8` flag is automatically added for MSVC builds. If you still see C2001/C3688 errors, ensure your source files are saved as UTF-8 (not GBK).

### PNG resources not displaying
- Ensure all PNG files from `resources/` are copied to the `resources/` subdirectory alongside the executable
- Check that `gdi32` is linked (needed for `LoadImageW` ICO loading)
- On Windows non-UTF-8 systems, Chinese PNG filenames require the `_wfopen` workaround in `png_loader.cpp`

## Version History

- v1.0.0 — Initial release
- v1.0.5 — Tab-based navigation, dark theme
- v1.1.0 — Sidebar navigation, light theme default, enhanced dashboard, window controls with PNG icons
