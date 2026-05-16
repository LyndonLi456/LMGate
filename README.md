# LMGate

A cross-platform LLM API Gateway with a native desktop GUI, built in C++17.

LMGate acts as a **local proxy** that unifies multiple LLM provider APIs (OpenAI-compatible and Anthropic-compatible) behind a single local endpoint. It features a real-time dashboard, provider management, model speed testing, and usage statistics — all accessible through a native GUI and a web interface.

## Features

- **Multi-Provider Gateway** — Manage multiple LLM API providers (SiliconFlow, DeepSeek, OpenAI, etc.) and route requests by API key
- **Format Translation** — Automatic conversion between OpenAI Chat Completions and Anthropic Messages formats
- **Native Desktop GUI** — Dashboard with real-time stats, provider CRUD, model speed testing, settings management
- **Web Dashboard** — Built-in web UI for browser-based access
- **Usage Statistics** — SQLite-based persistent tracking of token usage, request counts, and per-model metrics
- **Cross-Platform** — Windows (MSVC/MinGW), Linux, macOS support
- **System Tray** — Minimize to tray, background operation

## Quick Start

### Download Pre-built Binary (Windows)

1. Download `llm-gateway-portable.zip` from the [latest release](https://github.com/yourusername/LMGate/releases/latest)
2. Extract and run `llm-gateway.exe`
3. Access the GUI at the system tray, or open `http://localhost:10099` in your browser

### Build from Source

See [DEVELOPMENT.md](DEVELOPMENT.md) for detailed build instructions across all platforms.

```bash
# Linux quick build
sudo apt install build-essential cmake libssl-dev zlib1g-dev libglfw3-dev libgl1-mesa-dev
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j$(nproc)

# Windows (MSVC with vcpkg)
cmake -B build -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_TOOLCHAIN_FILE=C:/vcpkg/scripts/buildsystems/vcpkg.cmake
cmake --build build --config Release
```

## Usage

1. Launch LMGate — the GUI opens with a sidebar layout
2. Add your LLM API providers in the **Providers** tab (API key + endpoint URL)
3. Configure tools (Cursor, VS Code, etc.) to use `http://localhost:10099/v1` as their API endpoint
4. Set a `local_api_key` in **Settings** and use the same key in your client tools
5. Monitor usage on the **Dashboard** in real-time

## Vibe Coding / AI-Assisted Development

This project includes [DEVELOPMENT.md](DEVELOPMENT.md) — a comprehensive guide designed for AI coding agents (Claude Code, Cursor, Copilot, etc.) to independently understand, build, and extend LMGate on any platform. Give it to your AI tool of choice and ask it to:

- Build the project from source
- Port the GUI to Linux
- Add new features or providers
- Modify the dashboard

[DEVELOPMENT.md](DEVELOPMENT.md) covers: architecture, dependency map, build instructions per platform, source code walkthrough, GUI layout, theme system, configuration schema, and a Linux porting guide.

## License

MIT
