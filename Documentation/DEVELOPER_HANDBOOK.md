# Developer Handbook

This handbook provides comprehensive guidelines for developing and contributing to the RogueDungeon project. All developers and AI agents must adhere to these protocols.

---

## 1. Repository Guidelines

*This section is derived from the original `AGENTS.md`.*

### 1.1. Project Structure & Module Organization
- Source code lives at the top level of this folder with domain-specific subvfolders: `Abilities/`, `AI/`, `Character/`, `Grid/`, `Player/`, `Turn/`, `Utility/`, plus `Data/` for configuration and `Debug/` for temporary helpers. `Documentation/` hosts procedural references, while `README.md` provides the high-level overview. Keep new subsystems grouped by these directories to match the existing module layout and avoid scatter.

### 1.2. Coding Style & Naming Conventions
- Follow Unreal Engine C++ style conventions—use tabs for indentation, PascalCase for `A`/`U`/`F` classes, and prefix member variables with `b`, `My`, etc., as in surrounding code. Keep header/tooling hooks in the same folder as their matching module (e.g., `AI/Enemy` inside `AI/`).
- Every code modification must include a `CodeRevision` comment (format: `CodeRevision: <ID> (<description>) (YYYY-MM-DD HH:MM)`) near the affected logic and an entry in `Documentation/CODE_REVISION.md`. Keep all new comments in English and link the code comment and log entry to the same ID for traceability.

### 1.3. Testing Guidelines
- There is no automated C++ test suite in this project yet; rely on manual validation using the UE5 editor. After building, launch `LyraEditor`, resize the dungeon floor, run through a player turn, trigger an ability or AI turn, and verify the subsystem updates as expected. Document the scenario, steps, and results either inline in code (as part of the `CodeRevision` comment) or in `Documentation/REFACTORING_SPECIFICATION.md` if it affects architecture.
- For any workaround or bug fix, list the reproduction steps and expected outcome in the PR description so reviewers can reproduce the issue locally.

### 1.4. Commit & Pull Request Guidelines
- Reuse the apparent commit pattern seen in recent history (e.g., `eod: 2025-11-16`) so the log is easy to scan; keep the summary short, and mention the subsystem touched.
- PRs should include a concise description of the change, related issue numbers or `CodeRevision` IDs, the manual tests performed (command or editor scenario), and, if appropriate, a screenshot of the UI or gameplay change. Update `Documentation/CODE_REVISION.md` before merging so reviewers can cross-check your comment ID with the log entry.

---

## 2. Build Instructions

*This section is derived from the original `BUILD_INSTRUCTIONS.md`.*

### 2.1. Pre-build Checklist
1. **Visual Studio 2022** must be installed.
2. **Unreal Engine 5.6** must be installed.
3. **Git** should be used to pull the latest changes.

### 2.2. Recommended Build Command
Use the following command to build the project. It is configured to avoid common issues like the `9666` error by disabling the Unreal Build Accelerator (UBA).

```cmd
"C:\Program Files\Epic Games\UE_5.6\Engine\Binaries\DotNET\UnrealBuildTool\UnrealBuildTool.exe" LyraEditor Win64 Development -Project="C:\UnrealProjects\RoguelikeDungeon\RoguelikeDungeon.uproject" -WaitMutex -NoHotReload -Verbose -NoUBA -Log="C:\UnrealProjects\RoguelikeDungeon\UBT_noUBA.log"
```

**Build Log Location:**
The build log will be output to `C:\UnrealProjects\RoguelikeDungeon\UBT_noUBA.log`. Check this file if you encounter build errors.

### 2.3. Alternative Build Command
You can also use the standard `Build.bat` script, but be aware that this may cause `9666` errors due to UBA.

```cmd
"C:\Program Files\Epic Games\UE_5.6\Engine\Build\BatchFiles\Build.bat" LyraEditor Win64 Development -Project="C:\UnrealProjects\RoguelikeDungeon\RoguelikeDungeon.uproject" -WaitMutex -FromMsBuild
```

### 2.4. Troubleshooting
- **Error 9666**: This is likely a UBA compatibility issue. Use the recommended build command with the `-NoUBA` flag.
- **Linker Errors (LNK2019, LNK1120)**: Clean the project, delete the `Intermediate` and `Binaries` folders, and rebuild.

