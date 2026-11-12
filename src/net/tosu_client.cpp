#include "net/tosu_client.h"

#include "core/strings.h"

#include <winhttp.h>

#pragma comment(lib, "winhttp.lib")

void TosuClient::start(const std::string& host, int port, Handler onSample) {
    onSample_ = std::move(onSample);
    running_ = true;
    thread_ = std::thread(&TosuClient::run, this, widen(host), port);
}

void TosuClient::stop() {
    running_ = false;
    if (thread_.joinable()) thread_.join();
}

void TosuClient::run(std::wstring host, int port) {
    HINTERNET session = WinHttpOpen(L"DanTracker/1.0", WINHTTP_ACCESS_TYPE_NO_PROXY, nullptr, nullptr, 0);
    HINTERNET conn = session ? WinHttpConnect(session, host.c_str(), (INTERNET_PORT)port, 0) : nullptr;
    if (session) WinHttpSetTimeouts(session, 1000, 1000, 1000, 2000);
    std::string body;
    while (running_) {
        bool ok = false;
        HINTERNET req = conn ? WinHttpOpenRequest(conn, L"GET", L"/json/v2", nullptr, nullptr, nullptr, 0) : nullptr;
        if (req && WinHttpSendRequest(req, nullptr, 0, nullptr, 0, 0, 0) && WinHttpReceiveResponse(req, nullptr)) {
            body.clear();
            DWORD avail = 0;
            while (WinHttpQueryDataAvailable(req, &avail) && avail > 0) {
                size_t old = body.size();
                body.resize(old + avail);
                DWORD read = 0;
                if (!WinHttpReadData(req, &body[old], avail, &read)) break;
                body.resize(old + read);
            }
            Json j;
            if (Json::parse(body, j) && j.type == Json::Object && !j["state"].isNull()) {
                TosuSample s = TosuSample::fromJson(j);
                onSample_(&s, body);
                ok = true;
            }
        }
        if (req) WinHttpCloseHandle(req);
        if (!ok) onSample_(nullptr, body);
        Sleep(ok ? 33 : 1000);
    }
    if (conn) WinHttpCloseHandle(conn);
    if (session) WinHttpCloseHandle(session);
}
