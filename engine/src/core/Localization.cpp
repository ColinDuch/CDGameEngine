#include <engine/core/Localization.hpp>
#include <unordered_map>
#include <fstream>

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

const std::string& Lang::current() { return currentFile; }