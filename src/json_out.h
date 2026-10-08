#pragma once
// 写一行紧凑 JSON(UTF-8)。本程序只往外写、不读 JSON,所以只有这几十行。
// 逗号按层管:每进一层对象压一个"这一层下一项之前要不要逗号"。
#include <cstdio>
#include <string>
#include <vector>

namespace jsonout {

inline std::string escape(const std::string& s) {
    std::string o;
    o.reserve(s.size() + 8);
    for (unsigned char c : s) {
        switch (c) {
        case '"':  o += "\\\""; break;
        case '\\': o += "\\\\"; break;
        case '\b': o += "\\b";  break;
        case '\f': o += "\\f";  break;
        case '\n': o += "\\n";  break;
        case '\r': o += "\\r";  break;
        case '\t': o += "\\t";  break;
        default:
            if (c < 0x20) {
                char b[8];
                snprintf(b, sizeof b, "\\u%04x", (unsigned)c);
                o += b;
            } else {
                o += (char)c;     // UTF-8 原样
            }
        }
    }
    return o;
}

class Writer {
public:
    Writer& begin() { m_s += '{'; m_comma = false; return *this; }
    Writer& key(const char* k) {
        if (m_comma) m_s += ',';
        m_s += '"';
        m_s += escape(k);
        m_s += "\":";
        m_comma = false;
        return *this;
    }
    Writer& val(const char* s) { return str(s ? s : ""); }
    Writer& val(const std::string& s) { return str(s); }
    Writer& val(long long n) {
        char b[32];
        snprintf(b, sizeof b, "%lld", n);
        m_s += b;
        m_comma = true;
        return *this;
    }
    Writer& val(int n) { return val((long long)n); }
    Writer& val(bool b) { m_s += b ? "true" : "false"; m_comma = true; return *this; }
    Writer& arrOfStr(const std::vector<std::string>& v) {
        m_s += '[';
        for (size_t i = 0; i < v.size(); ++i) {
            if (i) m_s += ',';
            m_s += '"';
            m_s += escape(v[i]);
            m_s += '"';
        }
        m_s += ']';
        m_comma = true;
        return *this;
    }
    Writer& beginObj(const char* k) {
        key(k);
        m_depth.push_back(true);
        m_s += '{';
        m_comma = false;
        return *this;
    }
    Writer& endObj() { return end(); }
    Writer& end() {
        m_s += '}';
        if (!m_depth.empty()) m_depth.pop_back();
        m_comma = true;
        return *this;
    }
    const std::string& text() const { return m_s; }      // 一行,不带换行

private:
    Writer& str(const std::string& s) {
        m_s += '"';
        m_s += escape(s);
        m_s += '"';
        m_comma = true;
        return *this;
    }
    std::string m_s;
    bool m_comma = false;
    std::vector<bool> m_depth;
};

} // namespace jsonout
