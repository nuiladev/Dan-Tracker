#pragma once
#include <string>
#include <vector>
#include "json.h"

struct Hits {
    long long max = 0, h300 = 0, h200 = 0, h100 = 0, h50 = 0, miss = 0;
    long long total() const { return max + h300 + h200 + h100 + h50 + miss; }
    Hits operator-(const Hits& o) const {
        return {max - o.max, h300 - o.h300, h200 - o.h200, h100 - o.h100, h50 - o.h50, miss - o.miss};
    }
    bool operator==(const Hits& o) const {
        return max == o.max && h300 == o.h300 && h200 == o.h200 && h100 == o.h100 && h50 == o.h50 && miss == o.miss;
    }
    bool operator!=(const Hits& o) const { return !(*this == o); }
    static Hits fromTosu(const Json& j);
};

double maniaAccuracy(const Hits& h, bool scoreV2);

struct DanDef {
    std::string name;
    std::string checksum;
    std::string titleContains;
    std::string versionContains;
    std::vector<std::string> songs;
    std::vector<double> splitsMs;
};

struct Config {
    std::string host = "127.0.0.1";
    int port = 24050;
    int obsPort = 24051;
    int songsPerDan = 4;
    double minGapMs = 3000;
    std::vector<std::string> autoKeywords = {"dan"};
    std::vector<DanDef> dans;
    std::vector<std::string> marked;

    bool load(const std::wstring& path, std::string& error);
    void loadMarked(const std::wstring& path);
    bool toggleMarked(const std::wstring& path, const std::string& checksum, const std::string& label);
};

struct MapSegments {
    std::vector<double> splits;
    std::vector<std::pair<double, double>> ranges;
};

bool computeSegments(const std::wstring& osuPath, int songs, double minGapMs,
                     const std::vector<double>& manualSplits, MapSegments& out, std::string& error);
bool computeSegmentsFromText(const std::string& osuText, int songs, double minGapMs,
                             const std::vector<double>& manualSplits, MapSegments& out, std::string& error);

struct TosuSample {
    bool connected = false;
    int state = 0;
    int beatmapMode = 0;
    std::string checksum, artist, title, version, tags, mods;
    std::wstring osuFile;
    double liveMs = 0;
    Hits play, results;

    static TosuSample fromJson(const Json& j);
};

enum class SongStatus { Upcoming, Live, Done };

struct SongView {
    std::string name;
    Hits hits;
    SongStatus status = SongStatus::Upcoming;
    double startMs = 0, endMs = 0;
    bool operator==(const SongView& o) const {
        return name == o.name && hits == o.hits && status == o.status && startMs == o.startMs && endMs == o.endMs;
    }
};

class DanTracker {
public:
    explicit DanTracker(const Config* cfg) : cfg_(cfg) {}
    void setConfig(const Config* cfg) { cfg_ = cfg; mapKey_.clear(); }
    void update(const TosuSample& s);
    bool sameOutput(const DanTracker& o) const;

    bool connected = false;
    bool isDan = false;
    bool finished = false;
    bool scoreV2 = false;
    int state = 0;
    int current = 0;
    double liveMs = 0;
    std::string danName, mapLabel, note, checksum;
    std::vector<SongView> songs;
    Hits cumulative;

private:
    void loadMap(const TosuSample& s);
    void resetPlay();
    void setLiveHits(const Hits& cum);

    const Config* cfg_;
    std::wstring mapKey_;
    std::vector<double> splits_;
    Hits snapshot_;
    bool inPlay_ = false;
    double prevLive_ = 0;
};
