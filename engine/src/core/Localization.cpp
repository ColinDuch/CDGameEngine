#include <engine/core/Localization.hpp>
#include <unordered_map>
#include <fstream>
#include <cstdarg>
#include <cstdio>
#include <vector>

namespace {
    std::unordered_map<std::string, std::string> table;
    std::string currentFile;
}

void Lang::load(const std::string& file) {
    table.clear();
    currentFile = file;
    if (file.empty()) return;                 // anglais : pas de table

    std::ifstream f(file);
    if (!f) return;                           // fichier absent -> anglais

    std::string line;
    while (std::getline(f, line)) {
        if (line.empty() || line[0] == '#') continue;
        size_t eq = line.find('=');
        if (eq == std::string::npos) continue;
        std::string k = line.substr(0, eq);
        std::string v = line.substr(eq + 1);
        if (!v.empty() && v.back() == '\r') v.pop_back();   // fichiers Windows
        table[k] = v;
    }
}

const char* Lang::tr(const char* key) {
    auto it = table.find(key);
    return (it != table.end()) ? it->second.c_str() : key;
}

std::string Lang::fmt(const char* key, ...) {
    const char* form = tr(key);

    va_list args;
    va_start(args, key);
    va_list probe;
    va_copy(probe, args);
    const int n = std::vsnprintf(nullptr, 0, form, probe);
    va_end(probe);

    std::string out;
    if (n > 0) {
        std::vector<char> buf(static_cast<size_t>(n) + 1);
        std::vsnprintf(buf.data(), buf.size(), form, args);
        out.assign(buf.data(), static_cast<size_t>(n));
    }
    va_end(args);
    return out;
}

const std::string& Lang::current() { return currentFile; }