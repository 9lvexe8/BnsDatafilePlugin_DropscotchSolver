// Passive raw-lookup diagnostic for LEaN's DatafilePluginloader API 12.
// Does not replace records, install detours, or read game-record layouts.
#include <Windows.h>
#include <vector>
#include <utility>
#include "DatafilePluginsdk.h"
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <mutex>
#include <shared_mutex>
#include <stdexcept>
#include <thread>
#include <unordered_map>

namespace fs = std::filesystem;
namespace {
constexpr const char* kName = "DropscotchDiagnostics";
constexpr const char* kVersion = "1.1.0-diagnostic";
constexpr size_t kPerTableLimit = 100000;
constexpr const char* kTables[] = {"skillshow3", "skill3", "effect"};
// Only table metadata is read. These offsets belong to the pinned loader SDK,
// independently of the varying skill/effect record layouts.
static_assert(sizeof(void*) == 8);
static_assert(offsetof(DrTableDef, version) == 10);
static_assert(sizeof(PluginReturnData) == sizeof(void*));

struct Key {
    uint64_t raw;
    unsigned table;
    bool operator==(const Key&) const = default;
};
struct KeyHash {
    size_t operator()(const Key& key) const noexcept {
        return std::hash<uint64_t>{}(key.raw) ^ (size_t(key.table + 1) << 1);
    }
};
struct Row {
    Key key;
    uint64_t firstMs, lastMs, count;
    short tableType, major, minor;
    bool autoKey;
};

class Recorder {
public:
    explicit Recorder(const fs::path& folder) : folder_(folder) {
        fs::create_directories(folder_);
        rows_.reserve(8192);
        index_.reserve(8192);
        // Establish writability before activating a hook or spawning a worker.
        std::ofstream probe(folder_ / "status.txt", std::ios::trunc);
        if (!probe) throw std::runtime_error("Cannot create diagnostic log");
        probe << "DropscotchDiagnostics " << kVersion << " initializing\n";
        probe.close();
        worker_ = std::thread([this] { WriteLoop(); });
    }
    ~Recorder() { Stop(); }
    Recorder(const Recorder&) = delete;
    Recorder& operator=(const Recorder&) = delete;

