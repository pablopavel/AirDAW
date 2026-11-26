// Tiny JSON value/parser/writer (no dependencies) for session files.
#pragma once
#include <cstdio>
#include <cstdlib>
#include <string>
#include <utility>
#include <vector>

namespace airdaw {
struct JVal {
  enum Type { Null, Bool, Num, Str, Arr, Obj } type = Null;
  bool b = false; double n = 0; std::string s;
  std::vector<JVal> a; std::vector<std::pair<std::string, JVal>> o;
  static JVal num(double v) { JVal j; j.type = Num; j.n = v; return j; }
  static JVal boolean(bool v) { JVal j; j.type = Bool; j.b = v; return j; }
  static JVal str(std::string v) { JVal j; j.type = Str; j.s = std::move(v); return j; }
  static JVal arr() { JVal j; j.type = Arr; return j; }
  static JVal obj() { JVal j; j.type = Obj; return j; }
  JVal& set(const std::string& k, JVal v) { o.emplace_back(k, std::move(v)); return *this; }
  JVal& push(JVal v) { a.push_back(std::move(v)); return *this; }
  const JVal* get(const std::string& k) const { for (auto& p : o) if (p.first == k) return &p.second; return nullptr; }
  double num(const std::string& k, double d) const { auto* v = get(k); return v && v->type == Num ? v->n : d; }
  bool flag(const std::string& k, bool d) const { auto* v = get(k); return v && v->type == Bool ? v->b : d; }
  std::string text(const std::string& k, const std::string& d = {}) const { auto* v = get(k); return v && v->type == Str ? v->s : d; }

  void write(std::string& out, int ind = 0) const {
    auto pad = [&](int n) { out.append((size_t)n * 2, ' '); };
    switch (type) {
      case Null: out += "null"; break;
      case Bool: out += b ? "true" : "false"; break;
      case Num: { char buf[40]; std::snprintf(buf, sizeof buf, "%.9g", n); out += buf; break; }
      case Str: out += '"';
        for (char c : s) { switch (c) { case '"': out += "\\\""; break; case '\\': out += "\\\\"; break; case '\n': out += "\\n"; break;
          case '\r': out += "\\r"; break; case '\t': out += "\\t"; break; default: if ((unsigned char)c < 0x20) { char u[8]; std::snprintf(u, sizeof u, "\\u%04x", c); out += u; } else out += c; } }
        out += '"'; break;
      case Arr: out += '['; for (size_t i = 0; i < a.size(); ++i) { out += i ? ", " : ""; if (a[i].type == Obj || a[i].type == Arr) { out += '\n'; pad(ind + 1); } a[i].write(out, ind + 1); } out += ']'; break;
      case Obj: out += "{\n"; for (size_t i = 0; i < o.size(); ++i) { pad(ind + 1); JVal::str(o[i].first).write(out); out += ": "; o[i].second.write(out, ind + 1); out += i + 1 < o.size() ? ",\n" : "\n"; } pad(ind); out += '}'; break;
    }
  }
  std::string dump() const { std::string s; write(s); s += '\n'; return s; }

  static bool parse(const std::string& src, JVal& out) { size_t i = 0; if (!val(src, i, out, 0)) return false; ws(src, i); return i == src.size(); }
 private:
  static void ws(const std::string& s, size_t& i) { while (i < s.size() && (s[i] == ' ' || s[i] == '\n' || s[i] == '\r' || s[i] == '\t')) ++i; }
  static bool strv(const std::string& s, size_t& i, std::string& r) {
    if (s[i] != '"') return false; ++i; r.clear();
    while (i < s.size() && s[i] != '"') {
      if (s[i] == '\\' && i + 1 < s.size()) { ++i; char c = s[i];
        if (c == 'n') r += '\n'; else if (c == 'r') r += '\r'; else if (c == 't') r += '\t';
        else if (c == 'u' && i + 4 < s.size()) { r += (char)std::strtol(s.substr(i + 1, 4).c_str(), nullptr, 16); i += 4; } else r += c; }
      else r += s[i];
      ++i; }
    if (i >= s.size()) return false; ++i; return true;
  }
  static bool val(const std::string& s, size_t& i, JVal& v, int depth) {
    if (depth > 64) return false; ws(s, i); if (i >= s.size()) return false;
    char c = s[i];
    if (c == '{') { v = obj(); ++i; ws(s, i); if (i < s.size() && s[i] == '}') { ++i; return true; }
      for (;;) { ws(s, i); std::string k; if (i >= s.size() || !strv(s, i, k)) return false; ws(s, i); if (i >= s.size() || s[i] != ':') return false; ++i;
        JVal x; if (!val(s, i, x, depth + 1)) return false; v.o.emplace_back(k, std::move(x)); ws(s, i);
        if (i < s.size() && s[i] == ',') { ++i; continue; } if (i < s.size() && s[i] == '}') { ++i; return true; } return false; } }
    if (c == '[') { v = arr(); ++i; ws(s, i); if (i < s.size() && s[i] == ']') { ++i; return true; }
      for (;;) { JVal x; if (!val(s, i, x, depth + 1)) return false; v.a.push_back(std::move(x)); ws(s, i);
        if (i < s.size() && s[i] == ',') { ++i; continue; } if (i < s.size() && s[i] == ']') { ++i; return true; } return false; } }
    if (c == '"') { v.type = Str; return strv(s, i, v.s); }
    if (s.compare(i, 4, "true") == 0) { v = boolean(true); i += 4; return true; }
    if (s.compare(i, 5, "false") == 0) { v = boolean(false); i += 5; return true; }
    if (s.compare(i, 4, "null") == 0) { v = JVal(); i += 4; return true; }
    char* end; double d = std::strtod(s.c_str() + i, &end); if (end == s.c_str() + i) return false; i = end - s.c_str(); v = num(d); return true;
  }
};
}  // namespace airdaw