---

## 3. Encoding and File Handling

*This section is derived from the original `ENCODING_ISSUES.md`.*

### 3.1. Problem Overview
Source files can be misinterpreted by some AI tools as SHIFT-JIS instead of UTF-8, leading to file corruption, especially when non-ASCII characters (like Japanese comments) are present.

### 3.2. Root Cause
- **Missing BOM (Byte Order Mark)**: Files are saved as UTF-8 without BOM, making automatic encoding detection difficult for some tools.
- **Presence of Japanese Characters**: Different byte representations in UTF-8 vs. SHIFT-JIS.
- **Tool Logic**: Some tools default to system encoding (like SHIFT-JIS on Windows) when no BOM is found.

### 3.3. Mandatory Countermeasures
1.  **.editorconfig**: The `.editorconfig` file in the repository should specify the charset for C++ files.
    ```ini
    [*.{cpp,h}]
    charset = utf-8
    ```
2.  **English Comments**: As a long-term solution, prefer English for all new code comments to minimize encoding-related risks.
3.  **.gitattributes**: This file helps enforce encoding standards.
    ```
    *.cpp text eol=crlf working-tree-encoding=UTF-8
    *.h text eol=crlf working-tree-encoding=UTF-8
    ```
4.  **AI Tool Instructions**: All AI agents must be instructed to read and write all C++ files as UTF-8 (without BOM).

### 3.4. Emergency Response
If a file becomes corrupted:
1. Revert the file from Git: `git checkout -- <file>`
2. Verify encoding (e.g., in PowerShell): `[System.IO.File]::ReadAllText("<file>", [System.Text.Encoding]::UTF8)`
3. Re-save as UTF-8 if necessary.

---

## 4. AI Agent Protocols

*This section is derived from the original `LOG_PROCESSING_PROTOCOL.md`.*

### 4.1. Log Summarization (`log_summarizer.py`)
To manage AI context size, use the `log_summarizer.py` script to parse large Unreal Engine logs, leveraging predefined filtering depths or custom keywords. It also includes functionality to automatically detect the latest log file.

**Usage:**
The script supports different "parsing depths" to minimize context based on the analysis required. You can specify the input log file directly, or have the script automatically find the latest log in a given directory.

```bash
# Default behavior: Summarize the latest .log file in the current directory (default depth is 'medium')
python Tools/Log/log_summarizer.py <PATH_TO_GENERATED_SUMMARY_LOG>

# Specify a specific input log file
python Tools/Log/log_summarizer.py <PATH_TO_INPUT_LOG> <PATH_TO_GENERATED_SUMMARY_LOG>

# Automatically find the latest .log file in a specified directory
python Tools/Log/log_summarizer.py --latest-from-dir <PATH_TO_LOG_DIRECTORY> <PATH_TO_GENERATED_SUMMARY_LOG>

# Using predefined filtering depths:
# - critical: Only 'error:', 'fatal:', 'severe:'
# - high: Critical + 'warning:'
# - medium: High + custom keywords (default)
python Tools/Log/log_summarizer.py <PATH_TO_INPUT_LOG> <PATH_TO_GENERATED_SUMMARY_LOG> --depth critical
python Tools/Log/log_summarizer.py --latest-from-dir <PATH_TO_LOG_DIRECTORY> <PATH_TO_GENERATED_SUMMARY_LOG> --depth high

# With custom keywords (typically used with '--depth medium' implicitly or explicitly)
python Tools/Log/log_summarizer.py <PATH_TO_INPUT_LOG> <PATH_TO_GENERATED_SUMMARY_LOG> -k LogTurnManager LogAbilities
python Tools/Log/log_summarizer.py --latest-from-dir <PATH_TO_LOG_DIRECTORY> <PATH_TO_GENERATED_SUMMARY_LOG> --depth medium -k LogTurnManager LogAbilities
```

