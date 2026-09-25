#include <engine/renderer/MeshCache.hpp>

#include <algorithm>
#include <cctype>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <stdexcept>
#include <cstdio>
#include <cstdlib>

namespace {

constexpr char     kMagic[4] = { 'E', 'M', 'S', 'H' };
constexpr uint32_t kVersion  = 1;

void writeU32(std::ostream& os, uint32_t v) {
    os.write(reinterpret_cast<const char*>(&v), sizeof(v));
}
uint32_t readU32(std::istream& is) {
    uint32_t v = 0;
    is.read(reinterpret_cast<char*>(&v), sizeof(v));
    if (!is) throw std::runtime_error("MeshCache : fichier tronque");
    return v;
}
void writeF32(std::ostream& os, float v) {
    os.write(reinterpret_cast<const char*>(&v), sizeof(v));
}
float readF32(std::istream& is) {
    float v = 0.0f;
    is.read(reinterpret_cast<char*>(&v), sizeof(v));
    if (!is) throw std::runtime_error("MeshCache : fichier tronque");
    return v;
}
void writeStr(std::ostream& os, const std::string& s) {
    writeU32(os, static_cast<uint32_t>(s.size()));
    os.write(s.data(), static_cast<std::streamsize>(s.size()));
}
std::string readStr(std::istream& is) {
    uint32_t n = readU32(is);
    if (n > 65535u) throw std::runtime_error("MeshCache : chaine invalide");
    std::string s(n, '\0');
    if (n) is.read(&s[0], static_cast<std::streamsize>(n));
    if (!is) throw std::runtime_error("MeshCache : fichier tronque");
    return s;
}

std::filesystem::path cacheRoot() {
    const char* home = std::getenv("HOME");
    if (!home) home = std::getenv("USERPROFILE");   // Windows
    std::filesystem::path root = home ? (std::filesystem::path(home) / ".cache")
                                      : std::filesystem::path(".");
    return root / "GameEngineCpp" / "meshes";
}

uint64_t fnv1a(const std::string& s) {
    uint64_t h = 1469598103934665603ull;
    for (unsigned char c : s) { h ^= c; h *= 1099511628211ull; }
    return h;
}

} // namespace

