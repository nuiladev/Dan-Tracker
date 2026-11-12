#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <winsock2.h>
#include <ws2tcpip.h>

#include <atomic>
#include <string>
#include <thread>

#include "obs_server.h"
#include "obs_page.h"

#pragma comment(lib, "ws2_32.lib")

static SOCKET g_listen = INVALID_SOCKET;
static std::thread g_thread;
static std::atomic<bool> g_stop{false};

static void sendAll(SOCKET s, const std::string& data) {
    size_t off = 0;
    while (off < data.size()) {
        int n = send(s, data.data() + off, (int)(data.size() - off), 0);
        if (n <= 0) return;
        off += n;
    }
}

static void respond(SOCKET c, const char* type, const std::string& body, const char* status = "200 OK") {
    std::string h = std::string("HTTP/1.1 ") + status + "\r\nContent-Type: " + type +
                    "\r\nCache-Control: no-store\r\nAccess-Control-Allow-Origin: *\r\nConnection: close\r\nContent-Length: " +
                    std::to_string(body.size()) + "\r\n\r\n";
    sendAll(c, h + body);
}

static void serve(std::string (*getState)()) {
    while (!g_stop) {
        SOCKET c = accept(g_listen, nullptr, nullptr);
        if (c == INVALID_SOCKET) continue;
        DWORD timeout = 1000;
        setsockopt(c, SOL_SOCKET, SO_RCVTIMEO, (const char*)&timeout, sizeof(timeout));
        char buf[2048];
        int n = recv(c, buf, sizeof(buf) - 1, 0);
        if (n > 0) {
            buf[n] = 0;
            std::string req(buf);
            std::string path = req.substr(0, req.find('\r'));
            if (path.rfind("GET /state", 0) == 0) respond(c, "application/json; charset=utf-8", getState());
            else if (path.rfind("GET / ", 0) == 0 || path.rfind("GET /?", 0) == 0) respond(c, "text/html; charset=utf-8", kObsPage);
            else respond(c, "text/plain", "not found", "404 Not Found");
        }
        shutdown(c, SD_SEND);
        closesocket(c);
    }
}

bool startObsServer(int port, std::string (*getState)()) {
    WSADATA wsa;
    if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0) return false;
    g_listen = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (g_listen == INVALID_SOCKET) return false;
    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons((u_short)port);
    inet_pton(AF_INET, "127.0.0.1", &addr.sin_addr);
    if (bind(g_listen, (sockaddr*)&addr, sizeof(addr)) != 0 || listen(g_listen, 16) != 0) {
        closesocket(g_listen);
        g_listen = INVALID_SOCKET;
        return false;
    }
    g_thread = std::thread(serve, getState);
    return true;
}

void stopObsServer() {
    if (g_listen == INVALID_SOCKET) return;
    g_stop = true;
    closesocket(g_listen);
    if (g_thread.joinable()) g_thread.join();
    WSACleanup();
}
