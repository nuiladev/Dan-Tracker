#include "obs/obs_state.h"

#include <cstdio>

static std::string jsonStr(const std::string& s) {
    std::string o = "\"";
    for (unsigned char c : s) {
        if (c == '"' || c == '\\') { o += '\\'; o += (char)c; }
        else if (c < 0x20) { char b[8]; snprintf(b, sizeof(b), "\\u%04x", c); o += b; }
        else o += (char)c;
    }
    return o + "\"";
}

std::string obsStateJson(const DanTracker& t) {
    char b[256];
    std::string o = "{";
    o += "\"connected\":" + std::string(t.connected ? "true" : "false");
    o += ",\"isDan\":" + std::string(t.isDan ? "true" : "false");
    o += ",\"finished\":" + std::string(t.finished ? "true" : "false");
    o += ",\"playing\":" + std::string(t.state == 2 ? "true" : "false");
    o += ",\"current\":" + std::to_string(t.current);
    snprintf(b, sizeof(b), ",\"liveMs\":%.0f", t.liveMs); o += b;
    o += ",\"danName\":" + jsonStr(t.danName) + ",\"mapLabel\":" + jsonStr(t.mapLabel) + ",\"note\":" + jsonStr(t.note);
    o += ",\"songs\":[";
    for (size_t i = 0; i < t.songs.size(); ++i) {
        const SongView& s = t.songs[i];
        const char* st = s.status == SongStatus::Done ? "done" : s.status == SongStatus::Live ? "live" : "upcoming";
        snprintf(b, sizeof(b),
                 "%s{\"status\":\"%s\",\"acc\":%.4f,\"judged\":%lld,\"max\":%lld,\"h300\":%lld,\"h200\":%lld,"
                 "\"h100\":%lld,\"h50\":%lld,\"miss\":%lld,\"startMs\":%.0f,\"endMs\":%.0f,\"name\":",
                 i ? "," : "", st, maniaAccuracy(s.hits, t.scoreV2), s.hits.total(), s.hits.max, s.hits.h300, s.hits.h200,
                 s.hits.h100, s.hits.h50, s.hits.miss, s.startMs, s.endMs);
        o += b + jsonStr(s.name) + "}";
    }
    const Hits& c = t.cumulative;
    snprintf(b, sizeof(b),
             "],\"total\":{\"acc\":%.4f,\"judged\":%lld,\"max\":%lld,\"h300\":%lld,\"h200\":%lld,\"h100\":%lld,"
             "\"h50\":%lld,\"miss\":%lld}}",
             maniaAccuracy(c, t.scoreV2), c.total(), c.max, c.h300, c.h200, c.h100, c.h50, c.miss);
    return o + b;
}