bool MeshCache::isCacheFile(const std::string& path) {
    std::string ext = std::filesystem::path(path).extension().string();
    std::transform(ext.begin(), ext.end(), ext.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return ext == ".emesh";
}

void MeshCache::save(const std::string& path, const MeshCacheData& data) {
    std::filesystem::path p(path);
    std::error_code ec;
    if (p.has_parent_path()) std::filesystem::create_directories(p.parent_path(), ec);

    std::ofstream f(path, std::ios::binary);
    if (!f) throw std::runtime_error("MeshCache : ecriture impossible : " + path);

    f.write(kMagic, 4);
    writeU32(f, kVersion);
    writeU32(f, static_cast<uint32_t>(sizeof(Vertex)));   // garde anti-cache-obsolete
    writeU32(f, static_cast<uint32_t>(data.vertices.size()));
    writeU32(f, static_cast<uint32_t>(data.indices.size()));
    writeU32(f, static_cast<uint32_t>(data.subMeshes.size()));
    writeU32(f, data.hasUV ? 1u : 0u);

    if (!data.vertices.empty())
        f.write(reinterpret_cast<const char*>(data.vertices.data()),
                static_cast<std::streamsize>(data.vertices.size() * sizeof(Vertex)));
    if (!data.indices.empty())
        f.write(reinterpret_cast<const char*>(data.indices.data()),
                static_cast<std::streamsize>(data.indices.size() * sizeof(uint32_t)));

    for (const auto& s : data.subMeshes) {
        writeU32(f, s.indexOffset);
        writeU32(f, s.indexCount);
        writeF32(f, s.baseColor.x);
        writeF32(f, s.baseColor.y);
        writeF32(f, s.baseColor.z);
        writeF32(f, s.metallic);
        writeF32(f, s.roughness);
        writeU32(f, s.referencesTexture ? 1u : 0u);
        writeStr(f, s.diffusePath);
        writeStr(f, s.normalPath);
    }

    if (!f) throw std::runtime_error("MeshCache : ecriture incomplete : " + path);
}

MeshCacheData MeshCache::load(const std::string& path) {
    std::ifstream f(path, std::ios::binary);
    if (!f) throw std::runtime_error("MeshCache : lecture impossible : " + path);

    char magic[4] = {};
    f.read(magic, 4);
    if (!f || std::memcmp(magic, kMagic, 4) != 0)
        throw std::runtime_error("MeshCache : ce n'est pas un .emesh : " + path);

    uint32_t version = readU32(f);
    if (version != kVersion)
        throw std::runtime_error("MeshCache : version " + std::to_string(version) + " non geree");

    uint32_t stride = readU32(f);
    if (stride != sizeof(Vertex))
        throw std::runtime_error("MeshCache : la structure Vertex a change, cache obsolete");

    uint32_t vCount = readU32(f);
    uint32_t iCount = readU32(f);
    uint32_t sCount = readU32(f);

    MeshCacheData data;
    data.hasUV = (readU32(f) != 0);
    data.vertices.resize(vCount);
    data.indices.resize(iCount);

    if (vCount) f.read(reinterpret_cast<char*>(data.vertices.data()),
                       static_cast<std::streamsize>(vCount * sizeof(Vertex)));
    if (iCount) f.read(reinterpret_cast<char*>(data.indices.data()),
                       static_cast<std::streamsize>(iCount * sizeof(uint32_t)));
    if (!f) throw std::runtime_error("MeshCache : fichier tronque : " + path);

    for (uint32_t k = 0; k < sCount; ++k) {
        MeshCacheSubMesh s;
        s.indexOffset = readU32(f);
        s.indexCount  = readU32(f);
        s.baseColor.x = readF32(f);
        s.baseColor.y = readF32(f);
        s.baseColor.z = readF32(f);
        s.metallic    = readF32(f);
        s.roughness   = readF32(f);
        s.referencesTexture = (readU32(f) != 0);
        s.diffusePath = readStr(f);
        s.normalPath  = readStr(f);
        data.subMeshes.push_back(std::move(s));
    }

    // Filet de securite : jamais zero sous-mesh.
    if (data.subMeshes.empty()) {
        MeshCacheSubMesh s;
        s.indexCount = iCount;
        data.subMeshes.push_back(s);
    }
    return data;
}

std::string MeshCache::cachePathFor(const std::string& sourcePath) {
    std::error_code ec;
    std::filesystem::path src = std::filesystem::absolute(sourcePath, ec);
    if (ec) src = std::filesystem::path(sourcePath);

    // Le hash du chemin complet evite que deux "scene.fbx" differents se marchent dessus.
    char hex[17];
    std::snprintf(hex, sizeof(hex), "%016llx",
                  static_cast<unsigned long long>(fnv1a(src.string())));

    std::string stem = src.stem().string();
    for (char& c : stem) {
        if (c == '/' || c == '\\' || c == ':' || c == ' ') c = '_';
    }
    if (stem.empty()) stem = "mesh";

    return (cacheRoot() / (stem + "_" + std::string(hex) + ".emesh")).string();
}

bool MeshCache::isFresh(const std::string& cachePath, const std::string& sourcePath) {
    std::error_code ec;
    if (!std::filesystem::exists(cachePath, ec)) return false;
    if (!std::filesystem::exists(sourcePath, ec)) return true;   // source disparue : on garde

    auto tc = std::filesystem::last_write_time(cachePath, ec);
    if (ec) return false;
    auto ts = std::filesystem::last_write_time(sourcePath, ec);
    if (ec) return false;

    return tc >= ts;
}