#pragma once
// ide_asset_database.h  —  HonHon Engine IDE  —  Asset Database
// =============================================================================
// Covers:
//   - Asset record with GUID / type / import settings
//   - Import settings per asset type (texture, model, audio, script)
//   - Bulk rename, duplicate, delete
//   - Reference / dependency tracking (binary links via GUID)
//   - Sub-objects inside a compound asset (meshes, animations)
//   - Script import with recompilation flag
//   - Database save / load (.honassets sidecar files)
//   - Extended type system: 100+ file extensions across 20 categories
// =============================================================================

#include <string>
#include <vector>
#include <unordered_map>
#include <unordered_set>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <cstdint>
#include <cstring>
#include <random>
#include <algorithm>
#include <imgui.h>

namespace fs = std::filesystem;

// ─────────────────────────────────────────────────────────────────────────────
//  GUID — 128-bit unique identifier, stored as two uint64_t
// ─────────────────────────────────────────────────────────────────────────────
struct AssetGUID {
    uint64_t hi = 0, lo = 0;

    bool operator==(const AssetGUID& o) const { return hi == o.hi && lo == o.lo; }
    bool operator!=(const AssetGUID& o) const { return !(*this == o); }
    bool IsNull() const { return hi == 0 && lo == 0; }

    std::string ToString() const {
        char buf[36];
        // Format: xxxxxxxx-xxxx-xxxx-xxxx-xxxxxxxxxxxx  (simplified 64+64)
        std::snprintf(buf, sizeof(buf), "%016llx-%016llx",
            (unsigned long long)hi, (unsigned long long)lo);
        return buf;
    }

    static AssetGUID Generate() {
        static std::mt19937_64 rng(std::random_device{}());
        AssetGUID g; g.hi = rng(); g.lo = rng();
        return g;
    }
    static AssetGUID FromString(const std::string& s) {
        AssetGUID g;
        if (s.size() < 33) return g;
        try {
            g.hi = std::stoull(s.substr(0, 16), nullptr, 16);
            g.lo = std::stoull(s.substr(17, 16), nullptr, 16);
        }
        catch (...) {}
        return g;
    }
};

// ─────────────────────────────────────────────────────────────────────────────
//  Asset types
//  NOTE: Do NOT reorder or remove existing values — they are serialised as
//  integers in .honassets files.  Append new values only (before _Count).
// ─────────────────────────────────────────────────────────────────────────────
enum class AssetType {
    // ── Original types (v1) — must stay at these indices ──
    Unknown = 0,
    Texture = 1,
    Model = 2,   // .obj / .gltf / .glb / .fbx / .dae
    Audio = 3,
    Script = 4,   // .cpp / .lua / .py / .js / .ts …
    Scene = 5,   // .honscene
    Material = 6,   // .honmat
    Prefab = 7,   // .honprefab
    Font = 8,   // .ttf / .otf / .woff …
    ShaderSource = 9,   // .vert / .frag / .glsl / .hlsl …
    Shader = 10,  // .honshader
    Other = 11,
    // ── Extended types (v2) — appended; do not reorder above ──
    TextDocument = 12,  // .txt / .md / .rst / .log / .csv …
    Header = 13,  // .h / .hpp / .hxx / .inl …
    Config = 14,  // .json / .yaml / .toml / .ini / .xml …
    Archive = 15,  // .zip / .rar / .7z / .tar / .gz …
    Executable = 16,  // .exe / .dll / .so / .dylib / .bin …
    Video = 17,  // .mp4 / .avi / .mov / .mkv …
    Document = 18,  // .pdf / .docx / .xlsx / .odt …
    CAD = 19,  // .blend / .stl / .step / .3ds / .vox …
};

