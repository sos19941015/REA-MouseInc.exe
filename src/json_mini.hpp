#pragma once
#include <string>
#include <vector>
#include <map>
#include <sstream>
#include <iomanip>
#include <cmath>
#include <cctype>

namespace mini_json {

enum class Type { Null, Bool, Number, String, Array, Object };

struct Value {
    Type type = Type::Null;
    bool bool_val = false;
    double num_val = 0.0;
    std::string str_val;
    std::vector<Value> arr_val;
    std::map<std::string, Value> obj_val;

    Value() = default;
    Value(bool b) : type(Type::Bool), bool_val(b) {}
    Value(double n) : type(Type::Number), num_val(n) {}
    Value(int n) : type(Type::Number), num_val((double)n) {}
    Value(const std::string& s) : type(Type::String), str_val(s) {}
    Value(const char* s) : type(Type::String), str_val(s ? s : "") {}

    bool is_null() const { return type == Type::Null; }
    bool is_bool() const { return type == Type::Bool; }
    bool is_number() const { return type == Type::Number; }
    bool is_string() const { return type == Type::String; }
    bool is_array() const { return type == Type::Array; }
    bool is_object() const { return type == Type::Object; }

    bool get_bool(bool def = false) const {
        return type == Type::Bool ? bool_val : def;
    }
    int get_int(int def = 0) const {
        return type == Type::Number ? (int)std::llround(num_val) : def;
    }
    double get_double(double def = 0.0) const {
        return type == Type::Number ? num_val : def;
    }
    std::string get_string(const std::string& def = "") const {
        return type == Type::String ? str_val : def;
    }

    const Value& operator[](const std::string& key) const {
        static const Value kNull;
        if (type != Type::Object) return kNull;
        auto it = obj_val.find(key);
        return it != obj_val.end() ? it->second : kNull;
    }
    Value& operator[](const std::string& key) {
        if (type != Type::Object) {
            type = Type::Object;
            obj_val.clear();
        }
        return obj_val[key];
    }
    bool contains(const std::string& key) const {
        return type == Type::Object && obj_val.find(key) != obj_val.end();
    }
    const Value& operator[](size_t idx) const {
        static const Value kNull;
        if (type != Type::Array || idx >= arr_val.size()) return kNull;
        return arr_val[idx];
    }
    size_t size() const {
        if (type == Type::Array) return arr_val.size();
        if (type == Type::Object) return obj_val.size();
        return 0;
    }
};

class Parser {
    const std::string& s_;
    size_t pos_ = 0;

    void skip_ws() {
        while (pos_ < s_.size()) {
            unsigned char c = (unsigned char)s_[pos_];
            if (c == ' ' || c == '\t' || c == '\r' || c == '\n') {
                pos_++;
            } else if (c == '/' && pos_ + 1 < s_.size() && s_[pos_ + 1] == '/') {
                pos_ += 2;
                while (pos_ < s_.size() && s_[pos_] != '\n') pos_++;
            } else if (c == '/' && pos_ + 1 < s_.size() && s_[pos_ + 1] == '*') {
                pos_ += 2;
                while (pos_ + 1 < s_.size() && !(s_[pos_] == '*' && s_[pos_ + 1] == '/')) pos_++;
                if (pos_ + 1 < s_.size()) pos_ += 2;
            } else {
                break;
            }
        }
    }

    static void append_utf8(std::string& out, uint32_t cp) {
        if (cp <= 0x7F) {
            out.push_back((char)cp);
        } else if (cp <= 0x7FF) {
            out.push_back((char)(0xC0 | ((cp >> 6) & 0x1F)));
            out.push_back((char)(0x80 | (cp & 0x3F)));
        } else if (cp <= 0xFFFF) {
            out.push_back((char)(0xE0 | ((cp >> 12) & 0x0F)));
            out.push_back((char)(0x80 | ((cp >> 6) & 0x3F)));
            out.push_back((char)(0x80 | (cp & 0x3F)));
        } else {
            out.push_back((char)(0xF0 | ((cp >> 18) & 0x07)));
            out.push_back((char)(0x80 | ((cp >> 12) & 0x3F)));
            out.push_back((char)(0x80 | ((cp >> 6) & 0x3F)));
            out.push_back((char)(0x80 | (cp & 0x3F)));
        }
    }

