# DropscotchSolver 1.1.0: Live/NEO compatibility candidate

Uses LEaN's original 14 panel IDs (5924645 through 5924658) and the original
Bard effect replacement (241040, variation 1, skin 0). Those panel requests
were observed in the user's current Live capture. This is a visual substitution,
not movement automation or a server-side change.

The published NEO table map assigns skillshow3 to 400; the current Live client
uses 390. The old region-based compatibility guard looks up the version by
numeric table ID. This candidate verifies the actual name skillshow3, version
2.11, and non-auto-key layout directly, avoiding that edition-specific map.
Only lookup keys and table metadata are read; no game-record layout is accessed.

With the game closed, put DropscotchSolver.dll in
BNSR/Binaries/Win64/datafilePlugins, beside the diagnostic if retained.
Keep the already-working DatafilePluginloader.dll in plugins. Do not put the
solver DLL directly in Win64 or plugins, and do not load two solver versions.

Markers are enabled by default. Insert opens the loader UI; DropscotchSolver
has an enable checkbox, a matched-lookup count, replacement count, and missing
replacement count. If the Bard record is unavailable, the normal lookup is used.
Unsupported table versions are rejected rather than guessed.

Both numeric table IDs are tested with a simulated host. Tests cover all 14
panel IDs, unrelated IDs, the enable switch, missing-target fallback, input-key
preservation, and version/name/auto-key rejection. NEO runtime and actual in-game
panel visuals still need confirmation. Compiler/test success does not by itself
prove which live panels are safe or that the effect renders with user settings.