inline AssetType ExtToAssetType(const std::string& ext) {
    std::string e = ext;
    for (auto& c : e) c = (char)std::tolower((unsigned char)c);

    // ── Textures ──────────────────────────────────────────────────────────
    if (e == ".png" || e == ".jpg" || e == ".jpeg" || e == ".hdr" ||
        e == ".gif" || e == ".apng" || e == ".webp" || e == ".tiff" ||
        e == ".tif" || e == ".dds" || e == ".ktx" || e == ".pvr" ||
        e == ".astc" || e == ".psd" || e == ".xcf" || e == ".kra" ||
        e == ".svg" || e == ".eps" || e == ".ai" || e == ".cdr" ||
        e == ".raw" || e == ".cr2" || e == ".nef" || e == ".arw" ||
        e == ".dng")
        return AssetType::Texture;

    // Textures that also matched in v1 (kept here for clarity)
    if (e == ".bmp" || e == ".tga" || e == ".exr")
        return AssetType::Texture;

    // ── Models / 3D (non-CAD) ─────────────────────────────────────────────
    if (e == ".obj" || e == ".gltf" || e == ".glb" ||
        e == ".fbx" || e == ".dae")
        return AssetType::Model;

    // ── CAD / DCC ─────────────────────────────────────────────────────────
    if (e == ".blend" || e == ".3ds" || e == ".stl" || e == ".step" ||
        e == ".stp" || e == ".iges" || e == ".igs" || e == ".vrm" ||
        e == ".vox")
        return AssetType::CAD;

    // ── Audio ─────────────────────────────────────────────────────────────
    if (e == ".wav" || e == ".mp3" || e == ".ogg" || e == ".aiff" ||
        e == ".aac" || e == ".m4a" || e == ".opus" || e == ".mid" ||
        e == ".midi" || e == ".xm" || e == ".mod" || e == ".it" ||
        e == ".s3m" || e == ".flac" || e == ".wma" || e == ".dsd" ||
        e == ".dff" || e == ".dsf")
        return AssetType::Audio;

    // ── Video ─────────────────────────────────────────────────────────────
    if (e == ".mp4" || e == ".avi" || e == ".mov" || e == ".mkv" ||
        e == ".webm" || e == ".flv" || e == ".m4v" || e == ".3gp" ||
        e == ".ogv" || e == ".mpeg" || e == ".mpg" || e == ".m2ts" ||
        e == ".vob" || e == ".wmv" || e == ".asf" || e == ".rm" ||
        e == ".rmvb")
        return AssetType::Video;

    // ── Scripts (language source files) ───────────────────────────────────
    if (e == ".cpp" || e == ".cc" || e == ".cxx" || e == ".c++" ||
        e == ".ixx" || e == ".c" || e == ".cs" ||
        e == ".lua" || e == ".py" || e == ".js" || e == ".ts" ||
        e == ".jsx" || e == ".tsx" || e == ".rb" || e == ".pl" ||
        e == ".pm" || e == ".php" || e == ".go" || e == ".rs" ||
        e == ".swift" || e == ".kt" || e == ".kts" || e == ".java" ||
        e == ".groovy" || e == ".scala" || e == ".clj" || e == ".cljc" ||
        e == ".edn" || e == ".sh" || e == ".bat" || e == ".ps1" ||
        e == ".ipp" || e == ".tpp")
        return AssetType::Script;

    // ── Headers ───────────────────────────────────────────────────────────
    if (e == ".h" || e == ".hpp" || e == ".hxx" || e == ".hh" ||
        e == ".inl")
        return AssetType::Header;

    // ── Shaders ───────────────────────────────────────────────────────────
    if (e == ".glsl" || e == ".vert" || e == ".frag" || e == ".geom" ||
        e == ".tesc" || e == ".tese" || e == ".comp" || e == ".hlsl" ||
        e == ".metal" || e == ".wgsl")
        return AssetType::ShaderSource;

    // ── Config / Data ─────────────────────────────────────────────────────
    if (e == ".json" || e == ".yaml" || e == ".yml" || e == ".toml" ||
        e == ".ini" || e == ".cfg" || e == ".conf" || e == ".properties" ||
        e == ".xml" || e == ".xsd" || e == ".dtd" || e == ".xsl" ||
        e == ".xslt" || e == ".plist" || e == ".lock" || e == ".env")
        return AssetType::Config;

    // ── Text / Documents ──────────────────────────────────────────────────
    if (e == ".txt" || e == ".md" || e == ".rst" || e == ".tex" ||
        e == ".log" || e == ".csv" || e == ".tsv" || e == ".rtf" ||
        e == ".nfo" || e == ".sfv")
        return AssetType::TextDocument;

    // ── Office / Document formats ─────────────────────────────────────────
    if (e == ".pdf" || e == ".doc" || e == ".docx" || e == ".xls" ||
        e == ".xlsx" || e == ".ppt" || e == ".pptx" || e == ".odt" ||
        e == ".ods" || e == ".odp" || e == ".odg" || e == ".epub" ||
        e == ".mobi" || e == ".cbr" || e == ".cbz")
        return AssetType::Document;

    // ── Archives / Compressed ─────────────────────────────────────────────
    if (e == ".zip" || e == ".rar" || e == ".7z" || e == ".tar" ||
        e == ".gz" || e == ".bz2" || e == ".xz" || e == ".lz" ||
        e == ".lzma" || e == ".zst" || e == ".tgz" || e == ".tbz2" ||
        e == ".txz" || e == ".tlz" || e == ".deb" || e == ".rpm" ||
        e == ".pkg" || e == ".dmg" || e == ".iso" || e == ".img")
        return AssetType::Archive;

    // ── Executables / Libraries ───────────────────────────────────────────
    if (e == ".exe" || e == ".dll" || e == ".so" || e == ".dylib" ||
        e == ".lib" || e == ".a" || e == ".o" || e == ".obj" ||
        e == ".out" || e == ".app" || e == ".msi" || e == ".bin" ||
        e == ".elf" || e == ".com")
        return AssetType::Executable;

    // ── Fonts ─────────────────────────────────────────────────────────────
    if (e == ".ttf" || e == ".otf" || e == ".woff" || e == ".woff2" ||
        e == ".eot" || e == ".pfa" || e == ".pfb" || e == ".pfm" ||
        e == ".afm")
        return AssetType::Font;

    // ── Engine types ──────────────────────────────────────────────────────
    if (e == ".honscene")  return AssetType::Scene;
    if (e == ".honmat")    return AssetType::Material;
    if (e == ".honprefab") return AssetType::Prefab;
    if (e == ".honshader") return AssetType::Shader;

    return AssetType::Other;
}

