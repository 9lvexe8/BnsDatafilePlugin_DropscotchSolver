# Dropscotch diagnostic

Passive diagnostic for LEaN's DatafilePluginloader API 12. This is a separate
DLL; the original solver is unchanged. The diagnostic never returns a replacement
record, calls the original lookup, or installs additional hooks.

Recording starts automatically when the loader initializes it. Play a round and
leave the game running for at least ten seconds afterward. Captures are saved in
`BNSR/Binaries/Win64/datafilePlugins/DropscotchDiagnosticsLogs/`, in a separate
timestamped folder for each process and load. The loader's Insert panel offers
a recording checkbox and a Save logs now button.

Each capture contains `lookups.csv` and `status.txt`. CSV rows aggregate repeated
lookups, including first/last elapsed milliseconds and counts. Table type,
actual table version, auto-key status, and the complete raw 64-bit lookup key
are retained. The low 32 bits and other key parts are provided as raw values;
they are not universally record IDs, especially for auto-key tables.

The worker writes snapshots every ten seconds and on normal plugin unload.
It performs no network requests and captures no chat, credentials, character
names, account details, player positions, or other processes. It reads table
metadata from the loader SDK but does not dereference skill/effect records.
Lookup logging is bounded to 100,000 unique keys per table. Capacity drops and
handler errors are reported, so a truncated capture cannot appear complete.

Installation requires a working compatible DatafilePluginloader already in
the game's `plugins` folder. Close the game before placing
`DropscotchDiagnostics.dll` in `datafilePlugins`. Remove that one diagnostic
DLL with the game closed to uninstall. Do not replace game archives.

The datafile loader may itself need updating for the current client. This
diagnostic cannot prove client compatibility merely by compiling. No in-game
success is claimed until the plugin loads and a capture is observed.

## Building

Use CMake and the Windows x64 C++ toolchain:

    cmake -S diagnostics -B build-diagnostic -A x64
    cmake --build build-diagnostic --config Release
    ctest --test-dir build-diagnostic -C Release --output-on-failure

The GitHub Actions workflow builds the DLL, runs a concurrent passive-lookup and
reload smoke test, then publishes an artifact. No game client or exported game
tables are uploaded. Vendored SDK headers are unchanged copies from
`leanleon93/BnsPlugin_DatafilePluginloader` commit
`2c3d7a83b18b4608a1a25c0b490ebf0ba780d930` (release v1.5.1, API 12).
