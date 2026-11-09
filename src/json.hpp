// RoutePerApp - minimaler, robuster JSON-Layer (UTF-8 auf Disk, UTF-16 im Speicher).
// Absichtlich handgeschrieben: keine Abhängigkeiten, deterministisch testbar.
#pragma once

#include <string>
#include <vector>
#include <memory>
#include <utility>
#include <cstdint>
#include <cstdlib>
#include <cwchar>
#include <cstdio>

namespace sr {

enum class JsonKind { Null, Bool, Number, String, Array, Object };

class Json;
using JsonPtr = std::shared_ptr<Json>;

class Json {
public:
    JsonKind kind = JsonKind::Null;
    bool b = false;
    double num = 0.0;
    std::wstring str;
    std::vector<JsonPtr> arr;
    std::vector<std::pair<std::wstring, JsonPtr>> obj;

    static JsonPtr MakeNull() { return std::make_shared<Json>(); }
    static JsonPtr MakeBool(bool v) { JsonPtr p = MakeNull(); p->kind = JsonKind::Bool; p->b = v; return p; }
    static JsonPtr MakeNumber(double v) { JsonPtr p = MakeNull(); p->kind = JsonKind::Number; p->num = v; return p; }
    static JsonPtr MakeString(std::wstring v) { JsonPtr p = MakeNull(); p->kind = JsonKind::String; p->str = std::move(v); return p; }
    static JsonPtr MakeArray() { JsonPtr p = MakeNull(); p->kind = JsonKind::Array; return p; }
    static JsonPtr MakeObject() { JsonPtr p = MakeNull(); p->kind = JsonKind::Object; return p; }

    bool isNull()   const { return kind == JsonKind::Null; }
    bool isBool()   const { return kind == JsonKind::Bool; }
    bool isNumber() const { return kind == JsonKind::Number; }
    bool isString() const { return kind == JsonKind::String; }
    bool isArray()  const { return kind == JsonKind::Array; }
    bool isObject() const { return kind == JsonKind::Object; }

    const JsonPtr* find(const wchar_t* key) const {
        for (const auto& kv : obj) if (kv.first == key) return &kv.second;
        return nullptr;
    }
    bool getStr(const wchar_t* key, std::wstring& out) const {
        const JsonPtr* v = find(key);
        if (!v || !(*v) || !(*v)->isString()) return false;
        out = (*v)->str;
        return true;
    }
    bool getNum(const wchar_t* key, double& out) const {
        const JsonPtr* v = find(key);
        if (!v || !(*v) || !(*v)->isNumber()) return false;
        out = (*v)->num;
        return true;
    }
    bool getBool(const wchar_t* key, bool& out) const {
        const JsonPtr* v = find(key);
        if (!v || !(*v) || !(*v)->isBool()) return false;
        out = (*v)->b;
        return true;
    }
    std::wstring strOr(const wchar_t* key, const wchar_t* def) const {
        std::wstring s;
        return getStr(key, s) ? s : std::wstring(def);
    }
    int intOr(const wchar_t* key, int def) const {
        double d;
        return getNum(key, d) ? static_cast<int>(d) : def;
    }
    bool boolOr(const wchar_t* key, bool def) const {
        bool v;
        return getBool(key, v) ? v : def;
    }
    void set(const std::wstring& key, JsonPtr v) {
        for (auto& kv : obj) if (kv.first == key) { kv.second = std::move(v); return; }
        obj.emplace_back(key, std::move(v));
    }
    void push(JsonPtr v) { arr.push_back(std::move(v)); }
};

struct JsonParseResult {
    bool ok = false;
    std::wstring error;
    size_t line = 0;
    size_t col = 0;
};

// ---------------------------------------------------------------- UTF-8/UTF-16

inline std::string U8FromW(const std::wstring& w) {
    std::string out;
    out.reserve(w.size() * 2 + 1);
    for (size_t i = 0; i < w.size(); ++i) {
        uint32_t cp = w[i];
        if (cp >= 0xD800 && cp < 0xDC00 && i + 1 < w.size() && w[i + 1] >= 0xDC00 && w[i + 1] < 0xE000) {
            cp = 0x10000 + ((cp - 0xD800) << 10) + (static_cast<uint32_t>(w[i + 1]) - 0xDC00);
            ++i;
        }
        if (cp < 0x80) {
            out.push_back(static_cast<char>(cp));
        } else if (cp < 0x800) {
            out.push_back(static_cast<char>(0xC0 | (cp >> 6)));
            out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
        } else if (cp < 0x10000) {
            out.push_back(static_cast<char>(0xE0 | (cp >> 12)));
            out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
            out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
        } else {
            out.push_back(static_cast<char>(0xF0 | (cp >> 18)));
            out.push_back(static_cast<char>(0x80 | ((cp >> 12) & 0x3F)));
            out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
            out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
        }
    }
    return out;
}

inline std::wstring WFromU8(const std::string& s) {
    std::wstring out;
    out.reserve(s.size());
    size_t i = 0;
    while (i < s.size()) {
        unsigned char c = static_cast<unsigned char>(s[i]);
        uint32_t cp = 0;
        size_t extra = 0;
        if (c < 0x80) { cp = c; }
        else if ((c & 0xE0) == 0xC0) { cp = c & 0x1F; extra = 1; }
        else if ((c & 0xF0) == 0xE0) { cp = c & 0x0F; extra = 2; }
        else if ((c & 0xF8) == 0xF0) { cp = c & 0x07; extra = 3; }
        else { out += L'\uFFFD'; ++i; continue; }   // ungültiges Startbyte
        if (i + extra >= s.size() + 0 && i + extra > s.size() - 1) { out += L'\uFFFD'; ++i; continue; }
        bool bad = false;
        for (size_t k = 1; k <= extra; ++k) {
            unsigned char cc = static_cast<unsigned char>(s[i + k]);
            if ((cc & 0xC0) != 0x80) { bad = true; break; }
            cp = (cp << 6) | (cc & 0x3F);
        }
        if (bad) { out += L'\uFFFD'; ++i; continue; }
        i += extra + 1;
        if (cp < 0x10000) {
            out.push_back(static_cast<wchar_t>(cp));
        } else {
            cp -= 0x10000;
            out.push_back(static_cast<wchar_t>(0xD800 + (cp >> 10)));
            out.push_back(static_cast<wchar_t>(0xDC00 + (cp & 0x3FF)));
        }
    }
    return out;
}

// ---------------------------------------------------------------- Parser

namespace jsondetail {

class Parser {
public:
    explicit Parser(const std::wstring& t) : s_(t) {}