inline const char* AssetTypeName(AssetType t) {
    switch (t) {
    case AssetType::Texture:      return "Texture";
    case AssetType::Model:        return "Model";
    case AssetType::Audio:        return "Audio";
    case AssetType::Script:       return "Script";
    case AssetType::Scene:        return "Scene";
    case AssetType::Material:     return "Material";
    case AssetType::Prefab:       return "Prefab";
    case AssetType::Font:         return "Font";
    case AssetType::ShaderSource: return "Shader Source";
    case AssetType::Shader:       return "Shader";
        // Extended
    case AssetType::TextDocument: return "Text";
    case AssetType::Header:       return "Header";
    case AssetType::Config:       return "Config";
    case AssetType::Archive:      return "Archive";
    case AssetType::Executable:   return "Executable";
    case AssetType::Video:        return "Video";
    case AssetType::Document:     return "Document";
    case AssetType::CAD:          return "CAD";
    default:                      return "Other";
    }
}

inline const char* AssetTypeIcon(AssetType t) {
    switch (t) {
    case AssetType::Texture:      return "\xef\x80\xbe";  // fa-image
    case AssetType::Model:        return "\xef\x86\xb2";  // fa-cubes
    case AssetType::Audio:        return "\xef\x80\xa1";  // fa-music
    case AssetType::Script:       return "\xef\x84\xa0";  // fa-terminal
    case AssetType::Scene:        return "\xef\x86\xbb";  // fa-tree
    case AssetType::Material:     return "\xef\x83\xab";  // fa-lightbulb-o
    case AssetType::Prefab:       return "\xef\x86\xb2";  // fa-cubes
    case AssetType::Font:         return "\xef\x80\xb1";  // fa-font
    case AssetType::ShaderSource: return "\xef\x81\x9b";  // fa-code
    case AssetType::Shader:       return "\xef\x81\x9b";  // fa-code
        // Extended
    case AssetType::TextDocument: return "\xef\x85\x9c";  // fa-file-text-o
    case AssetType::Header:       return "\xef\x85\xa6";  // fa-puzzle-piece
    case AssetType::Config:       return "\xef\x80\x93";  // fa-cog
    case AssetType::Archive:      return "\xef\x86\x9e";  // fa-archive
    case AssetType::Executable:   return "\xef\x84\xa7";  // fa-bolt
    case AssetType::Video:        return "\xef\x80\x88";  // fa-film
    case AssetType::Document:     return "\xef\x8c\x81";  // fa-file-pdf-o
    case AssetType::CAD:          return "\xef\x86\xb2";  // fa-cubes (3D)
    default:                      return "\xef\x85\x9b";  // fa-file-o
    }
}

// ─────────────────────────────────────────────────────────────────────────────
//  Per-type import settings
// ─────────────────────────────────────────────────────────────────────────────

struct TextureImportSettings {
    int  maxSize = 2048;     // 128..8192
    int  compression = 1;        // 0=None, 1=DXT1, 2=DXT5, 3=BC7
    bool generateMipmaps = true;
    bool sRGB = true;
    bool alphaIsTransparency = false;
    int  wrapMode = 0;        // 0=Repeat, 1=Clamp, 2=Mirror
    int  filterMode = 1;        // 0=Point, 1=Bilinear, 2=Trilinear
    int  anisotropy = 4;        // 1..16
};

struct ModelImportSettings {
    float importScale = 1.0f;
    int   upAxis = 1;       // 0=Y-up, 1=Y-up (default), 2=Z-up
    bool  importMaterials = true;
    bool  importAnimations = true;
    bool  generateCollisions = false;
    bool  weldVertices = true;
    bool  flipUVs = false;
    bool  importLights = false;
    bool  importCameras = false;
    float meshOptThreshold = 0.0f;    // 0=off
};

struct AudioImportSettings {
    int   quality = 100;      // 1..100
    int   sampleRate = 0;        // 0=native
    bool  forceToMono = false;
    bool  loadInBackground = true;
    int   compressionFmt = 0;        // 0=PCM, 1=Vorbis, 2=ADPCM
};

struct ScriptImportSettings {
    bool autoRecompile = true;
    bool treatAsHeader = false;    // .h – don't compile standalone
    std::string compileCommand;      // optional custom cmd
};

// ─────────────────────────────────────────────────────────────────────────────
//  Sub-object  (mesh or animation clip inside a compound asset)
// ─────────────────────────────────────────────────────────────────────────────
struct AssetSubObject {
    std::string name;
    int         index = 0;
    AssetType   type = AssetType::Unknown;  // e.g. Model (mesh) or Unknown (anim)
    bool        enabled = true;
};

// ─────────────────────────────────────────────────────────────────────────────
//  Asset record
// ─────────────────────────────────────────────────────────────────────────────
struct AssetRecord {
    AssetGUID   guid;
    std::string path;          // project-relative path
    std::string displayName;   // usually filename stem
    AssetType   type = AssetType::Unknown;
    int64_t     fileSize = 0;  // bytes
    bool        isFavorite = false;
    bool        needsReimport = false;
    bool        isDirty = false;
    std::string importError;

