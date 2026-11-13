#include "dan.h"

#define NOMINMAX
#include <windows.h>
#include <algorithm>
#include <cctype>
#include <fstream>
#include <sstream>

static std::string lower(std::string s) {
    for (auto& c : s) c = (char)std::tolower((unsigned char)c);
    return s;
}
static bool containsCI(const std::string& hay, const std::string& needle) {
    return needle.empty() || lower(hay).find(lower(needle)) != std::string::npos;
}
static std::wstring widen(const std::string& s) {
    if (s.empty()) return {};
    int n = MultiByteToWideChar(CP_UTF8, 0, s.data(), (int)s.size(), nullptr, 0);
    std::wstring w(n, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s.data(), (int)s.size(), w.data(), n);
    return w;
}
static bool readFile(const std::wstring& path, std::string& out) {
    std::ifstream f(path, std::ios::binary);
    if (!f) return false;
    std::stringstream ss;
    ss << f.rdbuf();
    out = ss.str();
    return true;
}

Hits Hits::fromTosu(const Json& j) {
    Hits h;
    h.max = j["geki"].integer();
    h.h300 = j["300"].integer();
    h.h200 = j["katu"].integer();
    h.h100 = j["100"].integer();
    h.h50 = j["50"].integer();
    h.miss = j["0"].integer();
    return h;
}

double maniaAccuracy(const Hits& h, bool scoreV2) {
    long long n = h.total();
    if (n <= 0) return 100.0;
    double maxW = scoreV2 ? 305.0 : 300.0;
    double pts = h.max * maxW + h.h300 * 300.0 + h.h200 * 200.0 + h.h100 * 100.0 + h.h50 * 50.0;
    return 100.0 * pts / (maxW * (double)n);
}

bool Config::load(const std::wstring& path, std::string& error) {
    std::string text;
    if (!readFile(path, text)) { error = "dans.json not found (using defaults)"; return false; }
    if (text.size() >= 3 && (unsigned char)text[0] == 0xEF) text = text.substr(3);
    Json j;
    if (!Json::parse(text, j) || j.type != Json::Object) { error = "dans.json has a syntax error"; return false; }

    host = j["tosuHost"].string(host);
    port = (int)j["tosuPort"].integer(port);
    obsPort = (int)j["obsPort"].integer(obsPort);
    songsPerDan = std::max(1, (int)j["songsPerDan"].integer(songsPerDan));
    minGapMs = j["minGapMs"].number(minGapMs);
    if (j["autoKeywords"].type == Json::Array) {
        autoKeywords.clear();
        for (auto& k : j["autoKeywords"].arr) if (!k.string().empty()) autoKeywords.push_back(k.string());
    }
    dans.clear();
    for (auto& d : j["dans"].arr) {
        DanDef def;
        def.name = d["name"].string("Dan");
        def.checksum = lower(d["checksum"].string());
        def.titleContains = d["titleContains"].string();
        def.versionContains = d["versionContains"].string();
        for (auto& s : d["songs"].arr) def.songs.push_back(s.string());
        for (auto& s : d["splitsMs"].arr) def.splitsMs.push_back(s.number());
        if (def.checksum.empty() && def.titleContains.empty() && def.versionContains.empty()) continue;
        dans.push_back(def);
    }
    return true;
}

static std::string firstWord(const std::string& line) {
    return lower(line.substr(0, line.find_first_of(" \t\r")));
}

void Config::loadMarked(const std::wstring& path) {
    marked.clear();
    std::ifstream f(path);
    std::string line;
    while (std::getline(f, line)) {
        std::string md5 = firstWord(line);
        if (md5.size() == 32) marked.push_back(md5);
    }
}

bool Config::toggleMarked(const std::wstring& path, const std::string& checksum, const std::string& label) {
    std::vector<std::string> lines;
    bool removed = false;
    {
        std::ifstream f(path);
        std::string line;
        while (std::getline(f, line)) {
            if (firstWord(line) == checksum) { removed = true; continue; }
            if (!line.empty()) lines.push_back(line);
        }
    }
    if (!removed) lines.push_back(checksum + " " + label);
    {
        std::ofstream f(path, std::ios::trunc);
        for (auto& l : lines) f << l << '\n';
    }
    loadMarked(path);
    return !removed;
}

