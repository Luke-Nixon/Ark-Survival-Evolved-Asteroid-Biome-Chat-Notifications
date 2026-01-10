# Asteroid Biome Chat Notifications (Genesis 2)

A high-performance C++ plugin for Ark: Survival Evolved (using ArkServerApi) that monitors Genesis 2 asteroid biomes and broadcasts status updates to global chat.

## Features
- **Real-time Monitoring**: Tracks biome shifts on Genesis 2.
- **Cross-Chat Integration**: Compatible with [Ark Cross Chat System](https://github.com/Luke-Nixon/Ark-Survival-Evolved-Cross-Chat-System).
- **SQL Backend**: Logs biome history to a MySQL/MariaDB database.
- **Configurable**: Easy setup via `config.json`.
- **Statistically Linked**: Optimized with a static runtime (/MT) for maximum server stability.

## Installation

1. Download the latest `AsteroidBiomeChatNotifications.zip` from the [Releases](https://github.com/Luke-Nixon/Ark-Survival-Evolved-Asteroid-Biome-Chat-Notifications/releases) page.
2. Extract the folder to your `ShooterGame/Binaries/Win64/ArkApi/Plugins/` directory.
3. Configure your database credentials in `config.json`.

## Configuration (`config.json`)
```json
{
  "Mysql": {
    "Host": "localhost",
    "User": "root",
    "Pass": "yourpassword",
    "Db": "ark_cross_chat",
    "Port": 3306
  }
}
```

## Dependencies
- [ArkServerApi (v3.0+)](https://github.com/Michidu/Ark-Server-API)
- [MariaDB Connector/C](https://mariadb.com/downloads/connectors/connector-c/) (Included in the build)

---

### Development
For instructions on how to compile this project manually, see [BUILDING.md](./BUILDING.md).