    // Import settings (only one is active based on type)
    TextureImportSettings textureSettings;
    ModelImportSettings   modelSettings;
    AudioImportSettings   audioSettings;
    ScriptImportSettings  scriptSettings;

    // Sub-objects (e.g. multiple meshes or animation clips)
    std::vector<AssetSubObject> subObjects;

    // References: GUIDs of other assets this asset depends on
    std::unordered_set<std::string> dependencies;  // GUID strings
    // Reverse: which other assets reference this one
    std::unordered_set<std::string> referencedBy;

    // UI state (not persisted)
    GLuint thumbnailTex = 0;    // 0 = not loaded yet
    bool   selected = false;
    bool   subExpanded = false;
};

// ─────────────────────────────────────────────────────────────────────────────
//  AssetDatabase
// ─────────────────────────────────────────────────────────────────────────────
struct AssetDatabase {
    std::unordered_map<std::string, AssetRecord> records;  // GUID string → record
    std::unordered_map<std::string, std::string> pathToGUID; // path → GUID string

    // ── Add / remove ─────────────────────────────────────────────────────────
    AssetRecord& Register(const std::string& projectRelPath) {
        auto it = pathToGUID.find(projectRelPath);
        if (it != pathToGUID.end()) {
            auto rit = records.find(it->second);
            if (rit != records.end()) return rit->second;
        }
        AssetGUID g = AssetGUID::Generate();
        std::string gstr = g.ToString();
        auto& rec = records[gstr];
        rec.guid = g;
        rec.path = projectRelPath;
        rec.displayName = fs::path(projectRelPath).stem().string();
        rec.type = ExtToAssetType(fs::path(projectRelPath).extension().string());
        try {
            rec.fileSize = (int64_t)fs::file_size(fs::path(projectRelPath));
        }
        catch (...) {}
        pathToGUID[projectRelPath] = gstr;
        return rec;
    }

    void Unregister(const std::string& guidStr) {
        auto it = records.find(guidStr);
        if (it == records.end()) return;
        pathToGUID.erase(it->second.path);
        records.erase(it);
    }

    AssetRecord* FindByPath(const std::string& path) {
        auto it = pathToGUID.find(path);
        if (it == pathToGUID.end()) return nullptr;
        auto rit = records.find(it->second);
        return (rit == records.end()) ? nullptr : &rit->second;
    }

    AssetRecord* FindByGUID(const std::string& guidStr) {
        auto it = records.find(guidStr);
        return (it == records.end()) ? nullptr : &it->second;
    }

    // ── Scan a directory, registering all new assets ──────────────────────
    void ScanDirectory(const std::string& rootDir) {
        try {
            for (auto& entry : fs::recursive_directory_iterator(rootDir,
                fs::directory_options::skip_permission_denied)) {
                if (!entry.is_regular_file()) continue;
                std::string p = entry.path().string();
                if (pathToGUID.find(p) == pathToGUID.end())
                    Register(p);
                else {
                    // Update filesize
                    auto* rec = FindByPath(p);
                    if (rec) {
                        try {
                            int64_t sz = (int64_t)fs::file_size(entry.path());
                            if (sz != rec->fileSize) {
                                rec->fileSize = sz;
                                rec->needsReimport = true;
                            }
                        }
                        catch (...) {}
                    }
                }
            }
        }
        catch (...) {}
    }

    // ── Bulk rename ───────────────────────────────────────────────────────
    // Returns number of records updated
    int BulkRename(const std::vector<std::string>& guidStrs,
        const std::string& prefix, const std::string& suffix) {
        int count = 0;
        for (auto& gstr : guidStrs) {
            auto* rec = FindByGUID(gstr);
            if (!rec) continue;
            rec->displayName = prefix + rec->displayName + suffix;
            ++count;
        }
        return count;
    }

    // ── Bulk delete (filesystem + database) ──────────────────────────────
    int BulkDelete(const std::vector<std::string>& guidStrs) {
        int count = 0;
        for (auto& gstr : guidStrs) {
            auto* rec = FindByGUID(gstr);
            if (!rec) continue;
            try { fs::remove(fs::path(rec->path)); }
            catch (...) {}
            pathToGUID.erase(rec->path);
            records.erase(gstr);
            ++count;
        }
        return count;
    }

    // ── Duplicate an asset (file copy + new GUID) ─────────────────────────
    std::string Duplicate(const std::string& guidStr) {
        auto* src = FindByGUID(guidStr);
        if (!src) return "";
        fs::path orig(src->path);
        fs::path copy = orig.parent_path() /
            (orig.stem().string() + "_copy" + orig.extension().string());
        // Avoid collision
        int n = 1;
        while (fs::exists(copy)) {
            copy = orig.parent_path() /
                (orig.stem().string() + "_copy" + std::to_string(n++) + orig.extension().string());
        }
        try {
            fs::copy_file(orig, copy);
        }
        catch (...) { return ""; }
        auto& newRec = Register(copy.string());
        // Copy import settings
        newRec.textureSettings = src->textureSettings;
        newRec.modelSettings = src->modelSettings;
        newRec.audioSettings = src->audioSettings;
        newRec.scriptSettings = src->scriptSettings;
        newRec.isFavorite = false;
        return newRec.guid.ToString();
    }

