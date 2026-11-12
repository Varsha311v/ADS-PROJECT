#include "json.hpp"
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <utility>

namespace ff::json {
namespace {

void appendUtf8(std::string& out, unsigned cp) {
    if (cp < 0x80) {
        out += static_cast<char>(cp);
    } else if (cp < 0x800) {
        out += static_cast<char>(0xC0 | (cp >> 6));
        out += static_cast<char>(0x80 | (cp & 0x3F));
    } else if (cp < 0x10000) {
        out += static_cast<char>(0xE0 | (cp >> 12));
        out += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
        out += static_cast<char>(0x80 | (cp & 0x3F));
    } else {
        out += static_cast<char>(0xF0 | (cp >> 18));
        out += static_cast<char>(0x80 | ((cp >> 12) & 0x3F));
        out += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
        out += static_cast<char>(0x80 | (cp & 0x3F));
    }
}

class Parser {
public:
    explicit Parser(const std::string& s) : s_(s) {}

    std::optional<Value> run() {
        ws();
        auto v = value(0);

        if (!v) return std::nullopt;

        ws();

        if (i_ != s_.size()) return std::nullopt;

        return v;
    }

private:
    const std::string& s_;
    size_t i_ = 0;

    void ws() {
        while (i_ < s_.size() &&
              (s_[i_] == ' ' || s_[i_] == '\t' ||
               s_[i_] == '\n' || s_[i_] == '\r')) {
            ++i_;
        }
    }

    bool lit(const char* w) {
        size_t n = 0;

        while (w[n]) ++n;

        if (s_.compare(i_, n, w) != 0) return false;

        i_ += n;
        return true;
    }

    std::optional<unsigned> hex4() {
        if (i_ + 4 > s_.size()) return std::nullopt;

        unsigned v = 0;

        for (int k = 0; k < 4; ++k) {
            char c = s_[i_++];
            v <<= 4;

            if (c >= '0' && c <= '9')
                v |= c - '0';
            else if (c >= 'a' && c <= 'f')
                v |= c - 'a' + 10;
            else if (c >= 'A' && c <= 'F')
                v |= c - 'A' + 10;
            else
                return std::nullopt;
        }

        return v;
    }

    std::optional<std::string> str() {
        if (i_ >= s_.size() || s_[i_] != '"')
            return std::nullopt;

        ++i_;
        std::string out;

        while (i_ < s_.size()) {
            unsigned char c = s_[i_++];

            if (c == '"') return out;
            if (c < 0x20) return std::nullopt;

            if (c != '\\') {
                out += static_cast<char>(c);
                continue;
            }

            if (i_ >= s_.size()) return std::nullopt;

            char e = s_[i_++];

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
                    auto u = hex4();
                    if (!u) return std::nullopt;

                    unsigned cp = *u;

                    if (cp >= 0xD800 && cp <= 0xDBFF) {
                        if (!(i_ + 1 < s_.size() &&
                              s_[i_] == '\\' &&
                              s_[i_ + 1] == 'u')) {
                            return std::nullopt;
                        }

                        i_ += 2;

                        auto lo = hex4();

                        if (!lo || *lo < 0xDC00 || *lo > 0xDFFF)
                            return std::nullopt;

                        cp = 0x10000 +
                             ((cp - 0xD800) << 10) +
                             (*lo - 0xDC00);

                    } else if (cp >= 0xDC00 && cp <= 0xDFFF) {
                        return std::nullopt;
                    }

                    appendUtf8(out, cp);
                    break;
                }

                default:
                    return std::nullopt;
            }
        }

        return std::nullopt;
    }

