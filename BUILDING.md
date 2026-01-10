# Building from Source

This project uses **CMake** to manage the build process. Follow these steps to compile the plugin.

## Prerequisites

1.  **Visual Studio 2022 or 2026**: Ensure you have the "Desktop development with C++" workload installed.
2.  **CMake**: Usually included with Visual Studio, or available as a standalone installation.

## Dependencies

### 1. Ark Server API (Framework)
The Ark API is included as a Git submodule in `extern/Framework-ArkServerApi`. If the folder is empty, run:
```powershell
git submodule update --init --recursive
```

### 2. MariaDB Connector/C
For convenience, the necessary headers and static libraries are **already included in `/extern/MariaDB Connector C 64-bit/`**. 
*   If you need to update it, download the Windows x86_64 ZIP version from the [Official MariaDB Site](https://mariadb.com/downloads/connectors/).
*   **Static Linking**: We link against `mariadbclient.lib` (the static library) to ensure the plugin is self-contained and stable on the server.

### 3. Custom Patches (mysql+++)
The `mysql+++` library (in `extern/mysql-modern-cpp`) has been patched to support disabling SSL for remote connections. 
*   This patch is located in `/patches/mysql+++/`. 
*   **CMake automatically prioritizes this patch** over the original submodule file during compilation.

### 3. Static Runtime (Visual C++)
This project is configured to use the **Static Runtime Library (/MT)**. This is essential for Ark Server API plugins to ensure stability and avoid crashes when using standard library objects (like `std::mutex` or `std::string`) within the game server environment.

1.  **Open a terminal** (PowerShell or Command Prompt).
2.  **Generate Build Files**:
    ```powershell
    cmake -B build -S .
    ```
    *(Note: If `cmake` is not in your PATH, use the one bundled with Visual Studio: `& "C:\Program Files\Microsoft Visual Studio\18\Community\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe" -B build -S .`)*

3.  **Compile the Project**:
    ```powershell
    cmake --build build --config Release
    ```

## Output

The build files will be packaged in:
`build/Release/AsteroidBiomeChatNotifications/`

This folder includes:
- `AsteroidBiomeChatNotifications.dll` (The plugin)
- `AsteroidBiomeChatNotifications.pdb` (Debug info)
- `config.json` (Database configuration)
- `PluginInfo.json` (Ark API plugin metadata)

## Project Configuration

The plugin expects its configuration files to be located in:
`ShooterGame/Binaries/Win64/ArkApi/Plugins/AsteroidBiomeChatNotifications/`
