# Handover Document: Log Summarizer Project

## 1. Project Goal

The primary objective is to create a robust log processing workflow.

- **C++ Logger**: An Unreal Engine component (`UDebugObserverCSV`) should capture all engine and game logs into a structured format.
- **Python Script**: A script (`Tools/Log/log_summarizer.py`) should parse these logs to extract meaningful, context-aware summaries based on user-defined filters (keywords, turn numbers, etc.).

## 2. Final Architecture

After a great deal of debugging and refactoring, we have established the following architecture:

- **C++ Logger (`UDebugObserverCSV`):**
    - Creates **one unique log file per game session**.
    - Saves the file to `Saved/TurnLogs/`.
    - The filename is **timestamped** to prevent overwriting (e.g., `Session_20251121-013000.csv`).
    - The log is in CSV format. Every single line is stamped with the `TurnID` as the second column: `Type,TurnID,Category,Timestamp,Message`.
    - The logger is now **thread-safe** (to prevent crashes on exit) and captures **all** log messages, not just game-specific ones.

- **Python Script (`log_summarizer.py`):**
    - **Input**: By default, it finds the latest `Session_*.csv` file to process. A specific file can also be passed as an argument.
    - **Turn Filtering**: It is designed to filter the single session file by turn number.
        - `--turn <T>`: Filters for one or more specific turns.
        - `--turn-range <START-END>`: Filters for a range of turns.
    - **Context Filtering**: It can extract lines before and after a keyword match.
        - `--before N`, `--after N`, `--context N` control this.
        - "Smart context" is the default, providing more lines for more severe errors.

## 3. The Unresolved Issue: The "Final Boss"

Everything in the C++ and Python code *appears* to be correct and aligned with the architecture above. However, one final bug remains.

- **The Problem**: The Python script's default behavior is to find the latest log, determine the highest turn number (`max_turn`) within it, and then process the last 3 turns. The script consistently **fails to determine the `max_turn`**, reporting `Could not determine max turn.`
- **What This Means**: The Python code loop that is supposed to read the second column of the CSV and parse the integer `TurnID` is failing for some reason.
- **Investigation So Far**:
    - The C++ `Printf` statements have been verified and seem to write the `TurnID` correctly as the second value.
    - The Python parsing logic has been made robust against empty lines and whitespace (`line.strip()`, `parts[1].strip().isdigit()`).
    - The log file's encoding was suspected, so the Python script now reads using `utf-8-sig`.
    - Peeking at the log file contents via shell commands shows a format that *looks* correct.

- **Hypothesis**: The root cause is a subtle **environmental issue**. There is a high probability of an invisible character, a non-standard newline, or an encoding problem on the user's system that is causing Python's `line.split(',')` or `isdigit()` to fail in a way that could not be remotely diagnosed.

## 4. Recommended Next Steps for the New AI

Your primary mission is to solve this final parsing problem.

1.  **Aggressive Debugging**: The key is to see *exactly* what the Python script sees. Modify the `max_turn` loop in `log_summarizer.py`:
    -   For a few lines from the log file, write detailed debug information to a separate file.
    -   For each line, output:
        -   The raw `line` itself.
        -   The `parts` array after `line.split(',')`.
        -   The value of `parts[1]`.
        -   The result of `''.join(char for char in parts[1] if char.isprintable())` to see if there are non-printable characters.
        -   The boolean result of `parts[1].strip().isdigit()`.
    -   This will definitively reveal why the parsing is failing.

2.  **Alternative Parsing Method**: If `split(',')` is proving unreliable (perhaps due to commas within log messages that are not being quoted correctly by every `UE_LOG` call), switch to a more robust parsing method.
    -   Use Python's built-in `csv` module. It is designed to handle quoted fields and other complexities gracefully. This is a much more robust solution than `split(',')`.

3.  **Finalize and Verify**: Once the `max_turn` can be determined, the default "last 3 turns" behavior should work. Verify this, and then the user will be ready to define their final keyword filtering policies.

Good luck.
