// Uses the original LEaN panel-effect substitution, with name/version based
// validation instead of a NEO-only numeric table-ID-to-version mapping.
#include <Windows.h>
#include <vector>
#include <utility>
#include "DatafilePluginsdk.h"
#include <atomic>
#include <cwchar>

namespace {
constexpr const char* kName = "DropscotchSolver";
constexpr const char* kVersion = "1.1.0-live-neo";
constexpr uint64_t kBardKey = uint64_t(241040) | (uint64_t(1) << 32);
std::atomic<bool> g_ready{false}, g_enabled{true}, g_incompatible{false};
std::atomic<bool> g_lastLookupFailed{false};
std::atomic<unsigned long long> g_matched{0}, g_replaced{0}, g_missing{0};
PluginImGuiAPI* g_imgui = nullptr;
UnregisterImGuiPanelFn g_unregister = nullptr;
int g_panel = 0;

PluginReturnData PanelVisual(PluginExecuteParams* params) {
    if (!g_ready.load() || !g_enabled.load() || !params || !params->table ||
        !params->table->_tabledef || !params->oFind || !params->key) return {};
    const auto* definition = params->table->_tabledef;
    if (!definition->name || std::wcscmp(definition->name, L"skillshow3") != 0) return {};
    // Live: table 390. NEO: table 400. Neither number defines compatibility.
    // Only the verified 2.11 key layout is supported. No records are dereferenced.
    if (definition->version.major_ver != 2 || definition->version.minor_ver != 11 ||
        definition->isAutoKey) {
        g_incompatible.store(true);
        return {};
    }
    const uint32_t id = uint32_t(params->key);
    if (id < 5924645 || id > 5924658) return {};
    ++g_matched;
    // Call the loader-provided original lookup, never the hooked entry point.
    // The request's key remains unchanged. Missing original panel records are OK.
    DrEl* replacement = params->oFind(params->table, kBardKey);
    g_lastLookupFailed.store(replacement == nullptr);
    if (!replacement) { ++g_missing; return {}; }
    ++g_replaced;
    return {replacement};
}

void Ui(void*) {
    auto* ui = g_imgui;
    if (!ui) return;
    bool enabled = g_enabled.load();
    if (ui->Checkbox("Enable Dropscotch panel markers", &enabled)) g_enabled.store(enabled);
    ui->Text("Matched panel lookups: %llu", g_matched.load());
    ui->Text("Visual replacements: %llu", g_replaced.load());
    ui->Text("Missing replacement lookups: %llu", g_missing.load());
    if (g_incompatible.load()) ui->Text("Unsupported skillshow3 format: expected version 2.11.");
    if (g_lastLookupFailed.load()) ui->Text("Bard visual not found; the original lookup is used.");
}

void Init(PluginInitParams* params) {
    g_enabled.store(true);
    g_incompatible.store(false);
    g_lastLookupFailed.store(false);
    g_matched.store(0); g_replaced.store(0); g_missing.store(0);
    g_ready.store(params && params->oFind);
    if (params && params->imgui && params->registerImGuiPanel && params->unregisterImGuiPanel) {
        g_imgui = params->imgui;
        g_unregister = params->unregisterImGuiPanel;
        ImGuiPanelDesc desc{kName, Ui, nullptr};
        g_panel = params->registerImGuiPanel(&desc, false);
    }
}
void Unregister() {
    g_ready.store(false);
    if (g_panel && g_unregister) g_unregister(g_panel);
    g_panel = 0; g_imgui = nullptr; g_unregister = nullptr;
}
PluginStatus Status() {
    if (!g_ready.load()) return {false, "Solver is not initialized"};
    if (g_incompatible.load()) return {false, "skillshow3 must be version 2.11 and non-auto-key"};
    if (g_lastLookupFailed.load()) return {false, "Bard visual record was not found"};
    return {};
}
PluginTableHandler handlers[] = {{L"skillshow3", PanelVisual}};
} // namespace

PLUGIN_EXPORT int PluginApiVersion() { return PLUGIN_API_VERSION; }
PLUGIN_EXPORT bool IsIncompatible() { return g_incompatible.load(); }
void SetCompatibilityError(bool value) { g_incompatible.store(value); }
DEFINE_PLUGIN_IDENTIFIER(kName)
DEFINE_PLUGIN_VERSION(kVersion)
DEFINE_PLUGIN_INIT(Init, Unregister)
DEFINE_PLUGIN_TABLE_HANDLERS(handlers)
DEFINE_PLUGIN_STATUS(Status)
