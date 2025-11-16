# Repository Guidelines

## Project Structure & Module Organization
- Source code lives at the top level of this folder with domain-specific subvfolders: `Abilities/`, `AI/`, `Character/`, `Grid/`, `Player/`, `Turn/`, `Utility/`, plus `Data/` for configuration and `Debug/` for temporary helpers. `Documentation/` hosts procedural references (`BUILD_INSTRUCTIONS.md`, `CODE_REVISION.md`, `REFACTORING_PLAN.md`), while `README.md` provides the high-level overview. Keep new subsystems grouped by these directories to match the existing module layout and avoid scatter.

## Build, Test, and Development Commands
- Preferred build: run the UnrealBuildTool command from `Documentation/BUILD_INSTRUCTIONS.md`, e.g.
  ```cmd
  "C:\Program Files\Epic Games\UE_5.6\Engine\Binaries\DotNET\UnrealBuildTool\UnrealBuildTool.exe" LyraEditor Win64 Development -Project="C:\UnrealProjects\RoguelikeDungeon\RoguelikeDungeon.uproject" -WaitMutex -NoHotReload -Verbose -NoUBA -Log="C:\UnrealProjects\RoguelikeDungeon\UBT_noUBA.log"
  ```
  Parameters are annotated in the same document; consult it whenever the command needs tweaking. A fallback is the legacy `Build.bat` invocation from the `Engine\Build\BatchFiles` directory (same target and platform flags, but relies on the Unreal Build Accelerator). Always confirm Visual Studio 2022 and Unreal Engine 5.6 are installed before kicking off a build.
- Development loop: open `LyraEditor` (the UE editor built from this target), use Play-In-Editor (PIE) to exercise turn-based flows, and note any manual reproduction steps in a tracking doc or the `CODE_REVISION.md` log.

## Coding Style & Naming Conventions
- Follow Unreal Engine C++ style conventions—use tabs for indentation, PascalCase for `A`/`U`/`F` classes, and prefix member variables with `b`, `My`, etc., as in surrounding code. Keep header/tooling hooks in the same folder as their matching module (e.g., `AI/Enemy` inside `AI/`).
- Every code modification must include a `CodeRevision` comment (format: `CodeRevision: <ID> (<description>) (YYYY-MM-DD HH:MM)`) near the affected logic and an entry in `Documentation/CODE_REVISION.md`. Keep all new comments in English and link the code comment and log entry to the same ID for traceability.

## Testing Guidelines
- There is no automated C++ test suite in this project yet; rely on manual validation using the UE5 editor. After building, launch `LyraEditor`, resize the dungeon floor, run through a player turn, trigger an ability or AI turn, and verify the subsystem updates as expected. Document the scenario, steps, and results either inline in code (as part of the `CodeRevision` comment) or in `Documentation/REFACTORING_PLAN.md` if it affects architecture.
- For any workaround or bug fix, list the reproduction steps and expected outcome in the PR description so reviewers can reproduce the issue locally.

## Commit & Pull Request Guidelines
- Reuse the apparent commit pattern seen in recent history (e.g., `eod: 2025-11-16`) so the log is easy to scan; keep the summary short, and mention the subsystem touched.
- PRs should include a concise description of the change, related issue numbers or `CodeRevision` IDs, the manual tests performed (command or editor scenario), and, if appropriate, a screenshot of the UI or gameplay change. Update `CODE_REVISION.md` before merging so reviewers can cross-check your comment ID with the log entry.

## Documentation & Operational Notes
- Read `Documentation/BUILD_INSTRUCTIONS.md` before building, especially the troubleshooting tips for UBA-induced `9666` errors and how to interpret `C:\UnrealProjects\RoguelikeDungeon\UBT_noUBA.log`. For architecture-level changes, refer to `Documentation/REFACTORING_PLAN.md` and annotate decisions there when they affect routing or turn management.
- Keep this guide brief; if you add new tooling or workflows, update `AGENTS.md` as well so future contributors understand the preferred path.