    // ── Add dependency link ───────────────────────────────────────────────
    void AddDependency(const std::string& srcGUID, const std::string& dstGUID) {
        auto* src = FindByGUID(srcGUID);
        auto* dst = FindByGUID(dstGUID);
        if (!src || !dst) return;
        src->dependencies.insert(dstGUID);
        dst->referencedBy.insert(srcGUID);
    }

    // ── Serialise to .honassets ──────────────────────────────────────────
    bool Save(const std::string& filepath) const {
        std::ofstream f(filepath);
        if (!f.good()) return false;
        f << "# HonHon Asset Database v2\n";
        f << "count=" << records.size() << "\n";
        for (auto& [gstr, rec] : records) {
            f << "[asset]\n";
            f << "guid=" << gstr << "\n";
            f << "path=" << rec.path << "\n";
            f << "name=" << rec.displayName << "\n";
            f << "type=" << (int)rec.type << "\n";
            f << "size=" << rec.fileSize << "\n";
            f << "fav=" << (rec.isFavorite ? "1" : "0") << "\n";
            // Texture settings
            if (rec.type == AssetType::Texture) {
                auto& ts = rec.textureSettings;
                f << "tex.maxSize=" << ts.maxSize << "\n";
                f << "tex.compress=" << ts.compression << "\n";
                f << "tex.mips=" << ts.generateMipmaps << "\n";
                f << "tex.srgb=" << ts.sRGB << "\n";
                f << "tex.wrap=" << ts.wrapMode << "\n";
                f << "tex.filter=" << ts.filterMode << "\n";
                f << "tex.aniso=" << ts.anisotropy << "\n";
            }
            // Model settings
            if (rec.type == AssetType::Model) {
                auto& ms = rec.modelSettings;
                f << "mod.scale=" << ms.importScale << "\n";
                f << "mod.upAxis=" << ms.upAxis << "\n";
                f << "mod.mats=" << ms.importMaterials << "\n";
                f << "mod.anim=" << ms.importAnimations << "\n";
                f << "mod.col=" << ms.generateCollisions << "\n";
                f << "mod.weld=" << ms.weldVertices << "\n";
            }
            // Sub-objects
            f << "subCount=" << rec.subObjects.size() << "\n";
            for (int i = 0; i < (int)rec.subObjects.size(); ++i) {
                auto& so = rec.subObjects[i];
                f << "sub" << i << ".name=" << so.name << "\n";
                f << "sub" << i << ".type=" << (int)so.type << "\n";
                f << "sub" << i << ".enabled=" << so.enabled << "\n";
            }
            // Dependencies
            f << "depCount=" << rec.dependencies.size() << "\n";
            int di = 0;
            for (auto& dep : rec.dependencies)
                f << "dep" << di++ << "=" << dep << "\n";
        }
        return true;
    }