    JsonParseResult parse(JsonPtr& out) {
        skipWs();
        if (!parseValue(out)) return res_;
        skipWs();
        if (pos_ != s_.size()) { fail("Daten nach JSON-Ende"); return res_; }
        res_.ok = true;
        return res_;
    }

private:
    const std::wstring& s_;
    size_t pos_ = 0;
    JsonParseResult res_;

    // Kurzform für ASCII/UTF-8-Meldungen aus dem Quelltext.
    void fail(const char* msg) { fail(WFromU8(msg).c_str()); }

    void fail(const wchar_t* msg) {
        if (res_.ok || !res_.error.empty()) return;
        res_.error = msg;
        size_t line = 1, col = 1;
        for (size_t i = 0; i < pos_ && i < s_.size(); ++i) {
            if (s_[i] == L'\n') { ++line; col = 1; } else { ++col; }
        }
        res_.line = line;
        res_.col = col;
    }
    bool eof() const { return pos_ >= s_.size(); }
    wchar_t cur() const { return pos_ < s_.size() ? s_[pos_] : 0; }

    void skipWs() {
        while (!eof()) {
            wchar_t c = cur();
            if (c == L' ' || c == L'\t' || c == L'\n' || c == L'\r') { ++pos_; continue; }
            // Zeilenumbrüche für BOM / Kommentare ignorieren wir nicht: JSON erlaubt keine Kommentare.
            break;
        }
    }

    bool parseValue(JsonPtr& out) {
        skipWs();
        if (eof()) { fail("Unerwartetes Dateiende"); return false; }
        wchar_t c = cur();
        if (c == L'{') return parseObject(out);
        if (c == L'[') return parseArray(out);
        if (c == L'"') { std::wstring v; if (!parseString(v)) return false; out = Json::MakeString(std::move(v)); return true; }
        if (c == L't') return parseLiteral(L"true", Json::MakeBool(true), out);
        if (c == L'f') return parseLiteral(L"false", Json::MakeBool(false), out);
        if (c == L'n') return parseLiteral(L"null", Json::MakeNull(), out);
        if (c == L'-' || (c >= L'0' && c <= L'9')) return parseNumber(out);
        fail("Unerwartetes Zeichen in JSON");
        return false;
    }

