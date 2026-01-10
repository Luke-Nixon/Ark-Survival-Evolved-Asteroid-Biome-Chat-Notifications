# Building from Source

This project uses **CMake** to manage the build process. Follow these steps to compile the plugin.

## Prerequisites

1.  **Visual Studio 2022 or 2026**: Ensure you have the "Desktop development with C++" workload installed.
2.  **CMake**: Usually included with Visual Studio, or available as a standalone installation.

## Dependencies

### 1. Ark Server API (Framework)
The Ark API is included as a Git submodule in `extern/Framework-ArkServerApi`. If you didn't clone with `--recursive`, run:
```powershell
git submodule update --init --recursive
```

### 2. MariaDB Connector/C
The plugin requires the **MariaDB Connector/C** for database interaction.
*   **Download**: Download the Windows x86_64 ZIP version from the [Official MariaDB Site](https://mariadb.com/downloads/connectors/).
*   **Placement**: Extract it to `extern/MariaDB Connector C 64-bit`.
*   **CRITICAL (Static Linking)**: To avoid runtime DLL errors on the server, we use **static linking**.
    *   The build is configured to link against `mariadbclient.lib` (the static library) rather than `libmariadb.lib` (the dynamic import library).
    *   Additional Windows system libraries (`ws2_32`, `Bcrypt`, etc.) are automatically handled by the `CMakeLists.txt`.

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
