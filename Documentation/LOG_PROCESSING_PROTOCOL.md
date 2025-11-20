# Log Processing Protocol

This document outlines the protocol for summarizing Unreal Engine log files for AI analysis. The goal is to create summaries that are concise yet provide enough context to be useful.

## 1. Core Principles
- **Conciseness:** The final log summary should be as small as possible.
- **Context:** Critical information should be presented with surrounding context to aid understanding.

## 2. Filtering Levels (Depth)
The script shall support different filtering depths:
- `critical`: Only `fatal:` and `error:` messages.
- `high`: `critical` + `warning:` messages.
- `medium`: `high` + specified custom keywords. (Default)

## 3. Log Source & Context

### 3.1. Default Behavior (Turn-Based)
- If no input is specified, the script will default to processing the **last 3 turns**.
- This involves finding the highest turn number `T` available in the log directory (`Saved/TurnLogs/`) and processing the log files for turns `T`, `T-2`, and `T-1`.

### 3.2. Context Control
To provide "necessary and minimal" context, the script must support capturing lines *before* and *after* a matched keyword.

#### 3.2.1. Command-Line Arguments
The following arguments will be added to `log_summarizer.py`:
- `--last-N-turns <N>`: Overrides the default and processes the last `N` turns.
- `--before N`: Captures `N` lines of context before a matched line.
- `--after N`: Captures `N` lines of context after a matched line.
- `--context N`: A shorthand for setting both `--before N` and `--after N`.

#### 3.2.2. Default Context Rules
If no context arguments are provided, the following default context rules will apply based on the keyword found:
- **`fatal:` / `error:`**: 5 lines before, 2 lines after.
- **`warning:`**: 1 line before, 1 line after.
- **Custom Keywords:** 0 lines before, 0 lines after.

This provides a smart default that gives more context for more severe issues.

## 4. Implementation Plan
The `log_summarizer.py` script will be modified to implement this logic. This will involve:
1. Updating `argparse` to include the new arguments (`--last-N-turns`, `--before`, `--after`, `--context`).
2. Adding logic to find and select the last N turn logs by default.
3. Rewriting the `summarize_log` function to use a circular buffer or a similar data structure to keep track of recent lines, allowing it to provide context from before and after a match.
