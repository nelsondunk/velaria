// Minimal JSON value + parser + writer (enough for the bridge protocol). Header-only.
#pragma once
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <map>
#include <string>
#include <vector>

namespace vrq {

struct Json {
    enum Type { Null, Bool, Number, String, Array, Object } type = Null;
    bool b = false;
    double num = 0;
    std::string str;
    std::vector<Json> arr;
    std::map<std::string, Json> obj;

    Json() = default;
    Json(bool v) : type(Bool), b(v) {}
    Json(int v) : type(Number), num(v) {}
    Json(int64_t v) : type(Number), num((double)v) {}
    Json(double v) : type(Number), num(v) {}
    Json(const char* s) : type(String), str(s) {}
    Json(const std::string& s) : type(String), str(s) {}
    static Json object() {
        Json j;
        j.type = Object;
        return j;
    }
    static Json array() {
        Json j;
        j.type = Array;
        return j;
    }
    Json& operator[](const std::string& k) {
        type = Object;
        return obj[k];
    }
    const Json& get(const std::string& k) const {
        static const Json none;
        auto it = obj.find(k);
        return it == obj.end() ? none : it->second;
    }
    bool has(const std::string& k) const { return obj.count(k) > 0; }
    double numberOr(double d) const { return type == Number ? num : type == Bool ? (b ? 1 : 0) : d; }
    int intOr(int d) const { return type == Number ? (int)std::llround(num) : d; }
    bool boolOr(bool d) const { return type == Bool ? b : type == Number ? num != 0 : d; }
    const std::string& stringOr(const std::string& d) const { return type == String ? str : d; }
    void push(const Json& v) {
        type = Array;
        arr.push_back(v);
    }

    std::string dump() const {
        std::string out;
        write(out);
        return out;
    }
    void write(std::string& out) const {
        char buf[64];
        switch (type) {
            case Null: out += "null"; break;
            case Bool: out += b ? "true" : "false"; break;
            case Number:
                if (std::isfinite(num) && num == std::floor(num) && std::fabs(num) < 1e15) snprintf(buf, sizeof buf, "%lld", (long long)num);
                else snprintf(buf, sizeof buf, "%.6g", num);
                out += buf;
                break;
            case String: writeString(out, str); break;
            case Array:
                out += '[';
                for (size_t i = 0; i < arr.size(); i++) {
                    if (i) out += ',';
                    arr[i].write(out);
                }
                out += ']';
                break;
            case Object: {
                out += '{';
                bool first = true;
                for (auto& kv : obj) {
                    if (!first) out += ',';
                    first = false;
                    writeString(out, kv.first);
                    out += ':';
                    kv.second.write(out);
                }
                out += '}';
                break;
            }
        }
    }
    static void writeString(std::string& out, const std::string& s) {
        out += '"';
        for (unsigned char c : s) {
            if (c == '"' || c == '\\') {
                out += '\\';
                out += (char)c;
            } else if (c < 0x20) {
                char b[8];
                snprintf(b, sizeof b, "\\u%04x", c);
                out += b;
            } else
                out += (char)c;
        }
        out += '"';
    }

