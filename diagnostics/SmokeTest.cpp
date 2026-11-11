#include <Windows.h>
#include <vector>
#include <utility>
#include "DatafilePluginsdk.h"
#include <atomic>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <thread>

namespace fs = std::filesystem;
static std::atomic<int> originalCalls{0};
static DrEl* Original(DrMultiKeyTable*, unsigned __int64) { ++originalCalls; return nullptr; }
static void Require(bool value, const char* message) { if (!value) throw std::runtime_error(message); }
template<class T> static T Get(HMODULE module, const char* name) {
    auto fn = reinterpret_cast<T>(GetProcAddress(module, name));
    Require(fn != nullptr, name);
    return fn;
}

int wmain(int argc, wchar_t** argv) {
    try {
        Require(argc == 2, "Pass the compiled diagnostic DLL");
        HMODULE module = LoadLibraryW(argv[1]);
        Require(module != nullptr, "Diagnostic DLL failed to load");
        auto api = Get<PluginApiVersionFunc>(module, "PluginApiVersion");
        auto init = Get<PluginInitFunc>(module, "PluginInit");
        auto unload = Get<PluginUnregisterFunc>(module, "PluginUnregister");
        auto getHandlers = Get<PluginTableHandlersFunc>(module, "PluginTableHandlers");
        auto getCount = Get<PluginTableHandlerCountFunc>(module, "PluginTableHandlerCount");
        auto status = Get<PluginStatusFunc>(module, "PluginStatusCheck");
        Require(api() == PLUGIN_API_VERSION, "Loader API mismatch");
        Require(getCount() == 3, "Three table handlers required");
        for (int cycle = 0; cycle < 2; ++cycle) {
            PluginInitParams initParams{};
            initParams.oFind = Original;
            init(&initParams);
            Require(status().success, "Diagnostic failed to initialize");
            const auto* handlers = getHandlers();
            std::atomic<int> replacements{0};
            std::vector<std::thread> callers;
            for (int t = 0; t < 4; ++t) callers.emplace_back([&] {
                for (unsigned table = 0; table < 3; ++table) {
                    DrTableDef def{};
                    def.type = short(100 + table);
                    def.version.major_ver = 2;
                    def.version.minor_ver = 11;
                    def.name = handlers[table].tableName;
                    def.isAutoKey = table == 2;
                    DrMultiKeyTable data{};
                    data._tabledef = &def;
                    PluginExecuteParams params{};
                    params.table = &data;
                    params.oFind = Original;
                    // The diagnostic must tolerate absent original records and
                    // mismatched versions because it reads only raw lookup keys.
                    for (int n = 0; n < 2000; ++n) {
                        params.key = uint64_t(5924645 + n % 14) | (uint64_t(1) << 32);
                        if (handlers[table].executeFunc(&params).drEl) ++replacements;
                    }
                    Require(!handlers[table].executeFunc(nullptr).drEl, "Null input changed a record");
                }
            });
            for (auto& caller : callers) caller.join();
            Require(replacements.load() == 0, "Diagnostic returned a replacement record");
            Require(originalCalls.load() == 0, "Diagnostic called into the original game lookup");
            unload();
            Require(!status().success, "Unregister did not release diagnostic state");
        }
        Require(FreeLibrary(module) != 0, "DLL unload failed");
        wchar_t exe[32768]{};
        GetModuleFileNameW(nullptr, exe, DWORD(std::size(exe)));
        const auto logs = fs::path(exe).parent_path() / "datafilePlugins" / "DropscotchDiagnosticsLogs";
        size_t captures = 0;
        for (const auto& folder : fs::directory_iterator(logs)) {
            if (!folder.is_directory()) continue;
            std::ifstream statusFile(folder.path() / "status.txt");
            const std::string text((std::istreambuf_iterator<char>(statusFile)), {});
            Require(text.find("unique_keys=42\n") != std::string::npos, "Deduplication lost lookup keys");
            Require(text.find("lookup_calls=24000\n") != std::string::npos, "Concurrent lookup count was lost");
            Require(text.find("handler_errors=0\n") != std::string::npos, "Handler reported errors");
            Require(text.find("capacity_drops=0\n") != std::string::npos, "Unexpected capacity loss");
            std::ifstream csv(folder.path() / "lookups.csv");
            std::string line;
            size_t lines = 0;
            while (std::getline(csv, line)) ++lines;
            Require(lines == 43, "CSV snapshot is incomplete");
            ++captures;
        }
        Require(captures == 2, "Both reload cycles must write logs");
        std::cout << "PASS: passive handlers, concurrent lookup counts, deduplication, final flush, reload and unload\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
