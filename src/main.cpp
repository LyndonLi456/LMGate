#include "httplib.h"
#include "config_manager.h"
#include "provider_manager.h"
#include "stats_db.h"
#include "request_forwarder.h"
#include "api_router.h"
#include "gui_app.h"
#include <iostream>
#include <csignal>
#include <string>
#include <thread>
#include <atomic>
#include <memory>

static httplib::Server* g_server = nullptr;
static std::atomic<bool> g_running{true};

void signalHandler(int) {
    g_running = false;
    if (g_server) g_server->stop();
}

std::string getExeDir(const char* argv0) {
    std::string path(argv0);
    auto pos = path.find_last_of("/\\");
    if (pos != std::string::npos) {
        return path.substr(0, pos) + "/";
    }
    return "./";
}

void runHttpServer(httplib::Server* server, int port) {
    server->listen("0.0.0.0", port);
}

int main(int argc, char* argv[]) {
    std::string exeDir = getExeDir(argv[0]);
    std::string configPath = exeDir + "config.json";
    std::string dbPath = exeDir + "llm-gateway-stats.db";

    if (argc > 1) {
        configPath = argv[1];
        dbPath = std::string(argv[1]) + ".db";
    }

    // Load config
    ConfigManager config;
    bool existed = config.load(configPath);
    if (!existed) {
        config.save(configPath);
        std::cout << "Created default config: " << configPath << std::endl;
    }

    // Open stats database
    StatsDatabase stats;
    if (!stats.open(dbPath)) {
        std::cerr << "Warning: Failed to open stats database at " << dbPath << std::endl;
    }

    // Wire components
    ProviderManager providers(&config);
    RequestForwarder forwarder(&providers, &stats);

    // Setup HTTP server
    auto server = std::make_unique<httplib::Server>();
    g_server = server.get();
    int port = config.getPort();

    ApiRouter router;
    router.setup(*server, &config, &providers, &forwarder, &stats);

    std::signal(SIGINT, signalHandler);
    std::signal(SIGTERM, signalHandler);
#ifdef _WIN32
    std::signal(SIGBREAK, signalHandler);
#endif

    // Start HTTP server in background thread
    std::thread httpThread(runHttpServer, server.get(), port);
    httpThread.detach();

    std::cout << "========================================" << std::endl;
    std::cout << "  LMGate - LLM API Gateway v1.1.0" << std::endl;
    std::cout << "========================================" << std::endl;
    std::cout << "  GUI:         Opening native window..." << std::endl;
    std::cout << "  OpenAI API:  http://localhost:" << port << "/v1/chat/completions" << std::endl;
    std::cout << "  Anthropic:   http://localhost:" << port << "/v1/messages" << std::endl;
    std::cout << "  Health:      http://localhost:" << port << "/health" << std::endl;
    std::cout << "  Config:      " << configPath << std::endl;
    std::cout << "========================================" << std::endl;

    // Run GUI in main thread (loop supports restart)
    int runResult = 0;
    do {
        GuiApp gui(&config, &providers, &stats);
        if (!gui.init("LMGate - LLM API Gateway", 1100, 700)) {
            std::cerr << "Failed to initialize GUI. Running in headless mode." << std::endl;
            while (g_running) {
                std::this_thread::sleep_for(std::chrono::milliseconds(100));
            }
            break;
        }
        runResult = gui.run();
        gui.shutdown();

        if (runResult == 1) {
            config.load(configPath);
            server->stop();
            g_server = nullptr;
            port = config.getPort();
            server = std::make_unique<httplib::Server>();
            g_server = server.get();
            router.setup(*server, &config, &providers, &forwarder, &stats);
            httpThread = std::thread(runHttpServer, server.get(), port);
            httpThread.detach();
            std::cout << "[Restart] Server restarted on port " << port << std::endl;
        }
    } while (runResult == 1);

    // Cleanup
    g_running = false;
    if (server) server->stop();
    stats.close();

    std::cout << "Gateway stopped." << std::endl;
    return 0;
}