    std::optional<Value> value(int depth) {
        if (depth > 32 || i_ >= s_.size())
            return std::nullopt;

        char c = s_[i_];

        if (c == '{') {
            ++i_;
            Value o = Value::object();
            ws();

            if (i_ < s_.size() && s_[i_] == '}') {
                ++i_;
                return o;
            }

            while (true) {
                ws();

                auto k = str();
                if (!k) return std::nullopt;

                ws();

                if (i_ >= s_.size() || s_[i_++] != ':')
                    return std::nullopt;

                ws();

                auto v = value(depth + 1);
                if (!v) return std::nullopt;

                o.set(std::move(*k), std::move(*v));

                ws();

                if (i_ >= s_.size()) return std::nullopt;

                if (s_[i_] == ',') {
                    ++i_;
                    continue;
                }

                if (s_[i_] == '}') {
                    ++i_;
                    return o;
                }

                return std::nullopt;
            }
        }

        if (c == '[') {
            ++i_;
            Value a = Value::array();
            ws();

            if (i_ < s_.size() && s_[i_] == ']') {
                ++i_;
                return a;
            }

            while (true) {
                ws();

                auto v = value(depth + 1);
                if (!v) return std::nullopt;

                a.push(std::move(*v));

                ws();

                if (i_ >= s_.size()) return std::nullopt;

                if (s_[i_] == ',') {
                    ++i_;
                    continue;
                }

                if (s_[i_] == ']') {
                    ++i_;
                    return a;
                }

                return std::nullopt;
            }
        }

        if (c == '"') {
            auto s = str();
            if (!s) return std::nullopt;
            return Value::string(std::move(*s));
        }

        if (lit("true")) return Value::boolean(true);
        if (lit("false")) return Value::boolean(false);
        if (lit("null")) return Value::null();

        size_t start = i_;

        if (s_[i_] == '-') ++i_;

        if (i_ >= s_.size() ||
            !(s_[i_] >= '0' && s_[i_] <= '9')) {
            return std::nullopt;
        }

        while (i_ < s_.size() &&
              ((s_[i_] >= '0' && s_[i_] <= '9') ||
               s_[i_] == '.' || s_[i_] == 'e' ||
               s_[i_] == 'E' || s_[i_] == '+' ||
               s_[i_] == '-')) {
            ++i_;
        }

        char* end = nullptr;
        std::string tok = s_.substr(start, i_ - start);

        double d = std::strtod(tok.c_str(), &end);

        if (end == tok.c_str() || *end != '\0' ||
            !std::isfinite(d)) {
            return std::nullopt;
        }

        return Value::number(d);
    }
};

void dumpTo(const Value& v, std::string& out) {
    switch (v.type) {
        case Value::Type::Null:
            out += "null";
            break;

        case Value::Type::Bool:
            out += v.b ? "true" : "false";
            break;

        case Value::Type::Number: {
            char buf[32];

            if (v.num == std::floor(v.num) &&
                std::fabs(v.num) < 1e15) {
                std::snprintf(buf, sizeof buf, "%.0f", v.num);
            } else {
                std::snprintf(buf, sizeof buf, "%.17g", v.num);
            }

            out += buf;
            break;
        }

        case Value::Type::String:
            out += escape(v.str);
            break;

        case Value::Type::Array:
            out += '[';

            for (size_t i = 0; i < v.arr.size(); ++i) {
                if (i) out += ',';
                dumpTo(v.arr[i], out);
            }

            out += ']';
            break;

        case Value::Type::Object:
            out += '{';

            for (size_t i = 0; i < v.obj.size(); ++i) {
                if (i) out += ',';

                out += escape(v.obj[i].first);
                out += ':';

                dumpTo(v.obj[i].second, out);
            }

            out += '}';
            break;
    }
}

} // namespace

std::optional<Value> parse(const std::string& text) {
    return Parser(text).run();
}

std::string escape(const std::string& s) {
    std::string out = "\"";

    for (unsigned char c : s) {
        switch (c) {
            case '"': out += "\\\""; break;
            case '\\': out += "\\\\"; break;
            case '\b': out += "\\b"; break;
            case '\f': out += "\\f"; break;
            case '\n': out += "\\n"; break;
            case '\r': out += "\\r"; break;
            case '\t': out += "\\t"; break;

            default:
                if (c < 0x20) {
                    char buf[8];
                    std::snprintf(buf, sizeof buf, "\\u%04x", c);
                    out += buf;
                } else {
                    out += static_cast<char>(c);
                }
        }
    }

    out += '"';
    return out;
}

std::string Value::dump() const {
    std::string out;
    dumpTo(*this, out);
    return out;
}

} // namespace ff::json
