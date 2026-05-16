# LMGate

跨平台 LLM API 网关，带原生桌面 GUI，使用 C++17 开发。

LMGate 作为一个**本地代理**，将多个 LLM 供应商 API（兼容 OpenAI 和 Anthropic 格式）统一到单个本地端点。提供实时仪表盘、供应商管理、模型测速和使用统计等功能，可通过原生 GUI 和 Web 界面访问。

## 功能特性

- **多供应商网关** — 管理多个 LLM API 供应商（SiliconFlow、DeepSeek、OpenAI 等），通过 API Key 路由请求
- **格式转换** — OpenAI Chat Completions 与 Anthropic Messages 格式自动互转
- **原生桌面 GUI** — 仪表盘实时统计、供应商增删改查、模型测速、设置管理
- **Web 控制台** — 内置 Web 界面，可通过浏览器访问
- **用量统计** — 基于 SQLite 持久化追踪 Token 用量、请求数、各模型指标
- **跨平台** — 支持 Windows (MSVC/MinGW)、Linux、macOS
- **系统托盘** — 最小化到托盘，后台运行

## 快速开始

### 下载预编译版本 (Windows)

1. 从 [latest release](https://github.com/yourusername/LMGate/releases/latest) 下载 `llm-gateway-portable.zip`
2. 解压后运行 `llm-gateway.exe`
3. 在系统托盘查看 GUI，或浏览器打开 `http://localhost:10099`

### 从源码构建

详细的多平台构建说明请参见 [DEVELOPMENT.md](DEVELOPMENT.md)。

```bash
# Linux 快速构建
sudo apt install build-essential cmake libssl-dev zlib1g-dev libglfw3-dev libgl1-mesa-dev
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j$(nproc)

# Windows (MSVC + vcpkg)
cmake -B build -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_TOOLCHAIN_FILE=C:/vcpkg/scripts/buildsystems/vcpkg.cmake
cmake --build build --config Release
```

## 使用方法

1. 启动 LMGate — GUI 以侧边栏布局打开
2. 在**供应商**页面添加 LLM API 供应商（API Key + 端点 URL）
3. 将工具（Cursor、VS Code 等）的 API 端点设为 `http://localhost:10099/v1`
4. 在**设置**中设置 `local_api_key`，客户端工具使用相同密钥
5. 在**仪表盘**实时监控用量

## Vibe Coding / AI 辅助开发

本项目包含 [DEVELOPMENT.md](DEVELOPMENT.md) — 一份专为 AI 编程助手（Claude Code、Cursor、Copilot 等）设计的综合开发指南，使其能够在任何平台上独立理解、构建和扩展 LMGate。将其提供给您的 AI 工具，即可：

- 从源码构建项目
- 将 GUI 移植到 Linux
- 添加新功能或供应商
- 修改仪表盘

[DEVELOPMENT.md](DEVELOPMENT.md) 涵盖：架构设计、依赖关系图、各平台构建说明、源码逐文件解析、GUI 布局、主题系统、配置结构以及 Linux 移植指南。

## 许可证

MIT