    bool parseLiteral(const wchar_t* lit, JsonPtr v, JsonPtr& out) {
        size_t n = wcslen(lit);
        if (pos_ + n > s_.size() || s_.compare(pos_, n, lit) != 0) { fail("Ungültiges Literal"); return false; }
        pos_ += n;
        out = v;
        return true;
    }

    bool parseNumber(JsonPtr& out) {
        size_t start = pos_;
        if (cur() == L'-') ++pos_;
        // JSON erlaubt keine führende Null ("01", "-05")
        if (!eof() && cur() == L'0' && pos_ + 1 < s_.size() && s_[pos_ + 1] >= L'0' && s_[pos_ + 1] <= L'9') {
            fail("Führende Null in Zahl");
            return false;
        }
        int digits = 0;
        while (!eof() && cur() >= L'0' && cur() <= L'9') { ++pos_; ++digits; }
        if (!digits) { fail("Ungültige Zahl"); return false; }
        if (!eof() && cur() == L'.') {
            ++pos_;
            int frac = 0;
            while (!eof() && cur() >= L'0' && cur() <= L'9') { ++pos_; ++frac; }
            if (!frac) { fail("Ungültige Zahl"); return false; }
        }
        if (!eof() && (cur() == L'e' || cur() == L'E')) {
            ++pos_;
            if (!eof() && (cur() == L'+' || cur() == L'-')) ++pos_;
            int exp = 0;
            while (!eof() && cur() >= L'0' && cur() <= L'9') { ++pos_; ++exp; }
            if (!exp) { fail("Ungültige Zahl"); return false; }
        }
        std::wstring tok = s_.substr(start, pos_ - start);
        out = Json::MakeNumber(_wcstod_l_wrapper(tok));
        return true;
    }

    static double _wcstod_l_wrapper(const std::wstring& tok) { return wcstod(tok.c_str(), nullptr); }

    bool parseString(std::wstring& out) {
        if (cur() != L'"') { fail("Erwartete Zeichenkette"); return false; }
        ++pos_;
        std::wstring v;
        while (true) {
            if (eof()) { fail("Zeichenkette nicht beendet"); return false; }
            wchar_t c = cur();
            if (c == L'"') { ++pos_; out = std::move(v); return true; }
            if (c == L'\\') {
                ++pos_;
                if (eof()) { fail("Escape nicht beendet"); return false; }
                wchar_t e = cur();
                ++pos_;
                switch (e) {
                case L'"': v += L'"'; break;
                case L'\\': v += L'\\'; break;
                case L'/': v += L'/'; break;
                case L'b': v += L'\b'; break;
                case L'f': v += L'\f'; break;
                case L'n': v += L'\n'; break;
                case L'r': v += L'\r'; break;
                case L't': v += L'\t'; break;
                case L'u': {
                    if (pos_ + 4 > s_.size()) { fail("Unvollständiger \\u-Escape"); return false; }
                    wchar_t hi = 0;
                    for (int i = 0; i < 4; ++i) {
                        wchar_t d = s_[pos_ + i];
                        int val;
                        if (d >= L'0' && d <= L'9') val = d - L'0';
                        else if (d >= L'a' && d <= L'f') val = 10 + d - L'a';
                        else if (d >= L'A' && d <= L'F') val = 10 + d - L'A';
                        else { fail("Ungültiger \\u-Escape"); return false; }
                        hi = static_cast<wchar_t>((hi << 4) | val);
                    }
                    pos_ += 4;
                    if (hi >= 0xD800 && hi < 0xDC00 && pos_ + 6 <= s_.size() &&
                        s_[pos_] == L'\\' && s_[pos_ + 1] == L'u') {
                        wchar_t lo = 0;
                        bool ok = true;
                        for (int i = 0; i < 4; ++i) {
                            wchar_t d = s_[pos_ + 2 + i];
                            int val;
                            if (d >= L'0' && d <= L'9') val = d - L'0';
                            else if (d >= L'a' && d <= L'f') val = 10 + d - L'a';
                            else if (d >= L'A' && d <= L'F') val = 10 + d - L'A';
                            else { ok = false; break; }
                            lo = static_cast<wchar_t>((lo << 4) | val);
                        }
                        if (ok && lo >= 0xDC00 && lo < 0xE000) {
                            v += hi; v += lo;
                            pos_ += 6;
                            break;
                        }
                    }
                    v += hi;
                    break;
                }
                default: fail("Unbekannter Escape"); return false;
                }
                continue;
            }
            if (c == L'\n') { fail("Zeilenumbruch in Zeichenkette"); return false; }
            v += c;
            ++pos_;
        }
    }

