#pragma once
#include <map>
#include <memory>
#include <string>
#include <vector>
#include <cstdlib>

struct Json {
    enum Type { Null, Bool, Number, String, Array, Object } type = Null;
    bool b = false;
    double num = 0;
    std::string str;
    std::vector<Json> arr;
    std::map<std::string, Json> obj;

    const Json& operator[](const char* key) const {
        static const Json none;
        if (type != Object) return none;
        auto it = obj.find(key);
        return it == obj.end() ? none : it->second;
    }
    const Json& operator[](size_t i) const {
        static const Json none;
        return (type == Array && i < arr.size()) ? arr[i] : none;
    }
    double number(double def = 0) const { return type == Number ? num : (type == Bool ? (b ? 1 : 0) : def); }
    long long integer(long long def = 0) const { return type == Number ? (long long)num : def; }
    std::string string(const std::string& def = "") const { return type == String ? str : def; }
    bool boolean(bool def = false) const { return type == Bool ? b : def; }
    bool isNull() const { return type == Null; }

    static bool parse(const std::string& text, Json& out) {
        size_t i = 0;
        if (!parseValue(text, i, out)) return false;
        skipWs(text, i);
        return i == text.size();
    }

private:
    static void skipWs(const std::string& s, size_t& i) {
        while (i < s.size()) {
            char c = s[i];
            if (c == ' ' || c == '\n' || c == '\r' || c == '\t') { ++i; continue; }
            if (c == '/' && i + 1 < s.size() && s[i + 1] == '/') {
                while (i < s.size() && s[i] != '\n') ++i;
                continue;
            }
            break;
        }
    }
    static void appendUtf8(std::string& o, unsigned cp) {
        if (cp < 0x80) o += (char)cp;
        else if (cp < 0x800) { o += (char)(0xC0 | (cp >> 6)); o += (char)(0x80 | (cp & 0x3F)); }
        else if (cp < 0x10000) { o += (char)(0xE0 | (cp >> 12)); o += (char)(0x80 | ((cp >> 6) & 0x3F)); o += (char)(0x80 | (cp & 0x3F)); }
        else { o += (char)(0xF0 | (cp >> 18)); o += (char)(0x80 | ((cp >> 12) & 0x3F)); o += (char)(0x80 | ((cp >> 6) & 0x3F)); o += (char)(0x80 | (cp & 0x3F)); }
    }
    static bool parseHex4(const std::string& s, size_t& i, unsigned& cp) {
        if (i + 4 > s.size()) return false;
        cp = (unsigned)strtoul(s.substr(i, 4).c_str(), nullptr, 16);
        i += 4;
        return true;
    }
    static bool parseString(const std::string& s, size_t& i, std::string& out) {
        if (s[i] != '"') return false;
        ++i;
        while (i < s.size()) {
            char c = s[i++];
            if (c == '"') return true;
            if (c != '\\') { out += c; continue; }
            if (i >= s.size()) return false;
            char e = s[i++];
            switch (e) {
                case 'n': out += '\n'; break;
                case 't': out += '\t'; break;
                case 'r': out += '\r'; break;
                case 'b': out += '\b'; break;
                case 'f': out += '\f'; break;
                case 'u': {
                    unsigned cp;
                    if (!parseHex4(s, i, cp)) return false;
                    if (cp >= 0xD800 && cp <= 0xDBFF && i + 6 <= s.size() && s[i] == '\\' && s[i + 1] == 'u') {
                        i += 2;
                        unsigned lo;
                        if (!parseHex4(s, i, lo)) return false;
                        cp = 0x10000 + ((cp - 0xD800) << 10) + (lo - 0xDC00);
                    }
                    appendUtf8(out, cp);
                    break;
                }
                default: out += e;
            }
        }
        return false;
    }
    static bool parseValue(const std::string& s, size_t& i, Json& out) {
        skipWs(s, i);
        if (i >= s.size()) return false;
        char c = s[i];
        if (c == '{') {
            out.type = Object;
            ++i;
            skipWs(s, i);
            if (i < s.size() && s[i] == '}') { ++i; return true; }
            while (true) {
                skipWs(s, i);
                std::string key;
                if (i >= s.size() || !parseString(s, i, key)) return false;
                skipWs(s, i);
                if (i >= s.size() || s[i] != ':') return false;
                ++i;
                if (!parseValue(s, i, out.obj[key])) return false;
                skipWs(s, i);
                if (i < s.size() && s[i] == ',') { ++i; continue; }
                if (i < s.size() && s[i] == '}') { ++i; return true; }
                return false;
            }
        }
        if (c == '[') {
            out.type = Array;
            ++i;
            skipWs(s, i);
            if (i < s.size() && s[i] == ']') { ++i; return true; }
            while (true) {
                out.arr.emplace_back();
                if (!parseValue(s, i, out.arr.back())) return false;
                skipWs(s, i);
                if (i < s.size() && s[i] == ',') { ++i; continue; }
                if (i < s.size() && s[i] == ']') { ++i; return true; }
                return false;
            }
        }
        if (c == '"') { out.type = String; return parseString(s, i, out.str); }
        if (s.compare(i, 4, "true") == 0) { out.type = Bool; out.b = true; i += 4; return true; }
        if (s.compare(i, 5, "false") == 0) { out.type = Bool; out.b = false; i += 5; return true; }
        if (s.compare(i, 4, "null") == 0) { out.type = Null; i += 4; return true; }
        char* end = nullptr;
        out.num = strtod(s.c_str() + i, &end);
        if (end == s.c_str() + i) return false;
        out.type = Number;
        i = end - s.c_str();
        return true;
    }
};
