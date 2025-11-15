# Rogue - Unreal Engine 5 Roguelike Dungeon Project

This is a turn-based roguelike dungeon game built on Unreal Engine 5.6 using the Lyra game framework.

## 📚 Documentation

**Important: Please read the documentation before making changes to this project.**

All project documentation is located in the [`Documentation/`](./Documentation/) directory:

- **[BUILD_INSTRUCTIONS.md](./Documentation/BUILD_INSTRUCTIONS.md)** - Build commands and troubleshooting guide
- **[CODE_REVISION.md](./Documentation/CODE_REVISION.md)** - Code revision log, change tracking, and usage guidelines
- **[REFACTORING_PLAN.md](./Documentation/REFACTORING_PLAN.md)** - Refactoring design document for large class decomposition

## 🎯 Project Overview

This project implements a turn-based combat system with:
- **Turn Management**: Phased turn system with player input, enemy AI, and simultaneous movement
- **Grid-Based Movement**: Tile-based dungeon navigation
- **Enemy AI**: Intent-based AI system for enemy decision making
- **Ability System**: Gameplay abilities for attacks and movement

## 🏗️ Key Components

- `GameTurnManagerBase` - Core turn flow coordinator
- `UnitBase` - Base class for all units (player, enemies, allies)
- `EnemyAISubsystem` - Enemy AI decision making
- `URogueDungeonSubsystem` - Dungeon generation and grid management
- `GridOccupancySubsystem` - Grid cell occupancy tracking

## 📝 Code Revision System

This project uses a `CodeRevision` comment system to track changes. When modifying code:

1. Add a `CodeRevision: <ID> (<description>) (YYYY-MM-DD HH:MM)` comment near your changes
2. Update `Documentation/CODE_REVISION.md` with the change details
3. Follow the guidelines in `Documentation/CODE_REVISION.md`

## 🔧 Build Requirements

- Visual Studio 2022
- Unreal Engine 5.6
- Windows 10/11

See [BUILD_INSTRUCTIONS.md](./Documentation/BUILD_INSTRUCTIONS.md) for detailed build commands.

---

**For AI Assistants**: When working on this project, please review the documentation in the `Documentation/` directory first, especially `CODE_REVISION.md` to understand the project's coding standards and change tracking system.