**Input Options:**
*   **Default Behavior:** If no input file or `--latest-from-dir` is specified, the script will automatically search for the most recently modified `.log` file in the **current directory** and use it as input.
*   **`<PATH_TO_INPUT_LOG>`**: Directly specify the path to the Unreal Engine log file you want to summarize.
*   **`--latest-from-dir <PATH_TO_LOG_DIRECTORY>`**: Instructs the script to search the specified directory for the most recently modified `.log` file and use that as the input. If the flag is used without a directory path (e.g., `python Tools/Log/log_summarizer.py --latest-from-dir output.log`), it will default to searching the **current directory** ('.'). This is useful for automatically processing the newest log generated by UE.

**Depth Options:**
*   **`--depth critical`**: Filters for only critical issues (`error:`, `fatal:`, `severe:`). Ideal for quick checks for show-stopping problems.
*   **`--depth high`**: Filters for critical issues plus general warnings (`warning:`). Useful for a broader overview of potential problems.
*   **`--depth medium` (Default)**: Includes `high` level filters and allows for additional custom keywords specified via the `-k` option. This enables targeted analysis of specific systems (e.g., `LogTurnManager`, `LogMoveVerbose`).

By default, if `--depth` is not specified, it behaves as `--depth medium`.

### 4.1.1. Naming Conventions for Generated Logs
When specifying the output file path for `log_summarizer.py` (i.e., `<PATH_TO_GENERATED_SUMMARY_LOG>`), AI agents should use descriptive and consistent naming conventions. This aids in quickly identifying the content and purpose of the generated log.

**Recommended Practices:**
*   **Include relevant keywords:** Incorporate the purpose of the filtering (e.g., `filtered`, `summary`).
*   **Reference the original log (if applicable):** If summarizing a specific raw log, consider including part of its name.
*   **Add context:** Include details like the date, time, or the specific systems analyzed (e.g., `turn_move_debug`).

**Examples:**
*   `filtered_turn_errors_20251120.log`
*   `summary_latest_critical.log`
*   `filtered_LogTurnManager_analysis.log`

### 4.1.1. Naming Conventions for Generated Logs
When specifying the output file path for `log_summarizer.py` (i.e., `<PATH_TO_GENERATED_SUMMARY_LOG>`), AI agents should use descriptive and consistent naming conventions. This aids in quickly identifying the content and purpose of the generated log.

**Recommended Practices:**
*   **Include relevant keywords:** Incorporate the purpose of the filtering (e.g., `filtered`, `summary`).
*   **Reference the original log (if applicable):** If summarizing a specific raw log, consider including part of its name.
*   **Add context:** Include details like the date, time, or the specific systems analyzed (e.g., `turn_move_debug`).

**Examples:**
*   `filtered_turn_errors_20251120.log`
*   `summary_latest_critical.log`
*   `filtered_LogTurnManager_analysis.log`




### 4.2. Encoding Best Practices for AI
**CRITICAL:** Adherence to encoding standards is mandatory to prevent data corruption.
- **Always Assume UTF-8 Input:** All text files in this project (source code, logs) should be treated as UTF-8.
- **Always Write UTF-8 Output:** Any files written or modified by AI agents must be written as **UTF-8 without BOM**.
    - **Python Example:** `open('file.txt', 'w', encoding='utf-8')`
    - **PowerShell Example:** `Set-Content -Encoding UTF8`

### 4.3. AI Log Analysis Workflow
1.  **Identify Raw Log:** Get the path to the latest UE log file.
2.  **Execute `log_summarizer.py`:** Run the script to generate a concise summary.
3.  **Read Summary Log:** Read the summary file using UTF-8 encoding.
4.  **Perform Analysis:** Analyze the summarized content.
5.  **Report/Act:** Generate reports or formulate prompts for other AIs based on the analysis.

### 4.4. Safety Operation Protocol Reminder
All AI agents must adhere to the project's master safety protocols. In case of any anomaly (garbled characters, unintended deletions), halt all operations and report to a human operator immediately.

### 4.5. Demonstration: Targeted Log Analysis for AI Context Reduction

This section details a practical application of `log_summarizer.py` to reduce AI context when analyzing large Unreal Engine logs, focusing on specific gameplay systems.

**Problem:** Large UE logs can quickly exceed AI context windows, making targeted analysis difficult.

**Solution:** Use `log_summarizer.py` with custom keywords to filter logs, focusing on relevant systems.