    bool Load(const std::string& filepath) {
        std::ifstream f(filepath);
        if (!f.good()) return false;
        records.clear(); pathToGUID.clear();

        std::string line, curGUID;
        AssetRecord* cur = nullptr;
        int subCount = 0, depCount = 0;

        auto kv = [](const std::string& l, const std::string& key) -> std::string {
            if (l.rfind(key + "=", 0) == 0) return l.substr(key.size() + 1);
            return "";
            };
        auto safeInt = [](const std::string& s, int def = 0) {
            try { return std::stoi(s); }
            catch (...) { return def; }
            };
        auto safeF = [](const std::string& s, float def = 0.f) {
            try { return std::stof(s); }
            catch (...) { return def; }
            };

        while (std::getline(f, line)) {
            if (line.empty() || line[0] == '#') continue;
            if (line == "[asset]") { cur = nullptr; subCount = 0; depCount = 0; continue; }
            if (!cur && line.rfind("guid=", 0) == 0) {
                curGUID = line.substr(5);
                auto& rec = records[curGUID];
                rec.guid = AssetGUID::FromString(curGUID);
                cur = &rec;
                continue;
            }
            if (!cur) continue;
            std::string v;
            if (!(v = kv(line, "path")).empty()) { cur->path = v; pathToGUID[v] = curGUID; }
            else if (!(v = kv(line, "name")).empty())    cur->displayName = v;
            else if (!(v = kv(line, "type")).empty())    cur->type = (AssetType)safeInt(v);
            else if (!(v = kv(line, "size")).empty()) { try { cur->fileSize = std::stoll(v); } catch (...) {} }
            else if (!(v = kv(line, "fav")).empty())     cur->isFavorite = (v == "1");
            // Texture
            else if (!(v = kv(line, "tex.maxSize")).empty())  cur->textureSettings.maxSize = safeInt(v, 2048);
            else if (!(v = kv(line, "tex.compress")).empty()) cur->textureSettings.compression = safeInt(v, 1);
            else if (!(v = kv(line, "tex.mips")).empty())     cur->textureSettings.generateMipmaps = (v != "0");
            else if (!(v = kv(line, "tex.srgb")).empty())     cur->textureSettings.sRGB = (v != "0");
            else if (!(v = kv(line, "tex.wrap")).empty())     cur->textureSettings.wrapMode = safeInt(v);
            else if (!(v = kv(line, "tex.filter")).empty())   cur->textureSettings.filterMode = safeInt(v, 1);
            else if (!(v = kv(line, "tex.aniso")).empty())    cur->textureSettings.anisotropy = safeInt(v, 4);
            // Model
            else if (!(v = kv(line, "mod.scale")).empty())    cur->modelSettings.importScale = safeF(v, 1.f);
            else if (!(v = kv(line, "mod.upAxis")).empty())   cur->modelSettings.upAxis = safeInt(v, 1);
            else if (!(v = kv(line, "mod.mats")).empty())     cur->modelSettings.importMaterials = (v != "0");
            else if (!(v = kv(line, "mod.anim")).empty())     cur->modelSettings.importAnimations = (v != "0");
            else if (!(v = kv(line, "mod.col")).empty())      cur->modelSettings.generateCollisions = (v == "1");
            else if (!(v = kv(line, "mod.weld")).empty())     cur->modelSettings.weldVertices = (v != "0");
            // Sub-objects
            else if (!(v = kv(line, "subCount")).empty()) {
                subCount = safeInt(v);
                cur->subObjects.resize(subCount);
                for (int i = 0; i < subCount; ++i)
                    cur->subObjects[i].index = i;
            }
            // Dependencies
            else if (!(v = kv(line, "depCount")).empty()) depCount = safeInt(v);
            else {
                // sub<i>.name/type/enabled  and  dep<i>=
                for (int i = 0; i < subCount; ++i) {
                    std::string pfx = "sub" + std::to_string(i) + ".";
                    if (!(v = kv(line, pfx + "name")).empty()) { cur->subObjects[i].name = v; break; }
                    if (!(v = kv(line, pfx + "type")).empty()) { cur->subObjects[i].type = (AssetType)safeInt(v); break; }
                    if (!(v = kv(line, pfx + "enabled")).empty()) { cur->subObjects[i].enabled = (v != "0"); break; }
                }
                for (int i = 0; i < depCount; ++i) {
                    if (!(v = kv(line, "dep" + std::to_string(i))).empty())
                    {
                        cur->dependencies.insert(v); break;
                    }
                }
            }
        }
        // Re-build referencedBy from loaded dependencies
        for (auto& [gstr, rec] : records)
            for (auto& dep : rec.dependencies)
                if (records.count(dep))
                    records[dep].referencedBy.insert(gstr);
        return true;
    }
};

