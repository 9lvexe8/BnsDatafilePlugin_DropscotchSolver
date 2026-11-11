#include <Windows.h>
#include <vector>
#include <utility>
#include "DatafilePluginsdk.h"
#include <iostream>
#include <stdexcept>

static DrEl marker{};
static bool provideMarker = true;
static int calls = 0;
static unsigned __int64 requestedKey = 0;
static DrMultiKeyTable* requestedTable = nullptr;
static DrEl* Original(DrMultiKeyTable* table, unsigned __int64 key) {
    ++calls; requestedKey = key; requestedTable = table;
    return provideMarker ? &marker : nullptr;
}
static void Require(bool ok, const char* reason) { if (!ok) throw std::runtime_error(reason); }
template<class T> static T Get(HMODULE module, const char* name) {
    auto value = reinterpret_cast<T>(GetProcAddress(module, name));
    Require(value != nullptr, name); return value;
}
static bool wantedEnabled = true;
static ImGuiPanelRenderFn renderUi = nullptr;
static bool Checkbox(const char*, bool* enabled) { *enabled = wantedEnabled; return true; }
static void Text(const char*, ...) {}
static int __stdcall RegisterPanel(const ImGuiPanelDesc* panel, bool) { renderUi = panel->renderFn; return 1; }
static void __stdcall UnregisterPanel(int) { renderUi = nullptr; }

int wmain(int argc, wchar_t** argv) {
    try {
        Require(argc == 2, "Pass solver DLL");
        auto module = LoadLibraryW(argv[1]); Require(module != nullptr, "DLL load failed");
        auto init = Get<PluginInitFunc>(module, "PluginInit");
        auto unload = Get<PluginUnregisterFunc>(module, "PluginUnregister");
        auto getHandlers = Get<PluginTableHandlersFunc>(module, "PluginTableHandlers");
        auto count = Get<PluginTableHandlerCountFunc>(module, "PluginTableHandlerCount");
        Require(Get<PluginApiVersionFunc>(module, "PluginApiVersion")() == 12, "API mismatch");
        Require(count() == 1, "Solver must hook only skillshow3");
        PluginImGuiAPI ui{}; ui.Checkbox = Checkbox; ui.Text = Text;
        PluginInitParams setup{}; setup.oFind = Original; setup.imgui = &ui;
        setup.registerImGuiPanel = RegisterPanel; setup.unregisterImGuiPanel = UnregisterPanel;
        init(&setup);
        auto fn = getHandlers()[0].executeFunc;
        DrTableDef definition{}; definition.name = L"skillshow3";
        definition.version.major_ver = 2; definition.version.minor_ver = 11;
        DrMultiKeyTable table{}; table._tabledef = &definition;
        PluginExecuteParams request{}; request.table = &table; request.oFind = Original;
        for (short type : {short(390), short(400)}) {
            definition.type = type;
            for (uint64_t id = 5924645; id <= 5924658; ++id) {
                request.key = id | (uint64_t(1) << 32);
                const auto originalKey = request.key;
                Require(fn(&request).drEl == &marker, "Panel was not marked on Live/NEO metadata");
                Require(request.key == originalKey, "Input key was modified");
                Require(requestedKey == (uint64_t(241040) | (uint64_t(1) << 32)), "Wrong Bard key");
                Require(requestedTable == &table, "Wrong table used");
            }
        }
        const int before = calls;
        for (uint64_t id : {uint64_t(5924644), uint64_t(5924659), uint64_t(5924660), uint64_t(241040)}) {
            request.key = id | (uint64_t(1) << 32);
            Require(fn(&request).drEl == nullptr, "Unrelated skill was replaced");
        }
        Require(calls == before, "Unrelated skills called replacement lookup");
        request.key = uint64_t(5924645) | (uint64_t(1) << 32);
        wantedEnabled = false; Require(renderUi != nullptr, "UI was not registered"); renderUi(nullptr);
        Require(!fn(&request).drEl && calls == before, "Disable toggle failed");
        wantedEnabled = true; renderUi(nullptr);
        provideMarker = false;
        Require(fn(&request).drEl == nullptr, "Missing Bard visual did not fall back");
        provideMarker = true;
        const int afterMissing = calls;
        definition.version.minor_ver = 12;
        Require(fn(&request).drEl == nullptr && calls == afterMissing, "Unsupported schema was accepted");
        definition.version.minor_ver = 11; definition.isAutoKey = true;
        Require(fn(&request).drEl == nullptr && calls == afterMissing, "Auto-key table was accepted");
        definition.isAutoKey = false; definition.name = L"another-table";
        Require(fn(&request).drEl == nullptr && calls == afterMissing, "Wrong table name accepted");
        Require(fn(nullptr).drEl == nullptr, "Null input changed data");
        unload(); Require(renderUi == nullptr, "UI not unregistered");
        Require(fn(&request).drEl == nullptr, "Unloaded solver still active");
        Require(FreeLibrary(module) != 0, "DLL unload failed");
        std::cout << "PASS: all 14 IDs on Live 390 and NEO 400, exact replacement key, unrelated IDs, toggle, missing record and schema rejection\n";
        return 0;
    } catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
}