**Example Scenario (Turn Control & Movement Logic Debugging):**
To debug issues related to turn management and unit movement, an AI agent can be instructed to filter a raw UE log (`A_RECENT_RAW_LOG.log`) for messages from `LogTurnManager` and `LogMoveVerbose`, as well as general `Error:` and `Warning:` messages.

**Command Executed:**
```bash
python Tools/Log/log_summarizer.py A_RECENT_RAW_LOG.log FILTERED_DEMO_LOG.log -k LogTurnManager LogMoveVerbose
```

**Outcome:**
*   **Input Log (`A_RECENT_RAW_LOG.log`)**: A comprehensive raw log file (e.g., 86,239 bytes, containing various system messages, localization warnings, etc.).
*   **Output Summary (`FILTERED_DEMO_LOG.log`)**: A significantly reduced log containing only lines matching the specified keywords and default filters (e.g., **695 lines**). This output provides a focused view of turn-based events, movement debugs, and critical issues, omitting irrelevant system noise and localization warnings.

**Benefits:**
*   **Reduced AI Context:** AI agents can process a much smaller, highly relevant log, minimizing token usage and improving processing speed.
*   **Focused Analysis:** Enables AI to quickly identify and analyze issues within specific gameplay systems (e.g., turn management, movement logic) without being overwhelmed by unrelated data.
*   **Efficient Debugging:** Human developers also benefit from a concise log for quicker troubleshooting.

This demonstrates the script's effectiveness in achieving "parsing depth" for context minimization.

---

## 5. Diagnostic Logging Guidelines

*Added: 2025-11-23*

### 5.1. Core Principle: Log-Only Problem Diagnosis

**Design Rule:** All critical game logic must produce logs sufficient to diagnose problems **without** launching the game.

A well-designed logging system enables:
- ✅ **Remote Debugging:** Analyze issues from log files alone
- ✅ **Historical Analysis:** Understand past bugs from archived logs
- ✅ **AI-Assisted Debugging:** AI agents can identify root causes from logs
- ✅ **Faster Iteration:** No need to reproduce bugs in-game

### 5.2. Enemy Pathfinding Diagnostic Logs

**CodeRevision:** `INC-2025-1123-LOG-R1` (2025-11-23 01:42)

The `GetNextStepTowardsPlayer` function in `DistanceFieldSubsystem.cpp` now produces comprehensive diagnostic logs:

#### Log Output Structure

```
[GetNextStep] START: From=(X,Y) Player=(X,Y) CurrentDist=N
[GetNextStep] GoalDelta=(dX,dY) (direction to player)
[GetNextStep]   Neighbor (X,Y): BLOCKED BY TERRAIN
[GetNextStep]   Neighbor (X,Y): DIAGONAL BLOCKED (Side1=(X,Y):0/1, Side2=(X,Y):0/1)
[GetNextStep]   Neighbor (X,Y): NO IMPROVEMENT (Dist=N, Current=M)
[GetNextStep]   Neighbor (X,Y): CANDIDATE ACCEPTED (Dist=M->N, Align=A, Diag=0/1) - reason
[GetNextStep]   Neighbor (X,Y): candidate rejected (Dist=M->N, Align=A, Diag=0/1)
[GetNextStep] RESULT: From=(X,Y) -> Next=(X,Y) (Dist=M->N, Candidates=C)
```

#### Information Provided

1. **START:** Initial position, player position, current distance
2. **GoalDelta:** Direction vector to player (clamped to -1, 0, 1)
3. **BLOCKED BY TERRAIN:** Cells that are unwalkable due to terrain
4. **DIAGONAL BLOCKED:** Diagonal moves blocked by corner-cutting prevention
5. **NO IMPROVEMENT:** Cells that don't reduce distance to player
6. **CANDIDATE ACCEPTED/rejected:** Valid candidates and why they were chosen/rejected
7. **RESULT:** Final decision and movement destination

### 5.3. Using the `enemy_pathfinding` Preset

**CodeRevision:** `INC-2025-1123-LOG-R4` (2025-11-23 01:42)

The `log_summarizer.py` script includes a dedicated preset for enemy pathfinding diagnosis:

```bash
# Basic usage (analyzes last 3 turns by default)
python Tools/Log/log_summarizer.py enemy_path.txt --preset enemy_pathfinding

# Analyze specific turn
python Tools/Log/log_summarizer.py enemy_path_turn5.txt --preset enemy_pathfinding --turn 5

# Analyze turn range
python Tools/Log/log_summarizer.py enemy_path_turn3_5.txt --preset enemy_pathfinding --turn-range 3-5
```

**What it filters:**
- GetNextStep decision logs
- Terrain blocking information
- Diagonal movement validation
- Distance field values
- Candidate evaluation results

**What it excludes:**
- Input/RPC noise
- Rendering/audio logs
- Unrelated gameplay events

### 5.4. Diagnostic Workflow Example

**Problem:** Enemies not moving toward player in Y-axis direction

**Step 1:** Play game and generate logs

**Step 2:** Extract pathfinding logs
```bash
python Tools/Log/log_summarizer.py enemy_diagnosis.txt --preset enemy_pathfinding --turn-range 43-45
```

**Step 3:** Analyze the output
```
[GetNextStep] START: From=(32,16) Player=(48,18) CurrentDist=230
[GetNextStep] GoalDelta=(1,1) (direction to player)
[GetNextStep]   Neighbor (33,16): CANDIDATE ACCEPTED (Dist=230->220, Align=1, Diag=0)
[GetNextStep]   Neighbor (32,17): BLOCKED BY TERRAIN  ← Root cause identified!
[GetNextStep]   Neighbor (33,17): DIAGONAL BLOCKED (Side1=(33,16):1, Side2=(32,17):0)
[GetNextStep] RESULT: From=(32,16) -> Next=(33,16) (Dist=230->220, Candidates=1)
```

**Diagnosis:** Y=17 row is blocked by terrain, preventing vertical movement.

**Solution:** Fix dungeon generation or remove blocking terrain.

### 5.5. Best Practices for Adding Diagnostic Logs

When implementing new game logic, follow these guidelines:

#### DO:
- ✅ Log decision points with clear reasons
- ✅ Log validation failures with specific causes
- ✅ Use consistent log prefixes (e.g., `[GetNextStep]`, `[ComputeIntent]`)
- ✅ Include relevant coordinates, IDs, and state values
- ✅ Use `UE_LOG(LogCategory, Log, ...)` for always-on diagnostics
- ✅ Use `UE_LOG(LogCategory, Warning/Error, ...)` for anomalies

#### DON'T:
- ❌ Rely solely on `DIAG_LOG` (requires VerboseLogging enabled)
- ❌ Log without context (e.g., "Move failed" without coordinates)
- ❌ Use vague messages (e.g., "Error occurred")
- ❌ Omit critical decision factors (e.g., why a candidate was rejected)
- ❌ Assume developers will reproduce bugs in-game

#### Example: Good vs. Bad Logging

**Bad:**
```cpp
if (!IsValid)
{
    UE_LOG(LogAI, Warning, TEXT("Invalid move"));
    return;
}
```

**Good:**
```cpp
if (!IsValid)
{
    UE_LOG(LogAI, Warning, 
        TEXT("[ComputeIntent] %s: Move to (%d,%d) INVALID - Reason: %s (Dist=%d, Walkable=%d)"),
        *GetNameSafe(Actor), TargetCell.X, TargetCell.Y, 
        *Reason, Distance, bWalkable ? 1 : 0);
    return;
}
```

### 5.6. Log Categories Reference

| Category | Purpose | Example Usage |
|----------|---------|---------------|
| `LogDistanceField` | Pathfinding, distance calculations | GetNextStep decisions |
| `LogEnemyAI` | Enemy intent generation | ComputeIntent, CollectIntents |
| `LogTurnManager` | Turn flow, phase transitions | Turn start/end, phase changes |
| `LogGridOccupancy` | Grid reservation, conflicts | RESERVE, COMMIT, REJECT |
| `LogMoveVerbose` | Movement execution details | Path following, snapping |
| `LogAttackAbility` | Attack execution | Target selection, damage |

### 5.7. Maintenance

When modifying game logic:
1. **Update logs** to reflect new decision factors
2. **Test log output** to ensure it's understandable
3. **Update presets** in `log_summarizer.py` if new keywords are needed
4. **Document** new log patterns in this handbook

**Remember:** Good logs are as important as good code. They are your first line of defense against bugs.