// ─────────────────────────────────────────────────────────────────────────────
//  Import-settings panel  (drawn inside the Inspector when an asset is selected)
// ─────────────────────────────────────────────────────────────────────────────
inline bool DrawImportSettingsPanel(AssetRecord& rec) {
    bool changed = false;

    if (rec.type == AssetType::Texture) {
        auto& ts = rec.textureSettings;
        ImGui::SeparatorText("Texture Import Settings");
        static const char* kSizes[] = { "128","256","512","1024","2048","4096","8192" };
        const int kSzVals[] = { 128,256,512,1024,2048,4096,8192 };
        int szIdx = 4;
        for (int i = 0; i < 7; ++i) if (kSzVals[i] == ts.maxSize) szIdx = i;
        if (ImGui::Combo("Max size##tex", &szIdx, kSizes, 7))
        {
            ts.maxSize = kSzVals[szIdx]; changed = true;
        }

        static const char* kCompr[] = { "None","DXT1","DXT5","BC7" };
        if (ImGui::Combo("Compression##tex", &ts.compression, kCompr, 4)) changed = true;

        if (ImGui::Checkbox("Generate mipmaps##tex", &ts.generateMipmaps)) changed = true;
        if (ImGui::Checkbox("sRGB##tex", &ts.sRGB)) changed = true;
        if (ImGui::Checkbox("Alpha is transparency##tex", &ts.alphaIsTransparency)) changed = true;

        static const char* kWrap[] = { "Repeat","Clamp","Mirror" };
        if (ImGui::Combo("Wrap mode##tex", &ts.wrapMode, kWrap, 3)) changed = true;
        static const char* kFilt[] = { "Point","Bilinear","Trilinear" };
        if (ImGui::Combo("Filter mode##tex", &ts.filterMode, kFilt, 3)) changed = true;
        if (ImGui::DragInt("Anisotropy##tex", &ts.anisotropy, 1, 1, 16)) changed = true;
    }
    else if (rec.type == AssetType::Model || rec.type == AssetType::CAD) {
        auto& ms = rec.modelSettings;
        ImGui::SeparatorText("Model Import Settings");
        if (ImGui::DragFloat("Import scale##mod", &ms.importScale, 0.01f, 0.001f, 100.f)) changed = true;
        static const char* kUp[] = { "Y-up (default)","Y-up (forced)","Z-up" };
        if (ImGui::Combo("Up axis##mod", &ms.upAxis, kUp, 3)) changed = true;
        if (ImGui::Checkbox("Import materials##mod", &ms.importMaterials))   changed = true;
        if (ImGui::Checkbox("Import animations##mod", &ms.importAnimations))  changed = true;
        if (ImGui::Checkbox("Generate collisions##mod", &ms.generateCollisions)) changed = true;
        if (ImGui::Checkbox("Weld vertices##mod", &ms.weldVertices))      changed = true;
        if (ImGui::Checkbox("Flip UVs##mod", &ms.flipUVs))           changed = true;
        if (ImGui::Checkbox("Import lights##mod", &ms.importLights))      changed = true;
        if (ImGui::Checkbox("Import cameras##mod", &ms.importCameras))     changed = true;
        if (ImGui::DragFloat("Mesh opt threshold##mod", &ms.meshOptThreshold, 0.001f, 0.f, 1.f, "%.4f")) changed = true;

        // Sub-objects list
        if (!rec.subObjects.empty()) {
            ImGui::Spacing();
            ImGui::SeparatorText("Sub-objects");
            for (auto& so : rec.subObjects) {
                ImGui::PushID(&so);
                ImGui::Checkbox(so.name.c_str(), &so.enabled);
                ImGui::SameLine();
                ImGui::TextDisabled("(%s)", AssetTypeName(so.type));
                ImGui::PopID();
            }
        }
    }
    else if (rec.type == AssetType::Audio) {
        auto& as = rec.audioSettings;
        ImGui::SeparatorText("Audio Import Settings");
        if (ImGui::DragInt("Quality (%%)", &as.quality, 1, 1, 100)) changed = true;
        if (ImGui::DragInt("Sample rate (0=native)", &as.sampleRate, 100, 0, 192000)) changed = true;
        if (ImGui::Checkbox("Force to mono##aud", &as.forceToMono))       changed = true;
        if (ImGui::Checkbox("Load in background##aud", &as.loadInBackground)) changed = true;
        static const char* kFmt[] = { "PCM","Vorbis","ADPCM" };
        if (ImGui::Combo("Compression##aud", &as.compressionFmt, kFmt, 3)) changed = true;
    }
    else if (rec.type == AssetType::Script || rec.type == AssetType::Header) {
        auto& ss = rec.scriptSettings;
        ImGui::SeparatorText("Script Import Settings");
        if (ImGui::Checkbox("Auto-recompile on save##scr", &ss.autoRecompile)) changed = true;
        if (rec.type == AssetType::Header) {
            // Headers are always treated as headers — lock the checkbox on and grey it out
            ImGui::BeginDisabled();
            bool locked = true;
            ImGui::Checkbox("Treat as header##scr", &locked);
            ImGui::EndDisabled();
        }
        else {
            if (ImGui::Checkbox("Treat as header##scr", &ss.treatAsHeader)) changed = true;
        }
        char cmdBuf[256]; strncpy_s(cmdBuf, sizeof(cmdBuf), ss.compileCommand.c_str(), sizeof(cmdBuf) - 1);
        cmdBuf[sizeof(cmdBuf) - 1] = '\0';
        if (ImGui::InputText("Custom compile cmd##scr", cmdBuf, sizeof(cmdBuf))) {
            ss.compileCommand = cmdBuf; changed = true;
        }
        if (!ss.treatAsHeader && ss.autoRecompile) {
            ImGui::PushStyleColor(ImGuiCol_Text, { 0.5f, 0.9f, 0.5f, 1.f });
            ImGui::TextUnformatted("  Will auto-recompile on disk change.");
            ImGui::PopStyleColor();
        }
    }

    // Mark as dirty when settings change
    if (changed) rec.isDirty = true;

    if (!rec.importError.empty()) {
        ImGui::Spacing();
        ImGui::TextColored({ 1.f, 0.4f, 0.4f, 1.f }, " Import error: %s", rec.importError.c_str());
    }

    // References section
    if (!rec.dependencies.empty() || !rec.referencedBy.empty()) {
        ImGui::Spacing();
        ImGui::SeparatorText("References");
        if (!rec.dependencies.empty()) {
            ImGui::TextDisabled("Depends on (%zu):", rec.dependencies.size());
            for (auto& dep : rec.dependencies)
                ImGui::TextDisabled("  %s", dep.substr(0, 18).c_str());
        }
        if (!rec.referencedBy.empty()) {
            ImGui::TextDisabled("Referenced by (%zu):", rec.referencedBy.size());
            for (auto& ref : rec.referencedBy)
                ImGui::TextDisabled("  %s", ref.substr(0, 18).c_str());
        }
    }

    // Apply button - appears when settings have been modified
    if (rec.isDirty) {
        ImGui::Spacing();
        if (ImGui::Button("Apply Changes", { -1, 0 })) {
            rec.isDirty = false;
            rec.needsReimport = true;
        }
    }

    return changed;
}

