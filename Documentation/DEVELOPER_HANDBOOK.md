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
To manage AI context size, use the `log_summarizer.py` script to parse large Unreal Engine logs.

**Usage:**
```bash
# Basic usage
python log_summarizer.py <PATH_TO_INPUT_LOG> <PATH_TO_OUTPUT_SUMMARY>

# With custom keywords
python log_summarizer.py <INPUT_LOG> <OUTPUT_SUMMARY> -k LogTurnManager LogAbilities
```

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
