# Building

## What you need

- **Visual Studio 2022 or newer**, with the C++ desktop workload (x64).
- **CMake 3.15+** — the copy bundled with Visual Studio is fine and is usually
  not on `PATH`:
  `…\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe`
- The **ArkServerApi framework**, vendored in `extern/Framework-ArkServerApi`.

That is the whole list. Earlier versions of this document also required MariaDB
Connector/C, the `mysql+++` wrapper and a patch directory; the plugin no longer
has a database, so none of that is needed or referenced by `CMakeLists.txt`.

## Build

```
cmake -S . -B build
cmake --build build --config Release
```

Output lands in `build/Release/AsteroidBiomeChatNotifications/`, and the
post-build step packages `AsteroidBiomeChatNotifications.zip` beside it.

## What goes in the package

`PluginInfo.json` and `config.example.json`, **by name**. The packaging step
deliberately does not copy the whole `configs/` directory, because that sweeps
in whatever is sitting there — including the `config.json` an operator edits
locally. `configs/config.json` is gitignored and is never packaged.

(The `config.json` this repository used to track held only placeholders, so
nothing was ever exposed. Naming the files is about making that structural
rather than lucky.)

## Runtime dependency

The built DLL links `ArkApi` only. At runtime it resolves `CrossChat_Post` from
`ArkCrossChat.dll` with `GetModuleHandle` + `GetProcAddress`, **at every call and
never cached** — ArkApi hot-reloads plugin DLLs, so a pointer kept from an
earlier call can point into freed code.