    bool parseArray(JsonPtr& out) {
        JsonPtr a = Json::MakeArray();
        ++pos_; // [
        skipWs();
        if (!eof() && cur() == L']') { ++pos_; out = a; return true; }
        while (true) {
            JsonPtr v;
            if (!parseValue(v)) return false;
            a->push(v);
            skipWs();
            if (eof()) { fail("Feld nicht abgeschlossen"); return false; }
            if (cur() == L',') { ++pos_; skipWs(); continue; }
            if (cur() == L']') { ++pos_; out = a; return true; }
            fail("Erwartete ',' oder ']'");
            return false;
        }
    }

    bool parseObject(JsonPtr& out) {
        JsonPtr o = Json::MakeObject();
        ++pos_; // {
        skipWs();
        if (!eof() && cur() == L'}') { ++pos_; out = o; return true; }
        while (true) {
            skipWs();
            std::wstring key;
            if (!parseString(key)) return false;
            skipWs();
            if (eof() || cur() != L':') { fail("Erwartete ':'"); return false; }
            ++pos_;
            JsonPtr v;
            if (!parseValue(v)) return false;
            o->set(key, v);
            skipWs();
            if (eof()) { fail("Objekt nicht abgeschlossen"); return false; }
            if (cur() == L',') { ++pos_; continue; }
            if (cur() == L'}') { ++pos_; out = o; return true; }
            fail("Erwartete ',' oder '}'");
            return false;
        }
    }
};

inline void escapeInto(std::wstring& out, const std::wstring& s) {
    out += L'"';
    for (wchar_t c : s) {
        switch (c) {
        case L'"':  out += L"\\\""; break;
        case L'\\': out += L"\\\\"; break;
        case L'\b': out += L"\\b"; break;
        case L'\f': out += L"\\f"; break;
        case L'\n': out += L"\\n"; break;
        case L'\r': out += L"\\r"; break;
        case L'\t': out += L"\\t"; break;
        default:
            if (c < 0x20) {
                wchar_t buf[8];
                swprintf(buf, 8, L"\\u%04x", static_cast<unsigned>(c));
                out += buf;
            } else {
                out += c;
            }
        }
    }
    out += L'"';
}

inline void serializeInto(std::wstring& out, const JsonPtr& v, bool pretty, int depth) {
    if (!v) { out += L"null"; return; }
    const wchar_t* nl = pretty ? L"\n" : L"";
    std::wstring pad;
    std::wstring padIn;
    if (pretty) {
        for (int i = 0; i < depth; ++i) pad += L"  ";
        for (int i = 0; i < depth + 1; ++i) padIn += L"  ";
    }
    switch (v->kind) {
    case JsonKind::Null:   out += L"null"; break;
    case JsonKind::Bool:   out += v->b ? L"true" : L"false"; break;
    case JsonKind::Number: {
        wchar_t buf[40];
        if (v->num == static_cast<long long>(v->num)) {
            swprintf(buf, 40, L"%lld", static_cast<long long>(v->num));
        } else {
            swprintf(buf, 40, L"%.10g", v->num);
        }
        out += buf;
        break;
    }
    case JsonKind::String: escapeInto(out, v->str); break;
    case JsonKind::Array: {
        if (v->arr.empty()) { out += L"[]"; break; }
        out += L'[';
        for (size_t i = 0; i < v->arr.size(); ++i) {
            if (i) out += L',';
            out += nl; out += padIn;
            serializeInto(out, v->arr[i], pretty, depth + 1);
        }
        out += nl; out += pad; out += L']';
        break;
    }
    case JsonKind::Object: {
        if (v->obj.empty()) { out += L"{}"; break; }
        out += L'{';
        for (size_t i = 0; i < v->obj.size(); ++i) {
            if (i) out += L',';
            out += nl; out += padIn;
            escapeInto(out, v->obj[i].first);
            out += pretty ? L": " : L":";
            serializeInto(out, v->obj[i].second, pretty, depth + 1);
        }
        out += nl; out += pad; out += L'}';
        break;
    }
    }
}

} // namespace jsondetail

inline JsonParseResult JsonParse(const std::wstring& text, JsonPtr& out) {
    jsondetail::Parser p(text);
    return p.parse(out);
}

inline std::wstring JsonSerialize(const JsonPtr& v, bool pretty) {
    std::wstring out;
    jsondetail::serializeInto(out, v, pretty, 0);
    return out;
}

} // namespace sr
