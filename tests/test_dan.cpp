#include <cmath>
#include <cstdio>
#include <fstream>
#define NOMINMAX
#include <windows.h>
#include "core/dan.h"

static int fails = 0;
#define CHECK(c) do { if (!(c)) { std::printf("FAIL line %d: %s\n", __LINE__, #c); ++fails; } } while (0)

int main() {
    std::string osu = "osu file format v14\r\n[General]\r\nMode: 3\r\n\r\n[HitObjects]\r\n";
    double songStart[4] = {1000, 26000, 51000, 76000};
    for (int s = 0; s < 4; ++s) {
        for (int k = 0; k < 40; ++k) {
            int t = (int)songStart[s] + k * 500;
            if (k == 39) osu += "64,192," + std::to_string(t) + ",128,0," + std::to_string(t + 500) + ":0:0:0:0:\r\n";
            else osu += "64,192," + std::to_string(t) + ",1,0,0:0:0:0:\r\n";
        }
    }
    wchar_t tmp[MAX_PATH];
    GetTempPathW(MAX_PATH, tmp);
    std::wstring path = std::wstring(tmp) + L"dan_test.osu";
    { std::ofstream f(path, std::ios::binary); f << osu; }

    MapSegments seg;
    std::string err;
    CHECK(computeSegments(path, 4, 3000, {}, seg, err));
    CHECK(seg.splits.size() == 3);
    CHECK(std::fabs(seg.splits[0] - 23500) < 1e-6);
    CHECK(seg.ranges[1].first == 26000 && seg.ranges[1].second == 46000);

    Hits h{10, 0, 0, 0, 0, 0};
    CHECK(std::fabs(maniaAccuracy(h, false) - 100.0) < 1e-9);
    Hits h2{1, 1, 1, 1, 1, 1};
    CHECK(std::fabs(maniaAccuracy(h2, false) - 950.0 / 1800.0 * 100.0) < 1e-9);
    CHECK(std::fabs(maniaAccuracy(h2, true) - (305 + 300 + 200 + 100 + 50) / (6 * 305.0) * 100.0) < 1e-9);

    Config cfg;
    DanTracker t(&cfg);
    auto sample = [&](int state, double live, Hits play, Hits res = {}) {
        TosuSample s;
        s.connected = true;
        s.state = state;
        s.beatmapMode = 3;
        s.checksum = "abc";
        s.title = "Dan Course";
        s.version = "4th Dan";
        s.osuFile = path;
        s.liveMs = live;
        s.play = play;
        s.results = res;
        t.update(s);
    };
    sample(5, 0, {});
    CHECK(t.isDan && t.songs.size() == 4);
    sample(2, 500, {});
    sample(2, 21500, {38, 2, 0, 0, 0, 0});
    CHECK(t.current == 0);
    sample(2, 24000, {38, 2, 0, 0, 0, 0});
    CHECK(t.current == 1 && t.songs[0].status == SongStatus::Done);
    CHECK(t.songs[0].hits.max == 38 && t.songs[0].hits.h300 == 2);
    sample(2, 47000, {70, 2, 5, 0, 0, 3});
    CHECK(t.songs[1].hits.max == 32 && t.songs[1].hits.h200 == 5 && t.songs[1].hits.miss == 3);
    sample(2, 49000, {70, 2, 5, 0, 0, 3});
    sample(2, 74000, {100, 2, 15, 0, 0, 3});
    sample(2, 96500, {130, 12, 15, 0, 0, 3});
    CHECK(t.current == 3);
    CHECK(t.songs[2].hits.max == 30 && t.songs[2].hits.h200 == 10);
    sample(7, 0, {}, {135, 15, 15, 0, 0, 5});
    CHECK(t.finished);
    CHECK(t.songs[3].hits.max == 35 && t.songs[3].hits.h300 == 13 && t.songs[3].hits.miss == 2);
    CHECK(t.songs[3].status == SongStatus::Done);
    long long sum = 0;
    for (auto& s : t.songs) sum += s.hits.total();
    CHECK(sum == t.cumulative.total() && sum == 170);

    sample(2, 300, {});
    CHECK(t.current == 0 && t.songs[0].hits.total() == 0 && !t.finished);
    sample(2, 5000, {5, 0, 0, 0, 0, 0});
    sample(2, 1000, {});
    CHECK(t.cumulative.total() == 0);

    TosuSample other;
    other.connected = true; other.state = 5; other.beatmapMode = 3; other.checksum = "zzz";
    other.title = "Some Song"; other.version = "Hard"; other.osuFile = path;
    t.update(other);
    CHECK(!t.isDan);

    std::wstring cpath = std::wstring(tmp) + L"dan_test.json";
    { std::ofstream f(cpath, std::ios::binary);
      f << "{ // comment\n \"songsPerDan\": 4, \"dans\": [ {\"name\": \"Alpha\", \"versionContains\": \"alpha\","
           " \"songs\": [\"A\",\"B\",\"C\",\"D\"], \"splitsMs\": [24000, 49000, 74000]} ] }"; }
    Config c2;
    CHECK(c2.load(cpath, err));
    CHECK(c2.dans.size() == 1 && c2.dans[0].songs.size() == 4 && c2.dans[0].splitsMs.size() == 3);
    DanTracker t2(&c2);
    TosuSample a = other; a.version = "ALPHA dan"; a.title = "x"; a.checksum = "q";
    t2.update(a);
    CHECK(t2.isDan && t2.danName == "Alpha" && t2.songs[2].name == "C");

    std::wstring mpath = std::wstring(tmp) + L"dan_marked.txt";
    DeleteFileW(mpath.c_str());
    Config c3;
    std::string md5 = "0123456789abcdef0123456789abcdef";
    CHECK(c3.toggleMarked(mpath, md5, "Some Song [Hard]"));
    DanTracker t3(&c3);
    TosuSample m = other; m.checksum = md5;
    t3.update(m);
    CHECK(t3.isDan && t3.songs.size() == 4);
    CHECK(!c3.toggleMarked(mpath, md5, "x") && c3.marked.empty());
    t3.setConfig(&c3);
    t3.update(m);
    CHECK(!t3.isDan);
    DeleteFileW(mpath.c_str());

    DanTracker t4(&cfg);
    TosuSample e = other; e.checksum = "fff"; e.title = ""; e.version = ""; e.artist = ""; e.tags = "";
    t4.update(e);
    CHECK(!t4.isDan);
    e.title = "Dan Course"; e.version = "4th Dan";
    t4.update(e);
    CHECK(t4.isDan && t4.songs.size() == 4);

    std::wstring path2 = std::wstring(tmp) + L"dan_meta.osu";
    {
        std::string withMeta = osu;
        withMeta.replace(withMeta.find("[HitObjects]"), 0, "[Metadata]\r\nTitle:Dan Course\r\nArtist:Various\r\nVersion:5th Dan\r\n\r\n");
        std::ofstream f(path2, std::ios::binary);
        f << withMeta;
    }
    DanTracker t5(&cfg);
    TosuSample n = e; n.checksum = "eee"; n.title = ""; n.version = ""; n.osuFile = path2;
    t5.update(n);
    CHECK(t5.isDan && t5.danName == "5th Dan" && t5.songs.size() == 4);
    DeleteFileW(path2.c_str());

    {
        Json j;
        CHECK(Json::parse(R"({"state":{"number":5},"beatmap":{"checksum":"FF62","mode":{"number":3},"title":"ASGORE"},
            "folders":{"songs":"C:\\Users\\User\\AppData\\Local\\osu!\\Songs","beatmap":"824051 toby fox - ASGORE"},
            "files":{"beatmap":"a [b].osu"},
            "directPath":{"beatmapFile":"824051 toby fox - ASGORE\\a [b].osu"}})", j));
        TosuSample s = TosuSample::fromJson(j);
        CHECK(s.osuFile == L"C:\\Users\\User\\AppData\\Local\\osu!\\Songs\\824051 toby fox - ASGORE\\a [b].osu");
        CHECK(s.checksum == "ff62" && s.beatmapMode == 3);
        Json k;
        CHECK(Json::parse(R"({"state":{"number":5},"folders":{"songs":"C:\\S"},"directPath":{"beatmapFile":"D:\\x\\y.osu"}})", k));
        CHECK(TosuSample::fromJson(k).osuFile == L"D:\\x\\y.osu");
    }

    DeleteFileW(path.c_str());
    DeleteFileW(cpath.c_str());
    std::printf(fails ? "%d FAILED\n" : "all tests passed\n", fails);
    return fails ? 1 : 0;
}