// ─────────────────────────────────────────────────────────────────────────────
//  Import-settings OVERLAY panel
//  A temporary floating window rendered above the Inspector.
//  Pass showImportOverlay = true to open it (e.g. when an asset is focused or
//  dropped onto the viewport).  Pass a non-null onImport callback for the
//  optional "Import" button (used for 3D-model drops so the user can confirm).
//  The panel closes automatically when the user clicks outside of it.
//
//  Usage (each frame):
//    DrawImportSettingsOverlay(rec, ide.showImportOverlay,
//                              inspectorWindowPos, inspectorWindowSize,
//                              onImport);
// ─────────────────────────────────────────────────────────────────────────────
inline void DrawImportSettingsOverlay(
    AssetRecord* rec,
    bool& showOverlay,
    ImVec2 anchorPos,
    ImVec2 anchorSize,
    const std::function<void()>& onImport = nullptr)
{
    if (!showOverlay || !rec) return;

    // Position above the Inspector, right-aligned
    const float panelW = (std::min)(anchorSize.x, 380.f);
    const float panelH = 420.f;
    ImVec2 panelPos = ImVec2(
        anchorPos.x + anchorSize.x - panelW,
        anchorPos.y - panelH - 4.f
    );
    panelPos.x = (((std::max)))(panelPos.x, 0.f);
    panelPos.y = (((std::max)))(panelPos.y, 20.f);

    ImGui::SetNextWindowPos(panelPos, ImGuiCond_Always);
    ImGui::SetNextWindowSize(ImVec2(panelW, panelH), ImGuiCond_Always);
    ImGui::SetNextWindowBgAlpha(0.97f);

    ImGuiWindowFlags flags =
        ImGuiWindowFlags_NoCollapse |
        ImGuiWindowFlags_NoResize |
        ImGuiWindowFlags_NoMove |
        ImGuiWindowFlags_NoDocking |
        ImGuiWindowFlags_NoSavedSettings;

    // Pass &showOverlay so the window's close button works
    if (!ImGui::Begin("Import Settings", &showOverlay, flags))
    {
        ImGui::End();
        return;
    }

    // Header
    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4{ 0.75f, 0.85f, 1.0f, 1.f });
    ImGui::TextUnformatted("Import Settings");
    ImGui::PopStyleColor();
    ImGui::SameLine();
    ImGui::TextDisabled("  |  %s", rec->displayName.c_str());
    ImGui::SameLine();
    ImGui::TextDisabled("(%s)", AssetTypeName(rec->type));
    ImGui::Separator();

    // Scrollable settings body
    float footerH = onImport ? 56.f : 0.f;
    ImGui::BeginChild("##import_body", ImVec2(0, -footerH), false);
    bool changed = DrawImportSettingsPanel(*rec);
    if (changed) rec->needsReimport = true;
    ImGui::EndChild();

    // Footer buttons
    ImGui::Separator();
    if (rec->needsReimport) {
        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4{ 0.6f, 0.35f, 0.05f, 1.f });
        if (ImGui::Button("Apply Changes", ImVec2(onImport ? -130.f : -1.f, 0))) {
            rec->needsReimport = false;
            // (Optional: trigger actual reimport here)
        }
        ImGui::PopStyleColor();
    }
    else {
        ImGui::BeginDisabled();
        ImGui::Button("Apply Changes", ImVec2(onImport ? -130.f : -1.f, 0));
        ImGui::EndDisabled();
    }

    if (onImport) {
        ImGui::SameLine();
        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4{ 0.18f, 0.55f, 0.18f, 1.f });
        if (ImGui::Button("Import", ImVec2(-1.f, 0))) {
            onImport();
            showOverlay = false;
        }
        ImGui::PopStyleColor();
    }

    // Escape key closes the overlay
    if (ImGui::IsKeyPressed(ImGuiKey_Escape)) {
        showOverlay = false;
    }

    ImGui::End();
}
// ─────────────────────────────────────────────────────────────────────────────
//  DrawBulkOpsPopup — called from asset browser context menu
//   selectedGUIDs: currently selected assets
//   Returns list of commands to execute as strings (caller interprets them)
// ─────────────────────────────────────────────────────────────────────────────
struct BulkOpResult {
    enum Op { None, Delete, Duplicate, Rename } op = None;
    std::string renamePrefix, renameSuffix;
};

inline BulkOpResult DrawBulkOpsPopup(const char* popupId, int selCount) {
    BulkOpResult result;
    if (!ImGui::BeginPopup(popupId)) return result;

    ImGui::TextDisabled("%d asset(s) selected", selCount);
    ImGui::Separator();

    static char pfxBuf[64] = "", sfxBuf[64] = "";
    ImGui::InputText("Prefix##bulk", pfxBuf, sizeof(pfxBuf));
    ImGui::InputText("Suffix##bulk", sfxBuf, sizeof(sfxBuf));
    if (ImGui::MenuItem("Rename (add prefix/suffix)##bulk")) {
        result.op = BulkOpResult::Rename;
        result.renamePrefix = pfxBuf;
        result.renameSuffix = sfxBuf;
    }
    ImGui::Separator();
    if (ImGui::MenuItem("Duplicate##bulk")) result.op = BulkOpResult::Duplicate;
    ImGui::Separator();
    ImGui::PushStyleColor(ImGuiCol_Text, { 1.f, 0.4f, 0.4f, 1.f });
    if (ImGui::MenuItem("Delete##bulk"))   result.op = BulkOpResult::Delete;
    ImGui::PopStyleColor();

    ImGui::EndPopup();
    return result;
}