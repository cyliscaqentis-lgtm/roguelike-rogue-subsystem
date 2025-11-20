# Log Processing Protocol

This document outlines the protocol for summarizing Unreal Engine CSV log files for AI analysis. The goal is to create summaries that are concise yet provide enough context to be useful.

## 1. Core Principles
- **Conciseness:** The final log summary should be as small as possible.
- **Context:** Critical information should be presented with surrounding context to aid understanding.

## 2. Log Architecture
- The C++ logger will produce one unique log file per game session in the `Saved/TurnLogs/` directory.
- The filename will be timestamped (e.g., `Session_20251121-010540.csv`).
- Each line in the CSV will be stamped with the current `TurnID`. The format is: `Type,TurnID,Category,Timestamp,Message`.

## 3. Python Script (`log_summarizer.py`) Behavior

### 3.1. Default Behavior
- When run without specific input arguments, the script will:
    1. Automatically find the **latest session log file** in the log directory.
    2. Analyze the file to find the highest turn number (`T`).
    3. Process only the logs from the **last 3 turns** (`T`, `T-1`, `T-2`).

### 3.2. Filtering
- **Input File:** The user can specify a path to a specific session log file to process instead of the latest one.
- **Turn Filtering:** The user can override the default "last 3 turns" behavior by specifying:
    - `--turn <T>`: A single turn number or a comma-separated list (e.g., `5` or `5,8,12`).
    - `--turn-range <START-END>`: An inclusive range of turns (e.g., `10-15`).
- **Keyword Filtering:** The script filters the selected turns for keywords using:
    - `-d, --depth`: `critical` (error/fatal), `high` (warning+), or `medium` (all keywords).
    - `-k, --keywords`: Additional custom keywords.

### 3.3. Context Control
To provide "necessary and minimal" context, the script supports capturing lines *before* and *after* a matched keyword.
- **Arguments:**
    - `--before N`: Captures `N` lines of context before a matched line.
    - `--after N`: Captures `N` lines of context after a matched line.
    - `--context N`: A shorthand for setting both.
- **Smart Defaults:** If no context arguments are given, the script applies default context based on the keyword's severity:
    - **`fatal:` / `error:`**: 5 lines before, 2 lines after.
    - **`warning:`**: 1 line before, 1 line after.
    - **Custom Keywords:** 0 lines before, 0 lines after.