    void Stop() noexcept {
        stopping_.store(true);
        wake_.notify_all();
        if (worker_.joinable()) worker_.join();
    }
    void Record(unsigned table, PluginExecuteParams* params) {
        if (!enabled.load() || stopping_.load() || !params || !params->table ||
            !params->table->_tabledef) return;
        const auto* def = params->table->_tabledef;
        const uint64_t now = GetTickCount64() - startTick_;
        const Key key{params->key, table};
        std::lock_guard lock(dataMutex_);
        ++calls_;
        if (auto found = index_.find(key); found != index_.end()) {
            auto& row = rows_[found->second];
            row.lastMs = now;
            ++row.count;
            return;
        }
        if (tableCounts_[table] >= kPerTableLimit) {
            ++dropped_;
            return;
        }
        const size_t position = rows_.size();
        // Insert the row first, then roll back if index allocation fails.
        rows_.push_back({key, now, now, 1, def->type,
                         def->version.major_ver, def->version.minor_ver,
                         def->isAutoKey});
        try { index_.emplace(key, position); }
        catch (...) { rows_.pop_back(); throw; }
        ++tableCounts_[table];
    }
    void FlushSoon() { flushRequested_.store(true); wake_.notify_all(); }
    const fs::path& Folder() const { return folder_; }
    size_t UniqueCount() {
        std::lock_guard lock(dataMutex_);
        return rows_.size();
    }
    std::atomic<bool> enabled{true};
    std::atomic<bool> healthy{true};

private:
    void Snapshot() {
        std::vector<Row> rows;
        uint64_t calls, dropped;
        {
            std::lock_guard lock(dataMutex_);
            rows = rows_;
            calls = calls_;
            dropped = dropped_;
        }
        const fs::path temporary = folder_ / "lookups.csv.tmp";
        const fs::path destination = folder_ / "lookups.csv";
        std::ofstream csv(temporary, std::ios::trunc);
        csv << "table,table_type,major,minor,is_auto_key,raw_key_hex,key_lo32,key_byte4,key_hi16,first_ms,last_ms,count\n";
        for (const auto& row : rows) {
            csv << kTables[row.key.table] << ',' << row.tableType << ','
                << row.major << ',' << row.minor << ',' << int(row.autoKey)
                << ",0x" << std::hex << std::setw(16) << std::setfill('0')
                << row.key.raw << std::dec << ',' << uint32_t(row.key.raw)
                << ',' << ((row.key.raw >> 32) & 0xff)
                << ',' << ((row.key.raw >> 48) & 0xffff)
                << ',' << row.firstMs << ',' << row.lastMs << ',' << row.count << '\n';
        }
        csv.flush();
        if (!csv) throw std::runtime_error("Cannot write lookup snapshot");
        csv.close();
        if (!MoveFileExW(temporary.c_str(), destination.c_str(), MOVEFILE_REPLACE_EXISTING))
            throw std::runtime_error("Cannot publish lookup snapshot");
        std::ofstream status(folder_ / "status.txt", std::ios::trunc);
        status << "plugin=" << kName << "\nversion=" << kVersion
               << "\nloader_api=" << PLUGIN_API_VERSION
               << "\nrecording=" << enabled.load() << "\nunique_keys=" << rows.size()
               << "\nlookup_calls=" << calls << "\ncapacity_drops=" << dropped
               << "\nhandler_errors=" << handlerErrors.load()
               << "\npassive_only=1\nrecord_layouts_read=0\noriginal_lookups_called=0\n"
               << "key_parts_are_raw_not_universal_record_ids=1\n";
        status.flush();
        if (!status) throw std::runtime_error("Cannot write status");
    }
    void WriteLoop() noexcept {
        for (;;) {
            std::unique_lock waitLock(waitMutex_);
            wake_.wait_for(waitLock, std::chrono::seconds(10), [this] {
                return stopping_.load() || flushRequested_.load();
            });
            flushRequested_.store(false);
            waitLock.unlock();
            // Stop may arrive while an earlier snapshot is being written.
            // Only exit after taking a snapshot that began AFTER Stop.
            const bool finalSnapshot = stopping_.load();
            try { Snapshot(); healthy.store(true); }
            catch (...) { healthy.store(false); }
            if (finalSnapshot) break;
        }
    }

public:
    std::atomic<uint64_t> handlerErrors{0};
private:
    fs::path folder_;
    const uint64_t startTick_ = GetTickCount64();
    std::mutex dataMutex_, waitMutex_;
    std::condition_variable wake_;
    std::vector<Row> rows_;
    std::unordered_map<Key, size_t, KeyHash> index_;
    size_t tableCounts_[3]{};
    uint64_t calls_ = 0, dropped_ = 0;
    std::atomic<bool> stopping_{false}, flushRequested_{true};
    std::thread worker_;
};

std::shared_mutex g_lifecycle;
// Ownership is explicitly released by PluginUnregister. At process termination
// Windows reclaims it, avoiding a worker-thread join inside DllMain.
Recorder* g_recorder = nullptr;
PluginImGuiAPI* g_imgui = nullptr;
UnregisterImGuiPanelFn g_unregisterPanel = nullptr;
int g_panel = 0;
std::atomic<bool> g_initFailed{false};
std::atomic<unsigned long long> g_captureSequence{0};

PluginReturnData Capture(unsigned table, PluginExecuteParams* params) {
    std::shared_lock lock(g_lifecycle);
    if (g_recorder) {
        try { g_recorder->Record(table, params); }
        catch (...) { ++g_recorder->handlerErrors; }
    }
    return {}; // Always let the loader perform its normal lookup.
}
PluginReturnData SkillShow(PluginExecuteParams* p) { return Capture(0, p); }
PluginReturnData Skill(PluginExecuteParams* p) { return Capture(1, p); }
PluginReturnData Effect(PluginExecuteParams* p) { return Capture(2, p); }

void Panel(void*) {
    std::shared_lock lock(g_lifecycle);
    if (!g_imgui) return;
    if (!g_recorder) {
        g_imgui->Text("Diagnostic initialization failed; no recording is active.");
        return;
    }
    bool enabled = g_recorder->enabled.load();
    if (g_imgui->Checkbox("Record lookups (starts automatically)", &enabled)) {
        g_recorder->enabled.store(enabled);
        g_recorder->FlushSoon();
    }
    g_imgui->Text("Unique lookup keys: %llu", static_cast<unsigned long long>(g_recorder->UniqueCount()));
    g_imgui->Text("Logs save automatically every 10 seconds.");
    g_imgui->Text("No panel markings or game-data changes are applied.");
    if (!g_recorder->healthy.load()) g_imgui->Text("Log write failed. Check folder permissions.");
    if (g_imgui->Button("Save logs now")) g_recorder->FlushSoon();
}

void Init(PluginInitParams* params) {
    {
        std::unique_lock lock(g_lifecycle);
        if (g_recorder) return;
        try {
            HMODULE module = nullptr;
            if (!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
                    GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                    reinterpret_cast<LPCWSTR>(&Init), &module))
                throw std::runtime_error("Cannot locate plugin module");
            std::vector<wchar_t> name(32768);
            const DWORD length = GetModuleFileNameW(module, name.data(), DWORD(name.size()));
            if (!length || length == name.size()) throw std::runtime_error("Cannot locate plugin folder");
            SYSTEMTIME time{};
            GetLocalTime(&time);
            wchar_t stamp[96]{};
            swprintf_s(stamp, L"%04u%02u%02u-%02u%02u%02u-%lu-%llu",
                time.wYear, time.wMonth, time.wDay, time.wHour, time.wMinute,
                time.wSecond, GetCurrentProcessId(), GetTickCount64());
            fs::path root = fs::path(std::wstring(name.data(), length)).parent_path();
            // The loader shadow-copies DLLs to TEMP. Use BNSR.exe's folder, not
            // that temporary DLL location, to keep captures easy to retrieve.
            const DWORD exeLength = GetModuleFileNameW(nullptr, name.data(), DWORD(name.size()));
            if (exeLength && exeLength < name.size())
                root = fs::path(std::wstring(name.data(), exeLength)).parent_path() / L"datafilePlugins";
            const auto captureName = std::wstring(stamp) + L"-" +
                std::to_wstring(g_captureSequence.fetch_add(1));
            g_recorder = new Recorder(root / L"DropscotchDiagnosticsLogs" / captureName);
            g_initFailed.store(false);
        } catch (...) { g_initFailed.store(true); }
        if (params) {
            g_imgui = params->imgui;
            g_unregisterPanel = params->unregisterImGuiPanel;
        }
    }
    if (params && params->imgui && params->registerImGuiPanel && params->unregisterImGuiPanel) {
        ImGuiPanelDesc panel{kName, Panel, nullptr};
        g_panel = params->registerImGuiPanel(&panel, false);
    }
}
void Unregister() {
    if (g_panel && g_unregisterPanel) g_unregisterPanel(g_panel);
    g_panel = 0;
    std::unique_lock lock(g_lifecycle);
    delete g_recorder; // Flush and join while the module is still loaded.
    g_recorder = nullptr;
    g_imgui = nullptr;
    g_unregisterPanel = nullptr;
}
PluginStatus Status() {
    std::shared_lock lock(g_lifecycle);
    if (g_initFailed.load() || !g_recorder) return {false, "Could not create diagnostic log folder"};
    if (!g_recorder->healthy.load()) return {false, "Could not write diagnostic logs"};
    return {};
}
PluginTableHandler handlers[] = {
    {L"skillshow3", SkillShow}, {L"skill3", Skill}, {L"effect", Effect}
};
} // namespace

DEFINE_PLUGIN_API_VERSION()
DEFINE_PLUGIN_IDENTIFIER(kName)
DEFINE_PLUGIN_VERSION(kVersion)
DEFINE_PLUGIN_INIT(Init, Unregister)
DEFINE_PLUGIN_TABLE_HANDLERS(handlers)
DEFINE_PLUGIN_STATUS(Status)