    // Parses `s`; returns false on malformed input (result then undefined).
    static bool parse(const std::string& s, Json& out) {
        size_t i = 0;
        if (!parseValue(s, i, out)) return false;
        skipWs(s, i);
        return i == s.size();
    }

private:
    static void skipWs(const std::string& s, size_t& i) {
        while (i < s.size() && (s[i] == ' ' || s[i] == '\n' || s[i] == '\r' || s[i] == '\t')) i++;
    }
    static bool parseString(const std::string& s, size_t& i, std::string& out) {
        if (i >= s.size() || s[i] != '"') return false;
        i++;
        while (i < s.size()) {
            char c = s[i++];
            if (c == '"') return true;
            if (c == '\\') {
                if (i >= s.size()) return false;
                char e = s[i++];
                switch (e) {
                    case '"': out += '"'; break;
                    case '\\': out += '\\'; break;
                    case '/': out += '/'; break;
                    case 'b': out += '\b'; break;
                    case 'f': out += '\f'; break;
                    case 'n': out += '\n'; break;
                    case 'r': out += '\r'; break;
                    case 't': out += '\t'; break;
                    case 'u': {
                        if (i + 4 > s.size()) return false;
                        unsigned cp = (unsigned)strtoul(s.substr(i, 4).c_str(), nullptr, 16);
                        i += 4;
                        if (cp >= 0xD800 && cp <= 0xDBFF && i + 6 <= s.size() && s[i] == '\\' && s[i + 1] == 'u') {
                            unsigned lo = (unsigned)strtoul(s.substr(i + 2, 4).c_str(), nullptr, 16);
                            i += 6;
                            cp = 0x10000 + ((cp - 0xD800) << 10) + (lo - 0xDC00);
                        }
                        appendUtf8(out, cp);
                        break;
                    }
                    default: return false;
                }
            } else
                out += c;
        }
        return false;
    }
    static void appendUtf8(std::string& o, unsigned cp) {
        if (cp < 0x80) o += (char)cp;
        else if (cp < 0x800) {
            o += (char)(0xC0 | (cp >> 6));
            o += (char)(0x80 | (cp & 0x3F));
        } else if (cp < 0x10000) {
            o += (char)(0xE0 | (cp >> 12));
            o += (char)(0x80 | ((cp >> 6) & 0x3F));
            o += (char)(0x80 | (cp & 0x3F));
        } else {
            o += (char)(0xF0 | (cp >> 18));
            o += (char)(0x80 | ((cp >> 12) & 0x3F));
            o += (char)(0x80 | ((cp >> 6) & 0x3F));
            o += (char)(0x80 | (cp & 0x3F));
        }
    }
    static bool parseValue(const std::string& s, size_t& i, Json& out) {
        skipWs(s, i);
        if (i >= s.size()) return false;
        char c = s[i];
        if (c == '{') {
            out = object();
            i++;
            skipWs(s, i);
            if (i < s.size() && s[i] == '}') {
                i++;
                return true;
            }
            while (true) {
                skipWs(s, i);
                std::string k;
                if (!parseString(s, i, k)) return false;
                skipWs(s, i);
                if (i >= s.size() || s[i] != ':') return false;
                i++;
                Json v;
                if (!parseValue(s, i, v)) return false;
                out.obj[k] = std::move(v);
                skipWs(s, i);
                if (i < s.size() && s[i] == ',') {
                    i++;
                    continue;
                }
                if (i < s.size() && s[i] == '}') {
                    i++;
                    return true;
                }
                return false;
            }
        }
        if (c == '[') {
            out = array();
            i++;
            skipWs(s, i);
            if (i < s.size() && s[i] == ']') {
                i++;
                return true;
            }
            while (true) {
                Json v;
                if (!parseValue(s, i, v)) return false;
                out.arr.push_back(std::move(v));
                skipWs(s, i);
                if (i < s.size() && s[i] == ',') {
                    i++;
                    continue;
                }
                if (i < s.size() && s[i] == ']') {
                    i++;
                    return true;
                }
                return false;
            }
        }
        if (c == '"') {
            out = Json("");
            return parseString(s, i, out.str);
        }
        if (s.compare(i, 4, "true") == 0) {
            out = Json(true);
            i += 4;
            return true;
        }
        if (s.compare(i, 5, "false") == 0) {
            out = Json(false);
            i += 5;
            return true;
        }
        if (s.compare(i, 4, "null") == 0) {
            out = Json();
            i += 4;
            return true;
        }
        char* end = nullptr;
        double d = strtod(s.c_str() + i, &end);
        if (end == s.c_str() + i) return false;
        out = Json(d);
        i = (size_t)(end - s.c_str());
        return true;
    }
};

}  // namespace vrq
