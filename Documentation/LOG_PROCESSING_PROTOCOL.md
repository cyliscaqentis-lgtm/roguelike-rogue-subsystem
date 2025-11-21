# Log Processing Protocol

This document outlines the protocol for summarizing Unreal Engine CSV log files for AI analysis. The goal is to create summaries that are concise yet provide enough context to be useful.

## 1. Core Principles
- **Conciseness:** The final log summary should be as small as possible.
- **Context:** Critical information should be presented with surrounding context to aid understanding.

## 2. Log Architecture
- The C++ logger will produce one unique log file per game session in the `Saved/TurnLogs/` directory.
- The filename will be timestamped (e.g., `Session_20251121-010540.csv`).
- Each line in the CSV will be stamped with the current `TurnID`. The format is: `Type,TurnID,Category,Timestamp,Message`.

## 3. Python Script (`Tools/Log/log_summarizer.py`) Behavior

The `log_summarizer.py` script is the primary entry point for AI‑driven log analysis. It provides:

- Automatic session selection (latest log in `Saved/TurnLogs`).
- Turn‑based trimming (typically the last 3 turns).
- Multiple presets for common analysis tasks.
- An AI‑friendly mode that emits structured JSON summaries of player actions.

> All examples below assume the working directory is `Source/LyraGame/Rogue/`.

### 3.1. Default Behavior (Turn Window)

When run without explicit turn filters, the script will:

1. Automatically find the **latest session log file** in the log directory (`Saved/TurnLogs/Session_*.csv`).
2. Analyze the file to find the highest turn number (`T`).
3. Process only the logs from the **last 3 turns** (`T`, `T-1`, `T-2`).

Example:

```cmd
python Tools\Log\log_summarizer.py summary_default.txt
```

> **Note:** If no directory is specified in the output filename, the script will automatically save the file to `Tools/Log/` to keep the root directory clean.

This command produces a filtered text summary for the last three turns using the default preset.

### 3.2. Turn Filtering

The turn window can be overridden explicitly:

- `--turn <T>`: A single turn or a comma‑separated list (e.g., `5` or `5,8,12`).
- `--turn-range <START-END>`: An inclusive range (e.g., `4-6`).

Examples:

```cmd
python Tools\Log\log_summarizer.py turn5.txt --turn 5
python Tools\Log\log_summarizer.py turn4_6.txt --turn-range 4-6
```

### 3.3. Preset Filtering (`--preset`)

Instead of manually specifying keywords, the script exposes named presets tailored for common debugging scenarios.

- `--preset default`  
  General error investigation. Keeps errors, warnings, and critical system events while dropping common noise.

- `--preset raw`  
  No filtering; emits all lines in the selected turn window (useful when building new presets).

- `--preset gameplay`  
  Focus on *what* happened in the game: turns, actions, damage, spawns, ability usage, and barrier changes.  
  Intended for high‑level gameplay timelines (player and enemy behavior).

- `--preset ai_debug`  
  Focus on enemy AI thinking: intents, scoring, observations, validity checks, and fallback paths.

- `--preset movement_logic`  
  Focus on grid reservations, origin‑holds, overlaps, and movement commits. Useful for “enemy stacking”や壁抜けなどの調査.

- `--preset input_flow`  
  Focus on player input windows, gates, latches, and RPC flow. Useful for input遅延・操作のひっかかりの分析.

- `--preset dungeon_gen`  
  Focus on procedural dungeon generation, room selection, and initial spawns.

- `--preset movement_detailed`  
  Deep dive into pathfinding scores and neighbor selection. Useful for debugging `GetNextStep` logic.

- `--preset gas_stats`  
  Debug Gameplay Ability System, Attributes, and Effects.

- `--preset turn_hang`  
  Debug turn flow, phase transitions, and barrier locks.

Each preset defines an internal **whitelist** (keywords to keep) and **blacklist** (noise to drop). The whitelist can be extended via:

- `-k, --keywords <KW ...>`: Additional case‑insensitive keywords to force‑include.

Example (gameplay‑oriented summary for turns 4–6):

```cmd
python Tools\Log\log_summarizer.py gameplay_4_6.txt --preset gameplay --turn-range 4-6
```

### 3.4. Context Control (`--before` / `--after`)

To provide “necessary and minimal” context, the script supports capturing lines *before* and *after* a matched line:

- `--before N`: Keep up to `N` lines preceding a matched line.
- `--after N`: Keep up to `N` lines following a matched line.

If no explicit context is given, the summarizer:

- Always favors keeping **critical** lines (`fatal`, `error`, `severe`, `exception`, `ensure`).
- Automatically adds a small number of trailing lines after critical events, to preserve the immediate call‑stack and resolution.

Example:

```cmd
python Tools\Log\log_summarizer.py errors_with_context.txt --preset default --before 2 --after 4
```

### 3.5. AI‑Focused Player Action Mode (`--mode player_actions`)

For AI agents that need to reconstruct what the **player** actually did, the script provides a dedicated mode:

- `--mode player_actions`  
  Extracts a concise list of *validated* player commands (`Command.Player.*`) within the selected turns.
  Transient or rejected inputs are ignored; only “Command validated / Command received and validated” events are included.

Each extracted action includes:

- `turn`: Turn ID (integer).
- `action_type`: `"attack"`, `"move"`, `"wait"`, or `"action"` (fallback).
- `tag`: Full command tag (e.g., `Command.Player.Attack`).
- `target_cell`: `{ "x": <int>, "y": <int> }` when the log carries a `TargetCell=(X,Y)` hint; otherwise `null`.
- `timestamp`: The log timestamp string.
- `raw`: The original log message (`Message` column).

#### JSON Output for AI

When combined with `--json`, the script writes a JSON array suitable for machine consumption:

```cmd
python Tools\Log\log_summarizer.py ai_player_last3.json --mode player_actions --json
```

Behavior:

1. Automatically selects the latest session log.
2. Detects the highest turn `T` and selects turns `T-2..T`.
3. Extracts validated `Command.Player.*` entries in that range.
4. Writes a JSON array to `ai_player_last3.json`.

Example (conceptual structure):

```json
[
  {
    "turn": 5,
    "action_type": "attack",
    "tag": "Command.Player.Attack",
    "target_cell": { "x": 0, "y": 0 },
    "timestamp": "16863240.083026",
    "raw": "[TurnCommandHandler] Command received and validated: TurnId=5, Tag=Command.Player.Attack, TargetCell=(0,0)"
  }
]
```

AI agents can call this once, parse the resulting JSON, and then answer questions such as
“What did the player do in the last three turns?” without re‑implementing any parsing logic.

#### Targeting Specific Turns

The same mode supports explicit turn filters:

```cmd
python Tools\Log\log_summarizer.py ai_player_turn5.json --mode player_actions --json --turn 5
python Tools\Log\log_summarizer.py ai_player_turn4_6.json --mode player_actions --json --turn-range 4-6
```

This is the recommended entry point for AI workflows that need structured player action timelines.

### 3.6. Deep Dive Debugging

For complex algorithmic issues (like pathfinding or AI scoring), standard logs may be insufficient.
The project includes a `DIAG_LOG` macro that outputs to `LogRogueDiagnostics`.

- **Enabling Verbose Logs:**
  Use the console variable `Rogue.Debug.VerboseLogging 1` to enable `VeryVerbose` diagnostic logs at runtime.
  
- **Analyzing:**
  Use `--preset movement_detailed` to capture these high-volume logs.
  
  ```cmd
  python Tools\Log\log_summarizer.py detailed_move.txt --preset movement_detailed --turn 5
  ```