bool computeSegmentsFromText(const std::string& text, int songs, double minGapMs,
                             const std::vector<double>& manualSplits, MapSegments& out, std::string& error) {
    struct Obj { double start, end; };
    std::vector<Obj> objs;
    std::istringstream in(text);
    std::string line;
    bool inObjects = false;
    while (std::getline(in, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        if (!line.empty() && line[0] == '[') { inObjects = (line.rfind("[HitObjects]", 0) == 0); continue; }
        if (!inObjects || line.empty()) continue;
        std::vector<std::string> f;
        std::stringstream ls(line);
        std::string part;
        while (std::getline(ls, part, ',')) f.push_back(part);
        if (f.size() < 4) continue;
        double t = atof(f[2].c_str());
        int type = atoi(f[3].c_str());
        double e = t;
        if ((type & 128) && f.size() >= 6) e = atof(f[5].c_str());
        else if ((type & 8) && f.size() >= 6) e = atof(f[5].c_str());
        objs.push_back({t, std::max(t, e)});
    }
    if (objs.empty()) { error = "the .osu has no notes"; return false; }
    std::sort(objs.begin(), objs.end(), [](const Obj& a, const Obj& b) { return a.start < b.start; });

    out.splits.clear();
    if (!manualSplits.empty()) {
        out.splits = manualSplits;
        std::sort(out.splits.begin(), out.splits.end());
    } else {
        struct Gap { double from, to; };
        std::vector<Gap> gaps;
        double maxEnd = objs[0].end;
        for (size_t i = 1; i < objs.size(); ++i) {
            if (objs[i].start - maxEnd >= minGapMs) gaps.push_back({maxEnd, objs[i].start});
            maxEnd = std::max(maxEnd, objs[i].end);
        }
        std::sort(gaps.begin(), gaps.end(), [](const Gap& a, const Gap& b) { return (a.to - a.from) > (b.to - b.from); });
        if ((int)gaps.size() > songs - 1) gaps.resize(std::max(0, songs - 1));
        for (auto& g : gaps) out.splits.push_back((g.from + g.to) / 2.0);
        std::sort(out.splits.begin(), out.splits.end());
        if ((int)out.splits.size() < songs - 1)
            error = "only " + std::to_string(out.splits.size() + 1) + " songs detected (pauses >= " +
                    std::to_string((int)minGapMs) + " ms)";
    }

    out.ranges.assign(out.splits.size() + 1, {-1.0, -1.0});
    for (auto& o : objs) {
        size_t k = std::upper_bound(out.splits.begin(), out.splits.end(), o.start) - out.splits.begin();
        auto& r = out.ranges[k];
        if (r.first < 0) r.first = o.start;
        r.second = std::max(r.second, o.end);
    }
    return true;
}

bool computeSegments(const std::wstring& osuPath, int songs, double minGapMs,
                     const std::vector<double>& manualSplits, MapSegments& out, std::string& error) {
    std::string text;
    if (!readFile(osuPath, text)) { error = "could not read the .osu"; return false; }
    return computeSegmentsFromText(text, songs, minGapMs, manualSplits, out, error);
}

TosuSample TosuSample::fromJson(const Json& j) {
    TosuSample s;
    s.connected = true;
    s.state = (int)j["state"]["number"].integer();
    const Json& bm = j["beatmap"];
    s.beatmapMode = (int)bm["mode"]["number"].integer();
    s.checksum = lower(bm["checksum"].string());
    s.artist = bm["artist"].string();
    s.title = bm["title"].string();
    s.version = bm["version"].string();
    s.tags = bm["tags"].string();
    s.liveMs = bm["time"]["live"].number();
    s.play = Hits::fromTosu(j["play"]["hits"]);
    s.results = Hits::fromTosu(j["resultsScreen"]["hits"]);
    s.mods = j["play"]["mods"]["name"].string();

    std::string file = j["directPath"]["beatmapFile"].string();
    std::string songsDir = j["folders"]["songs"].string();
    if (file.empty() && !j["files"]["beatmap"].string().empty())
        file = j["folders"]["beatmap"].string() + "\\" + j["files"]["beatmap"].string();
    bool absolute = file.size() > 2 && (file[1] == ':' || (file[0] == '\\' && file[1] == '\\') || file[0] == '/');
    if (!file.empty() && !absolute && !songsDir.empty()) file = songsDir + "\\" + file;
    s.osuFile = widen(file);
    return s;
}

static void fillFromOsu(const std::string& text, TosuSample& s) {
    std::istringstream in(text);
    std::string line, section;
    while (std::getline(in, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        if (!line.empty() && line[0] == '[') { section = line; if (section == "[HitObjects]") break; continue; }
        size_t c = line.find(':');
        if (c == std::string::npos) continue;
        std::string k = line.substr(0, c), v = line.substr(c + 1);
        while (!v.empty() && v[0] == ' ') v.erase(0, 1);
        if (section == "[General]" && k == "Mode" && s.beatmapMode == 0) s.beatmapMode = atoi(v.c_str());
        if (section != "[Metadata]") continue;
        if (k == "Title" && s.title.empty()) s.title = v;
        else if (k == "Artist" && s.artist.empty()) s.artist = v;
        else if (k == "Version" && s.version.empty()) s.version = v;
        else if (k == "Tags" && s.tags.empty()) s.tags = v;
    }
}

void DanTracker::loadMap(const TosuSample& sample) {
    TosuSample s = sample;
    std::string osuText;
    bool haveOsu = !s.osuFile.empty() && readFile(s.osuFile, osuText);
    if (haveOsu) fillFromOsu(osuText, s);
    isDan = false;
    finished = false;
    songs.clear();
    splits_.clear();
    note.clear();
    danName.clear();
    mapLabel = s.artist + " - " + s.title + " [" + s.version + "]";
    checksum = s.checksum;
    resetPlay();
    if (s.checksum.empty()) { note = "tosu has not sent the map yet"; return; }

    const DanDef* def = nullptr;
    for (auto& d : cfg_->dans) {
        bool ok = d.checksum.empty() || d.checksum == s.checksum;
        ok = ok && containsCI(s.title, d.titleContains) && containsCI(s.version, d.versionContains);
        if (ok) { def = &d; break; }
    }
    bool marked = std::find(cfg_->marked.begin(), cfg_->marked.end(), s.checksum) != cfg_->marked.end();
    if (!def && !marked) {
        if (s.beatmapMode != 3) { note = "not an osu!mania map (Ctrl+Shift+D marks it as a dan)"; return; }
        bool kw = false;
        for (auto& k : cfg_->autoKeywords)
            if (containsCI(s.title, k) || containsCI(s.version, k) || containsCI(s.tags, k)) kw = true;
        if (!kw) { note = "does not look like a dan: press Ctrl+Shift+D to mark it"; return; }
    }

    int wanted = def && !def->songs.empty() ? (int)def->songs.size() : cfg_->songsPerDan;
    MapSegments seg;
    std::string err;
    static const std::vector<double> noSplits;
    bool segOk = haveOsu && computeSegmentsFromText(osuText, wanted, cfg_->minGapMs, def ? def->splitsMs : noSplits, seg, err);
    if (!haveOsu) err = "could not read the map's .osu";
    if (!segOk && !(def && !def->splitsMs.empty())) {
        note = err;
        return;
    }
    if (seg.splits.empty() && def) seg.splits = def->splitsMs;
    note = err;
    isDan = true;
    danName = def ? def->name : (s.version.empty() ? s.title : s.version);
    if (danName.empty()) danName = "Dan";
    splits_ = seg.splits;
    songs.resize(splits_.size() + 1);
    for (size_t i = 0; i < songs.size(); ++i) {
        songs[i].name = (def && i < def->songs.size() && !def->songs[i].empty()) ? def->songs[i]
                                                                                 : "Song " + std::to_string(i + 1);
        if (i < seg.ranges.size()) { songs[i].startMs = seg.ranges[i].first; songs[i].endMs = seg.ranges[i].second; }
    }
    resetPlay();
}

void DanTracker::resetPlay() {
    snapshot_ = {};
    cumulative = {};
    current = 0;
    finished = false;
    prevLive_ = 0;
    for (auto& sv : songs) { sv.hits = {}; sv.status = SongStatus::Upcoming; }
    if (!songs.empty()) songs[0].status = SongStatus::Live;
}

void DanTracker::setLiveHits(const Hits& cum) {
    cumulative = cum;
    while (current + 1 < (int)songs.size() && liveMs >= splits_[current]) {
        songs[current].hits = cum - snapshot_;
        songs[current].status = SongStatus::Done;
        snapshot_ = cum;
        ++current;
    }
    songs[current].hits = cum - snapshot_;
    songs[current].status = SongStatus::Live;
}

bool DanTracker::sameOutput(const DanTracker& o) const {
    return connected == o.connected && isDan == o.isDan && finished == o.finished && scoreV2 == o.scoreV2 &&
           state == o.state && current == o.current && liveMs == o.liveMs && danName == o.danName &&
           mapLabel == o.mapLabel && note == o.note && checksum == o.checksum && songs == o.songs &&
           cumulative == o.cumulative;
}

void DanTracker::update(const TosuSample& s) {
    connected = s.connected;
    if (!s.connected) return;
    state = s.state;

    std::string info = s.checksum + "|" + s.title + "|" + s.version + "|" + std::to_string(s.beatmapMode);
    std::wstring key = std::wstring(info.begin(), info.end()) + L"|" + s.osuFile;
    if (key != mapKey_) {
        mapKey_ = key;
        loadMap(s);
        inPlay_ = false;
    }
    if (!isDan) return;

    if (s.state == 2) {
        liveMs = s.liveMs;
        if (!inPlay_) { resetPlay(); inPlay_ = true; }
        scoreV2 = s.mods.find("V2") != std::string::npos;
        if (s.play.total() < cumulative.total() || s.liveMs < prevLive_ - 2000) resetPlay();
        prevLive_ = s.liveMs;
        setLiveHits(s.play);
    } else if (s.state == 7 && inPlay_) {
        Hits fin = s.results.total() >= cumulative.total() ? s.results : cumulative;
        liveMs = std::max(liveMs, splits_.empty() ? liveMs : splits_.back());
        setLiveHits(fin);
        songs[current].status = SongStatus::Done;
        finished = current + 1 == (int)songs.size();
        inPlay_ = false;
    } else if (s.state != 2) {
        inPlay_ = false;
    }
}
