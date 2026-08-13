#pragma once

#include <string>
#include <unordered_map>
#include <fstream>

namespace chatroom {

// 极简 INI 解析器：支持 [section] / key = value / # 注释，header-only，无第三方依赖
class Config {
public:
    bool load(const std::string& path) {
        std::ifstream in(path);
        if (!in.is_open()) return false;   // 文件不存在：调用方回退默认值
        kv_.clear();
        std::string section, line;
        while (std::getline(in, line)) {
            line = trim(stripComment(line));
            if (line.empty()) continue;
            if (line.front() == '[' && line.back() == ']') {
                section = trim(line.substr(1, line.size() - 2));
                continue;
            }
            auto pos = line.find('=');
            if (pos == std::string::npos) continue;
            std::string key = trim(line.substr(0, pos));
            std::string val = trim(line.substr(pos + 1));
            if (key.empty()) continue;
            kv_[section + "." + key] = val;
        }
        return true;
    }

    std::string get(const std::string& section, const std::string& key,
                    const std::string& def = "") const {
        auto it = kv_.find(section + "." + key);
        return it == kv_.end() ? def : it->second;
    }

    int getInt(const std::string& section, const std::string& key, int def = 0) const {
        auto it = kv_.find(section + "." + key);
        if (it == kv_.end() || it->second.empty()) return def;
        try { return std::stoi(it->second); } catch (...) { return def; }
    }

private:
    std::unordered_map<std::string, std::string> kv_;

    static std::string trim(const std::string& s) {
        auto b = s.find_first_not_of(" \t\r\n");
        if (b == std::string::npos) return "";
        auto e = s.find_last_not_of(" \t\r\n");
        return s.substr(b, e - b + 1);
    }

    static std::string stripComment(const std::string& s) {
        auto p = s.find('#');
        return p == std::string::npos ? s : s.substr(0, p);
    }
};

} // namespace chatroom