    uint32_t parse_hex4() {
        uint32_t val = 0;
        for (int i = 0; i < 4 && pos_ < s_.size(); i++, pos_++) {
            char c = s_[pos_];
            val <<= 4;
            if (c >= '0' && c <= '9') val |= (c - '0');
            else if (c >= 'a' && c <= 'f') val |= (c - 'a' + 10);
            else if (c >= 'A' && c <= 'F') val |= (c - 'A' + 10);
        }
        return val;
    }

    std::string parse_string() {
        std::string out;
        if (pos_ >= s_.size() || s_[pos_] != '"') return out;
        pos_++;
        while (pos_ < s_.size()) {
            char c = s_[pos_++];
            if (c == '"') break;
            if (c == '\\' && pos_ < s_.size()) {
                char esc = s_[pos_++];
                switch (esc) {
                    case '"': out.push_back('"'); break;
                    case '\\': out.push_back('\\'); break;
                    case '/': out.push_back('/'); break;
                    case 'b': out.push_back('\b'); break;
                    case 'f': out.push_back('\f'); break;
                    case 'n': out.push_back('\n'); break;
                    case 'r': out.push_back('\r'); break;
                    case 't': out.push_back('\t'); break;
                    case 'u': {
                        uint32_t cp = parse_hex4();
                        if (cp >= 0xD800 && cp <= 0xDBFF && pos_ + 1 < s_.size() && s_[pos_] == '\\' && s_[pos_ + 1] == 'u') {
                            pos_ += 2;
                            uint32_t low = parse_hex4();
                            if (low >= 0xDC00 && low <= 0xDFFF) {
                                cp = 0x10000 + (((cp - 0xD800) << 10) | (low - 0xDC00));
                            }
                        }
                        append_utf8(out, cp);
                        break;
                    }
                    default: out.push_back(esc); break;
                }
            } else {
                out.push_back(c);
            }
        }
        return out;
    }

public:
    explicit Parser(const std::string& s) : s_(s) {
        // Skip UTF-8 BOM if present
        if (s_.size() >= 3 && (unsigned char)s_[0] == 0xEF && (unsigned char)s_[1] == 0xBB && (unsigned char)s_[2] == 0xBF) {
            pos_ = 3;
        }
    }

