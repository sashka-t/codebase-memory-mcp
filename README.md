# codebase-memory-mcp

[![GitHub Release](https://img.shields.io/github/v/release/DeusData/codebase-memory-mcp?style=flat&color=blue)](https://github.com/DeusData/codebase-memory-mcp/releases/latest)
[![License](https://img.shields.io/badge/license-MIT-green)](LICENSE)
[![CI](https://img.shields.io/github/actions/workflow/status/DeusData/codebase-memory-mcp/dry-run.yml?label=CI)](https://github.com/DeusData/codebase-memory-mcp/actions/workflows/dry-run.yml)
[![Tests](https://img.shields.io/badge/tests-2745_passing-brightgreen)](https://github.com/DeusData/codebase-memory-mcp)
[![Languages](https://img.shields.io/badge/languages-66-orange)](https://github.com/DeusData/codebase-memory-mcp)
[![Agents](https://img.shields.io/badge/agents-10-purple)](https://github.com/DeusData/codebase-memory-mcp)
[![Pure C](https://img.shields.io/badge/pure_C-zero_dependencies-blue)](https://github.com/DeusData/codebase-memory-mcp)
[![Platform](https://img.shields.io/badge/macOS_%7C_Linux_%7C_Windows-supported-lightgrey)](https://github.com/DeusData/codebase-memory-mcp/releases/latest)
[![OpenSSF Scorecard](https://api.scorecard.dev/projects/github.com/DeusData/codebase-memory-mcp/badge)](https://scorecard.dev/viewer/?uri=github.com/DeusData/codebase-memory-mcp)
[![SLSA 3](https://slsa.dev/images/gh-badge-level3.svg)](https://slsa.dev)
[![VirusTotal](https://img.shields.io/badge/VirusTotal-0%2F72_engines-brightgreen?logo=virustotal)](https://www.virustotal.com/gui/file/dcbe9a951a2b1f7ec6d003edce2f38b586f74bf8cf98faeedec36f1dd3444b06/detection)
[![arXiv](https://img.shields.io/badge/arXiv-2603.27277-b31b1b?logo=arxiv)](https://arxiv.org/abs/2603.27277)

**The fastest and most efficient code intelligence engine for AI coding agents.** Full-indexes an average repository in milliseconds, the Linux kernel (28M LOC, 75K files) in 3 minutes. Answers structural queries in under 1ms. Ships as a single static binary for macOS, Linux, and Windows — download, run `install`, done.

High-quality parsing through [tree-sitter](https://tree-sitter.github.io/tree-sitter/) AST analysis across all 66 languages, enhanced with LSP-style hybrid type resolution for Go, C, and C++ (more languages coming soon) — producing a persistent knowledge graph of functions, classes, call chains, HTTP routes, and cross-service links. 19 MCP tools. Zero dependencies. Plug and play across 10 coding agents plus JetBrains IDEs.

> **Research** — The design and benchmarks behind this project are described in the preprint [*Codebase-Memory: Tree-Sitter-Based Knowledge Graphs for LLM Code Exploration via MCP*](https://arxiv.org/abs/2603.27277) (arXiv:2603.27277). Evaluated across 31 real-world repositories: 83% answer quality, 10× fewer tokens, 2.1× fewer tool calls vs. file-by-file exploration.

> **Security & Trust** — This tool reads your codebase and writes to your agent configuration files. That is what it is designed to do. If you prefer to audit before running, the [full source is here](https://github.com/DeusData/codebase-memory-mcp) — every release binary is signed, checksummed, and scanned by 70+ antivirus engines. All processing happens 100% locally; your code never leaves your machine. Found a security issue? We want to know — see [SECURITY.md](SECURITY.md). Security is Priority #1 for us.

<p align="center">
  <img src="docs/graph-ui-screenshot.png" alt="Graph visualization UI showing the codebase-memory-mcp knowledge graph" width="800">
  <br>
  <em>Built-in 3D graph visualization (UI variant) — explore your knowledge graph at localhost:9749</em>
</p>

## Why codebase-memory-mcp

- **Extreme indexing speed** — Linux kernel (28M LOC, 75K files) in 3 minutes. RAM-first pipeline: LZ4 compression, in-memory SQLite, fused Aho-Corasick pattern matching. Memory released after indexing.
- **Plug and play** — single static binary for macOS (arm64/amd64), Linux (arm64/amd64), and Windows (amd64). No Docker, no runtime dependencies, no API keys. Download → `install` → restart agent → done.
- **66 languages** — vendored tree-sitter grammars compiled into the binary. Nothing to install, nothing that breaks.
- **120x fewer tokens** — 5 structural queries: ~3,400 tokens vs ~412,000 via file-by-file search. One graph query replaces dozens of grep/read cycles.
- **Agents and IDEs, one command** — `install` auto-detects Claude Code, Codex CLI, Gemini CLI, Zed, OpenCode, Antigravity, Aider, KiloCode, VS Code, OpenClaw, and JetBrains IDEs (IntelliJ IDEA, CLion, PyCharm, GoLand, WebStorm, Rider) — configures MCP entries, instruction files, and pre-tool hooks where supported.
- **Built-in graph visualization** — 3D interactive UI at `localhost:9749` (optional UI binary variant).
- **Infrastructure-as-code indexing** — Dockerfiles, Kubernetes manifests, and Kustomize overlays indexed as graph nodes with cross-references. `Resource` nodes for K8s kinds, `Module` nodes for Kustomize overlays with `IMPORTS` edges to referenced resources.
- **19 MCP tools** — graph search/trace, Cypher, change impact, ADRs, runtime traces, and **test runs / JUnit ingestion / failure tracing** — all in one binary.

## Quick Start

**One-line install** (macOS / Linux):
```bash
curl -fsSL https://raw.githubusercontent.com/DeusData/codebase-memory-mcp/main/install.sh | bash
```

With graph visualization UI:
```bash
curl -fsSL https://raw.githubusercontent.com/DeusData/codebase-memory-mcp/main/install.sh | bash -s -- --ui
```

**Windows** (PowerShell):
```powershell
# 1. Download the installer
Invoke-WebRequest -Uri https://raw.githubusercontent.com/DeusData/codebase-memory-mcp/main/install.ps1 -OutFile install.ps1

# 2. (Optional but recommended) Inspect the script
notepad install.ps1

# 3. Run it
.\install.ps1

```

Options: `--ui` (graph visualization), `--skip-config` (binary only, no agent setup), `--dir=<path>` (custom location).

Restart your coding agent. Say **"Index this project"** — done.

<details>
<summary>Manual install</summary>

1. **Download** the archive for your platform from the [latest release](https://github.com/DeusData/codebase-memory-mcp/releases/latest):
   - `codebase-memory-mcp-<os>-<arch>.tar.gz` (macOS/Linux) or `.zip` (Windows) — standard
   - `codebase-memory-mcp-ui-<os>-<arch>.tar.gz` / `.zip` — with graph visualization

2. **Extract and install** (each archive includes `install.sh` or `install.ps1`):

   macOS / Linux:
   ```bash
   tar xzf codebase-memory-mcp-*.tar.gz
   ./install.sh
   ```

   Windows (PowerShell):
   ```powershell
   Expand-Archive codebase-memory-mcp-windows-amd64.zip -DestinationPath .
   .\install.ps1
   ```

3. **Restart** your coding agent.

The `install` command automatically strips macOS quarantine attributes and ad-hoc signs the binary — no manual `xattr`/`codesign` needed.
</details>

The `install` command auto-detects installed coding agents and supported JetBrains IDEs, then configures MCP server entries, instruction files, skills, and pre-tool hooks where available.

### Graph Visualization UI

If you downloaded the `ui` variant:

```bash
codebase-memory-mcp --ui=true --port=9749
```

Open `http://localhost:9749` in your browser. The UI runs as a background thread alongside the MCP server — it's available whenever your agent is connected.

### Auto-Index

Enable automatic indexing on MCP session start:

```bash
codebase-memory-mcp config set auto_index true
```

When enabled, new projects are indexed automatically on first connection. Previously-indexed projects are registered with the background watcher for ongoing git-based change detection. Configurable file limit: `config set auto_index_limit 50000`.

### Keeping Up to Date

```bash
codebase-memory-mcp update
```

The MCP server also checks for updates on startup and notifies on the first tool call if a newer release is available.

### Uninstall

```bash
codebase-memory-mcp uninstall
```

Removes all agent configs, skills, hooks, and instructions. Does not remove the binary or SQLite databases.

## Features

- **Architecture overview**: `get_architecture` returns languages, packages, entry points, routes, hotspots, boundaries, layers, and clusters in a single call
- **Architecture Decision Records**: `manage_adr` persists architectural decisions across sessions
- **Louvain community detection**: Discovers functional modules by clustering call edges
- **Git diff impact mapping**: `detect_changes` maps uncommitted changes to affected symbols with risk classification
- **Call graph**: Resolves function calls across files and packages (import-aware, type-inferred)
- **Cross-service HTTP linking**: Discovers REST routes and matches them to HTTP call sites with confidence scoring
- **Auto-sync**: Background watcher detects file changes and re-indexes automatically
- **Cypher-like queries**: `MATCH (f:Function)-[:CALLS]->(g) WHERE f.name = 'main' RETURN g.name`
- **Dead code detection**: Finds functions with zero callers, excluding entry points
- **Route nodes**: REST endpoints are first-class graph entities
- **CLI mode**: `codebase-memory-mcp cli search_graph '{"name_pattern": ".*Handler.*"}'`
- **Single binary, zero infrastructure**: SQLite-backed, persists to `~/.cache/codebase-memory-mcp/`

## How It Works

codebase-memory-mcp is a **structural analysis backend** — it builds and queries the knowledge graph. It does **not** include an LLM. Instead, it relies on your MCP client (Claude Code, or any MCP-compatible agent) to be the intelligence layer.

```
You: "what calls ProcessOrder?"

Agent calls: trace_path(function_name="ProcessOrder", direction="inbound")

codebase-memory-mcp: executes graph query, returns structured results

Agent: presents the call chain in plain English
```

**Why no built-in LLM?** Other code graph tools embed an LLM for natural language → graph query translation. This means extra API keys, extra cost, and another model to configure. With MCP, the agent you're already talking to *is* the query translator.

## Performance

Benchmarked on Apple M3 Pro:

| Operation | Time | Notes |
|-----------|------|-------|
| **Linux kernel full index** | **3 min** | 28M LOC, 75K files → 2.1M nodes, 4.9M edges |
| Linux kernel fast index | 1m 12s | 1.88M nodes |
| Django full index | ~6s | 49K nodes, 196K edges |
| Cypher query | <1ms | Relationship traversal |
| Name search (regex) | <10ms | SQL LIKE pre-filtering |
| Dead code detection | ~150ms | Full graph scan with degree filtering |
| Trace call path (depth=5) | <10ms | BFS traversal |

**RAM-first pipeline**: All indexing runs in memory (LZ4 HC compressed read, in-memory SQLite, single dump at end). Memory is released back to the OS after indexing completes.

**Token efficiency**: Five structural queries consumed ~3,400 tokens via codebase-memory-mcp versus ~412,000 tokens via file-by-file grep exploration — a **99.2% reduction**.

## Installation

### Pre-built Binaries

| Platform | Standard | With Graph UI |
|----------|----------|---------------|
| macOS (Apple Silicon) | `codebase-memory-mcp-darwin-arm64.tar.gz` | `codebase-memory-mcp-ui-darwin-arm64.tar.gz` |
| macOS (Intel) | `codebase-memory-mcp-darwin-amd64.tar.gz` | `codebase-memory-mcp-ui-darwin-amd64.tar.gz` |
| Linux (x86_64) | `codebase-memory-mcp-linux-amd64.tar.gz` | `codebase-memory-mcp-ui-linux-amd64.tar.gz` |
| Linux (ARM64) | `codebase-memory-mcp-linux-arm64.tar.gz` | `codebase-memory-mcp-ui-linux-arm64.tar.gz` |
| Windows (x86_64) | `codebase-memory-mcp-windows-amd64.zip` | `codebase-memory-mcp-ui-windows-amd64.zip` |

Every release includes `checksums.txt` with SHA-256 hashes. All binaries are statically linked — no shared library dependencies.

> **Windows note**: SmartScreen may show a warning for unsigned software. Click **"More info"** → **"Run anyway"**. Verify integrity with `checksums.txt`.

### Setup Scripts

<details>
<summary>Automated download + install</summary>

**macOS / Linux:**

```bash
curl -fsSL https://raw.githubusercontent.com/DeusData/codebase-memory-mcp/main/scripts/setup.sh | bash
```

**Windows (PowerShell):**

```powershell
irm https://raw.githubusercontent.com/DeusData/codebase-memory-mcp/main/scripts/setup-windows.ps1 | iex
```

</details>

### Install via Claude Code

```
You: "Install this MCP server: https://github.com/DeusData/codebase-memory-mcp"
```

### Build from Source

<details>
<summary>Prerequisites: C compiler + zlib</summary>

| Requirement | Check | Install |
|-------------|-------|---------|
| **C compiler** (gcc or clang) | `gcc --version` or `clang --version` | macOS: `xcode-select --install`, Linux: `apt install build-essential` |
| **C++ compiler** | `g++ --version` or `clang++ --version` | Same as above |
| **zlib** | — | macOS: included, Linux: `apt install zlib1g-dev` |
| **Git** | `git --version` | Pre-installed on most systems |

</details>

```bash
git clone https://github.com/DeusData/codebase-memory-mcp.git
cd codebase-memory-mcp
scripts/build.sh                    # standard binary
scripts/build.sh --with-ui          # with graph visualization
# Binary at: build/c/codebase-memory-mcp
```

**Wire MCP to a stable path:** running **`./build/c/codebase-memory-mcp install`** (after build) copies the binary to **`~/.local/bin/codebase-memory-mcp`**, then writes MCP configs with that absolute path — so agents do not keep pointing at a transient `build/c/...` location.

To copy the binary yourself (then run `install` for skills/hooks, or edit MCP by hand):

```bash
mkdir -p ~/.local/bin
cp build/c/codebase-memory-mcp ~/.local/bin/
chmod +x ~/.local/bin/codebase-memory-mcp
```

### Help commands (macOS: clean cache, install binary, index current repo)

From the repository root (after `scripts/build.sh` or `make -f Makefile.cbm cbm`). Resets the on-disk index cache, copies your **local build** into `~/.local/bin`, applies Gatekeeper-friendly signing, runs `install`, then indexes the current working tree. Set `BRANCH` so the project name is unique per git branch (same idea as `project_name` in the worktree examples below).

```bash
rm -rf ~/.cache/codebase-memory-mcp
mkdir -p ~/.cache/codebase-memory-mcp
cp build/c/codebase-memory-mcp ~/.local/bin/codebase-memory-mcp
codesign --sign - --force ~/.local/bin/codebase-memory-mcp
xattr -d com.apple.quarantine ~/.local/bin/codebase-memory-mcp 2>/dev/null || true
codebase-memory-mcp install
BRANCH=$(git branch --show-current)
codebase-memory-mcp cli index_repository '{"repo_path": "'$(pwd)'", "project_name": "repo-'"$BRANCH"'"}'
```

### Manual MCP Configuration

<details>
<summary>If you prefer not to use the install command</summary>

Add to `~/.claude/.mcp.json` (global) or project `.mcp.json`:

```json
{
  "mcpServers": {
    "codebase-memory-mcp": {
      "command": "/path/to/.local/bin/codebase-memory-mcp",
      "args": []
    }
  }
}
```

Restart your agent. Verify with `/mcp` — you should see `codebase-memory-mcp` with 19 tools.

</details>

## Multi-Agent Support

`install` auto-detects and configures all installed agents:

| Agent | MCP Config | Instructions | Hooks |
|-------|-----------|-------------|-------|
| Claude Code | `.claude/.mcp.json` | 4 Skills | PreToolUse (Grep/Glob/Read reminder) |
| Codex CLI | `.codex/config.toml` | `.codex/AGENTS.md` | — |
| Gemini CLI | `.gemini/settings.json` | `.gemini/GEMINI.md` | BeforeTool (grep/read reminder) |
| Zed | `settings.json` (JSONC) | — | — |
| OpenCode | `opencode.json` | `AGENTS.md` | — |
| Antigravity | `mcp_config.json` | `AGENTS.md` | — |
| Aider | — | `CONVENTIONS.md` | — |
| KiloCode | `mcp_settings.json` | `~/.kilocode/rules/` | — |
| VS Code | `Code/User/mcp.json` | — | — |
| OpenClaw | `openclaw.json` | — | — |
| IntelliJ IDEA | `JetBrains/IntelliJIdea*/options/mcp.json` | — | — |
| CLion | `JetBrains/CLion*/options/mcp.json` | — | — |
| PyCharm | `JetBrains/PyCharm*/options/mcp.json` | — | — |
| GoLand | `JetBrains/GoLand*/options/mcp.json` | — | — |
| WebStorm | `JetBrains/WebStorm*/options/mcp.json` | — | — |
| Rider | `JetBrains/Rider*/options/mcp.json` | — | — |

**Hooks** are advisory (exit code 0) — they remind agents to prefer MCP graph tools when they reach for grep/glob/read, without blocking the tool call.

## CLI Mode

Every MCP tool can be invoked from the command line:

```bash
codebase-memory-mcp cli index_repository '{"repo_path": "/path/to/repo"}'
codebase-memory-mcp cli search_graph '{"name_pattern": ".*Handler.*", "label": "Function"}'
codebase-memory-mcp cli trace_path '{"function_name": "Search", "direction": "both"}'
codebase-memory-mcp cli query_graph '{"query": "MATCH (f:Function) RETURN f.name LIMIT 5"}'
codebase-memory-mcp cli list_projects
codebase-memory-mcp cli --raw search_graph '{"label": "Function"}' | jq '.results[].name'
```

## MCP Tools

There are **19** tools, grouped below by role.

### Indexing & projects

| Tool | Description |
|------|-------------|
| `index_repository` | Index a repository into the graph. Auto-sync keeps it fresh after that. |
| `list_projects` | List all indexed projects with node/edge counts. |
| `delete_project` | Remove a project and all its graph data. |
| `index_status` | Check indexing status of a project. |

### Search & navigation

| Tool | Description |
|------|-------------|
| `search_graph` | BM25 full-text, regex `name_pattern` / `qn_pattern`, optional semantic vector search (`semantic_query` array). Pagination via `limit` / `offset`. |
| `search_code` | Grep-like text search within indexed project files, ranked with graph context. |
| `get_code_snippet` | Read source for a symbol by qualified name (use `search_graph` first). |
| `get_graph_schema` | Node labels, edge types, and relationship patterns — run early in a session. |
| `get_architecture` | High-level overview: packages, services, structure. |

### Traces, impact & ADRs

| Tool | Description |
|------|-------------|
| `trace_path` | BFS over `CALLS` / `DATA_FLOWS` / cross-service routes — callers, callees, data flow, blast radius. |
| `query_graph` | Read-only Cypher against the stored graph. |
| `detect_changes` | Map git diff to changed files and impacted symbols (with optional risk labels). |
| `manage_adr` | Create or update Architecture Decision Records. |
| `ingest_traces` | Ingest runtime traces to validate or enrich `HTTP_CALLS` / async edges. |

### Test output

| Tool | Description |
|------|-------------|
| `run_tests` | Run a **whitelisted** test command (`go test`, `pytest`, `mvn`, Gradle, `sbt`, …), parse structured output, optionally persist a `run_id` for follow-up tools. |
| `ingest_test_reports` | Parse existing JUnit-style XML (and related formats) from disk under `cwd` or `report_dir`; use `persist` / `run_id` to keep results in the server session. |
| `query_test_results` | Filter cases in a persisted run by `status`, name, or suite pattern. |
| `list_test_runs` | List persisted runs (optionally filtered by `project`). |
| `trace_test_failures` | For failed/error tests, walk `TESTS` edges to production symbols and list inbound `CALLS` up to `depth`. |

## Graph Data Model

### Node Labels

`Project`, `Package`, `Folder`, `File`, `Module`, `Class`, `Function`, `Method`, `Interface`, `Enum`, `Type`, `Route`, `Resource`

### Edge Types

`CONTAINS_PACKAGE`, `CONTAINS_FOLDER`, `CONTAINS_FILE`, `DEFINES`, `DEFINES_METHOD`, `IMPORTS`, `CALLS`, `HTTP_CALLS`, `ASYNC_CALLS`, `IMPLEMENTS`, `HANDLES`, `USAGE`, `CONFIGURES`, `WRITES`, `MEMBER_OF`, `TESTS`, `USES_TYPE`, `FILE_CHANGES_WITH`

### Qualified Names

`get_code_snippet` uses qualified names: `<project>.<path_parts>.<name>`. Use `search_graph` to discover them first.

### Supported Cypher Subset

`query_graph` supports: `MATCH` with labels and relationship types, variable-length paths, `WHERE` with comparisons/regex/CONTAINS, `RETURN` with property access and `COUNT`/`DISTINCT`, `ORDER BY`, `LIMIT`. Not supported: `WITH`, `COLLECT`, `OPTIONAL MATCH`, mutations.

## Ignoring Files

Layered: hardcoded patterns (`.git`, `node_modules`, etc.) → `.gitignore` hierarchy → `.cbmignore` (project-specific, gitignore syntax). Symlinks are always skipped.

## Git Worktrees

Git worktrees (`git worktree add`) let you check out multiple branches simultaneously into separate directories. codebase-memory-mcp treats each worktree as an independent project — index them individually using their directory path:

```bash
# Add a worktree for a feature branch
git worktree add ../my-repo-feature feature/my-branch

# Index the main working tree
index_repository(repo_path="/path/to/my-repo")

# Index the feature worktree as a separate project
index_repository(repo_path="/path/to/my-repo-feature", project_name="my-repo-feature")
```

Or via CLI:

```bash
codebase-memory-mcp cli index_repository '{"repo_path": "/path/to/my-repo-feature", "project_name": "my-repo-feature"}'
```

**What works out of the box:**
- `.gitignore` patterns are loaded correctly (in worktrees `.git` is a file, not a directory — the tool handles both)
- `detect_changes` compares against the worktree's current branch
- The background watcher tracks each worktree independently, polling for HEAD changes
- Git history coupling (`FILE_CHANGES_WITH` edges) uses the shared commit history

**Detecting changes in a worktree:**

```
detect_changes(project="my-repo-feature", base_branch="main")
```

Use the `since` parameter to scope changes to a time range or specific commit:

```
# Changes since a date
detect_changes(project="my-repo-feature", since="2026-01-01")

# Changes since a commit or tag
detect_changes(project="my-repo-feature", since="HEAD~10")
detect_changes(project="my-repo-feature", since="v1.2.0")
```

## Configuration

```bash
codebase-memory-mcp config list                          # show all settings
codebase-memory-mcp config set auto_index true           # auto-index on session start
codebase-memory-mcp config set auto_index_limit 50000    # max files for auto-index
codebase-memory-mcp config reset auto_index              # reset to default
```

### Environment Variables

| Variable | Default | Description |
|----------|---------|-------------|
| `CBM_CACHE_DIR` | `~/.cache/codebase-memory-mcp` | Override the database storage directory. All project indexes and config are stored here. |
| `CBM_DIAGNOSTICS` | `false` | Set to `1` or `true` to enable periodic diagnostics output to `/tmp/cbm-diagnostics-<pid>.json`. |
| `CBM_DOWNLOAD_URL` | *(GitHub releases)* | Override the download URL for updates. Used for testing or self-hosted deployments. |

```bash
# Store indexes in a custom directory
export CBM_CACHE_DIR=~/my-projects/cbm-data
```

## Custom File Extensions

Map additional file extensions to supported languages via JSON config files. Useful for framework-specific extensions like `.blade.php` (Laravel) or `.mjs` (ES modules).

**Per-project** (in your repo root):
```json
// .codebase-memory.json
{"extra_extensions": {".blade.php": "php", ".mjs": "javascript"}}
```

**Global** (applies to all projects):
```json
// ~/.config/codebase-memory-mcp/config.json  (or $XDG_CONFIG_HOME/...)
{"extra_extensions": {".twig": "html", ".phtml": "php"}}
```

Project config overrides global for conflicting extensions. Unknown language values are silently skipped. Missing config files are ignored.

## Persistence

SQLite databases stored at `~/.cache/codebase-memory-mcp/`. Persists across restarts (WAL mode, ACID-safe). To reset: `rm -rf ~/.cache/codebase-memory-mcp/`.

## Troubleshooting

| Problem | Fix |
|---------|-----|
| `/mcp` doesn't show the server | Check `.mcp.json` path is absolute. Restart agent. Test: `echo '{}' \| /path/to/binary` should output JSON. |
| `index_repository` fails | Pass absolute path: `index_repository(repo_path="/absolute/path")` |
| `trace_path` returns 0 results | Use `search_graph(name_pattern=".*PartialName.*")` first to find the exact name. |
| Queries return wrong project results | Add `project="name"` parameter. Use `list_projects` to see names. |
| Binary not found after install | Add to PATH: `export PATH="$HOME/.local/bin:$PATH"` |
| UI not loading | Ensure you downloaded the `ui` variant and ran `--ui=true`. Check `http://localhost:9749`. |
| Git worktree `.gitignore` not respected | Ensure you are on v0.6.1+. Older versions only loaded `.gitignore` when `.git` was a directory; worktrees use a `.git` file. |

## Language Support

66 languages. Benchmarked against 64 real open-source repositories (78 to 49K nodes):

| Tier | Score | Languages |
|------|-------|-----------|
| **Excellent** (>= 90%) | | Lua, Kotlin, C++, Perl, Objective-C, Groovy, C, Bash, Zig, Swift, CSS, YAML, TOML, HTML, SCSS, HCL, Dockerfile |
| **Good** (75-89%) | | Python, TypeScript, TSX, Go, Rust, Java, R, Dart, JavaScript, Erlang, Elixir, Scala, Ruby, PHP, C#, SQL |
| **Functional** (< 75%) | | OCaml, Haskell |

Plus: Clojure, F#, Julia, Vim Script, Nix, Common Lisp, Elm, Fortran, CUDA, COBOL, Verilog, Emacs Lisp, MATLAB, Lean 4, FORM, Magma, Wolfram, JSON, XML, Markdown, Makefile, CMake, Protobuf, GraphQL, Vue, Svelte, Meson, GLSL, INI.

## Architecture

```
src/
  main.c              Entry point (MCP stdio server + CLI + install/update/config)
  mcp/                MCP server (19 tools, JSON-RPC 2.0, session detection, auto-index)
  cli/                Install/uninstall/update/config (agents, JetBrains IDEs, hooks, instructions)
  store/              SQLite graph storage (nodes, edges, traversal, search, Louvain)
  pipeline/           Multi-pass indexing (structure → definitions → calls → HTTP links → config → tests)
  cypher/             Cypher query lexer, parser, planner, executor
  discover/           File discovery (.gitignore, .cbmignore, symlink handling)
  watcher/            Background auto-sync (git polling, adaptive intervals)
  traces/             Runtime trace ingestion
  ui/                 Embedded HTTP server + 3D graph visualization
  foundation/         Platform abstractions (threads, filesystem, logging, memory)
internal/cbm/         Vendored tree-sitter grammars (66 languages) + AST extraction engine
```

## Security

Every release binary is verified through a multi-layer pipeline before publication:

- **VirusTotal** — all binaries scanned by 70+ antivirus engines (zero detections required to publish)
- **SLSA Level 3** — cryptographic build provenance generated by GitHub Actions; verify with `gh attestation verify <file> --repo DeusData/codebase-memory-mcp`
- **Sigstore cosign** — keyless signatures on all artifacts; bundles included in every release
- **SHA-256 checksums** — `checksums.txt` published with every release; verified by both install scripts before extraction
- **CodeQL SAST** — blocks release pipeline if any open alerts remain
- **Zero runtime dependencies** — no transitive supply chain; all libraries vendored at compile time

### v0.6.0 VirusTotal scans

| Binary | SHA-256 | VirusTotal |
|--------|---------|-----------|
| `linux-amd64` | `dcbe9a951a2b1f7ec6d0...` | [0/72 ✅](https://www.virustotal.com/gui/file/dcbe9a951a2b1f7ec6d003edce2f38b586f74bf8cf98faeedec36f1dd3444b06/detection) |
| `linux-arm64` | `3dc702d2ff2b5a7e9094...` | [0/72 ✅](https://www.virustotal.com/gui/file/3dc702d2ff2b5a7e909409337a8a24ba3f724e7e47d6b159b3c9dedf70117fe2/detection) |
| `darwin-arm64` | `61d543c9c795471702...` | [0/72 ✅](https://www.virustotal.com/gui/file/61d543c9c79547170296badddcdfe117b145471361d86606c7094d41aea2644f/detection) |
| `darwin-amd64` | `eea862d705ac9b44a7bd...` | [0/72 ✅](https://www.virustotal.com/gui/file/eea862d705ac9b44a7bd595bfcd1c5c36aa3409ae6e7f0a2454308024c205e40/detection) |
| `windows-amd64` | `dd828ee0d790f9d81c9b...` | [0/72 ✅](https://www.virustotal.com/gui/file/dd828ee0d790f9d81c9bde348db8d5681d624f786bba0e1b5e6c9409534c7a28/detection) |

Scan links for every release are also included in the GitHub Release notes automatically.

## License

MIT