    Value parse_value() {
        skip_ws();
        if (pos_ >= s_.size()) return Value();
        char c = s_[pos_];
        if (c == '"') {
            return Value(parse_string());
        } else if (c == '{') {
            Value v;
            v.type = Type::Object;
            pos_++;
            skip_ws();
            if (pos_ < s_.size() && s_[pos_] == '}') {
                pos_++;
                return v;
            }
            while (pos_ < s_.size()) {
                skip_ws();
                std::string key = parse_string();
                skip_ws();
                if (pos_ < s_.size() && s_[pos_] == ':') pos_++;
                v.obj_val[key] = parse_value();
                skip_ws();
                if (pos_ < s_.size() && s_[pos_] == ',') {
                    pos_++;
                } else if (pos_ < s_.size() && s_[pos_] == '}') {
                    pos_++;
                    break;
                } else {
                    break;
                }
            }
            return v;
        } else if (c == '[') {
            Value v;
            v.type = Type::Array;
            pos_++;
            skip_ws();
            if (pos_ < s_.size() && s_[pos_] == ']') {
                pos_++;
                return v;
            }
            while (pos_ < s_.size()) {
                v.arr_val.push_back(parse_value());
                skip_ws();
                if (pos_ < s_.size() && s_[pos_] == ',') {
                    pos_++;
                } else if (pos_ < s_.size() && s_[pos_] == ']') {
                    pos_++;
                    break;
                } else {
                    break;
                }
            }
            return v;
        } else if (c == 't' && s_.compare(pos_, 4, "true") == 0) {
            pos_ += 4;
            return Value(true);
        } else if (c == 'f' && s_.compare(pos_, 5, "false") == 0) {
            pos_ += 5;
            return Value(false);
        } else if (c == 'n' && s_.compare(pos_, 4, "null") == 0) {
            pos_ += 4;
            return Value();
        } else {
            size_t start = pos_;
            if (s_[pos_] == '-' || s_[pos_] == '+') pos_++;
            while (pos_ < s_.size() && (std::isdigit((unsigned char)s_[pos_]) || s_[pos_] == '.' || s_[pos_] == 'e' || s_[pos_] == 'E' || s_[pos_] == '+' || s_[pos_] == '-')) {
                pos_++;
            }
            double num = 0.0;
            try {
                num = std::stod(s_.substr(start, pos_ - start));
            } catch (...) {}
            return Value(num);
        }
    }
};

inline Value parse(const std::string& json_text) {
    Parser p(json_text);
    return p.parse_value();
}

inline void escape_string(std::ostringstream& os, const std::string& s) {
    os << '"';
    for (unsigned char c : s) {
        switch (c) {
            case '"': os << "\\\""; break;
            case '\\': os << "\\\\"; break;
            case '\b': os << "\\b"; break;
            case '\f': os << "\\f"; break;
            case '\n': os << "\\n"; break;
            case '\r': os << "\\r"; break;
            case '\t': os << "\\t"; break;
            default:
                if (c < 0x20) {
                    os << "\\u" << std::hex << std::setw(4) << std::setfill('0') << (int)c << std::dec;
                } else {
                    os << (char)c;
                }
                break;
        }
    }
    os << '"';
}

inline void serialize_impl(std::ostringstream& os, const Value& v, int indent, int depth) {
    std::string pad(depth * (indent > 0 ? 1 : 0), '\t');
    std::string child_pad((depth + 1) * (indent > 0 ? 1 : 0), '\t');
    switch (v.type) {
        case Type::Null: os << "null"; break;
        case Type::Bool: os << (v.bool_val ? "true" : "false"); break;
        case Type::Number: {
            if (std::floor(v.num_val) == v.num_val && std::fabs(v.num_val) < 1e15) {
                os << (long long)v.num_val;
            } else {
                os << v.num_val;
            }
            break;
        }
        case Type::String: escape_string(os, v.str_val); break;
        case Type::Array: {
            if (v.arr_val.empty()) {
                os << "[]";
                break;
            }
            bool all_primitive = true;
            for (const auto& item : v.arr_val) {
                if (item.is_array() || item.is_object()) {
                    all_primitive = false;
                    break;
                }
            }
            if (all_primitive || indent <= 0) {
                os << "[";
                for (size_t i = 0; i < v.arr_val.size(); i++) {
                    if (i > 0) os << (indent > 0 ? ", " : ",");
                    serialize_impl(os, v.arr_val[i], indent, depth + 1);
                }
                os << "]";
            } else {
                os << "[\n";
                for (size_t i = 0; i < v.arr_val.size(); i++) {
                    os << child_pad;
                    serialize_impl(os, v.arr_val[i], indent, depth + 1);
                    if (i + 1 < v.arr_val.size()) os << ",";
                    os << "\n";
                }
                os << pad << "]";
            }
            break;
        }
        case Type::Object: {
            if (v.obj_val.empty()) {
                os << "{}";
                break;
            }
            os << (indent > 0 ? "{\n" : "{");
            size_t i = 0;
            for (const auto& kv : v.obj_val) {
                if (indent > 0) os << child_pad;
                escape_string(os, kv.first);
                os << (indent > 0 ? ": " : ":");
                serialize_impl(os, kv.second, indent, depth + 1);
                if (i + 1 < v.obj_val.size()) os << ",";
                if (indent > 0) os << "\n";
                i++;
            }
            if (indent > 0) os << pad;
            os << "}";
            break;
        }
    }
}

inline std::string dump(const Value& v, int indent = 1) {
    std::ostringstream os;
    serialize_impl(os, v, indent, 0);
    return os.str();
}

} // namespace mini_json
