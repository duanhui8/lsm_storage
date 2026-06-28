# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Session Logging

After each substantive Q&A or development session, append a summary to `/opt/miniob/dev.txt`.
The summary should include:
- The questions asked and answers given
- Key architectural insights discovered
- Comparisons between MiniOB and OceanBase 4.4.2 where relevant
- Code paths and decisions made

Rules for dev.txt:
- If dev.txt already has content, organize and merge new material into it — never delete or
  overwrite existing content
- Structure entries with clear headers and dates so newcomers can follow the learning path
- Focus on the "why" (design rationale) not just the "what" (file names)

## Build

```bash
# First-time setup: build all third-party dependencies (libevent, googletest, benchmark, jsoncpp, replxx)
bash build.sh init

# Configure + build debug (default)
bash build.sh debug --make -j$(nproc)

# Release build
bash build.sh release --make -j$(nproc)

# With optional features
bash build.sh debug -DCONCURRENCY=ON -DWITH_BENCHMARK=ON --make -j$(nproc)

# Build a single target
cd build_debug && make -j$(nproc) observer    # server binary
cd build_debug && make -j$(nproc) unittest    # all unit tests
cd build_debug && make -j$(nproc) oblsm       # LSM static library

# Clean all build artifacts
bash build.sh clean
```

Build output: `build_debug/bin/` (executables), `build_debug/lib/` (static libraries), `build_debug/unittest/` (test binaries). A symlink `build -> build_debug` exists at the repo root.

Key CMake options (default: Debug, C++20, ASan ON):

| Option | Default | Purpose |
|--------|---------|---------|
| `ENABLE_ASAN` | ON | Address sanitizer |
| `CONCURRENCY` | OFF | Thread safety / multi-threading |
| `WITH_UNIT_TESTS` | ON | Build unit tests |
| `WITH_BENCHMARK` | OFF | Build benchmarks |
| `WITH_MEMTRACER` | OFF | Memory tracing shared library |
| `ENABLE_COVERAGE` | OFF | Code coverage (lcov) |

## Test

```bash
# All unit tests (from build directory)
cd build_debug && ctest --verbose
cd build_debug && ctest -E memtracer_test --verbose    # exclude memtracer

# Single test
cd build_debug && ctest -R bplus_tree_test --verbose
cd build_debug && ./unittest/bplus_tree_test            # run directly

# SQL integration tests
python3 test/case/miniob_test.py                        # all cases
python3 test/case/miniob_test.py --test-cases=basic     # specific case
python3 test/case/miniob_test.py --test-cases=basic --debug

# Full integration tests (requires MySQL)
cd test/integration_test && python3 ./libminiob_test.py -c conf.ini
```

Tests use Google Test. Each unittest `.cpp` file compiles to its own binary and is registered with CTest via `add_test()`. Tests live in `unittest/{common,observer,oblsm,memtracer}/`.

## Run

```bash
# TCP mode (connect with obclient or mysql client on port 6789)
./build_debug/bin/observer -f etc/observer.ini -p 6789

# CLI mode (stdin/stdout, good for debugging)
./build_debug/bin/observer -P cli

# With LSM storage engine + LSM transactions
./build_debug/bin/observer -P cli -E lsm -t lsm

# Connect client
./build_debug/bin/obclient -p 6789
```

CLI flags: `-p <port>`, `-s <unix-socket>`, `-f <config>`, `-P <protocol: cli|plain|mysql>`, `-E <storage: heap|lsm>`, `-t <trx: vacuou|mvcc|lsm>`.

## Architecture

MiniOB is an educational relational database from OceanBase. It's a C++20 CMake project (`project(minidb)`). It omits concurrency (by default), security, and complex transactions to focus on database internals.

### Source modules (`src/`)

**`common/`** — Foundation library (`libcommon.a`). Logging, I/O, threading, memory management, OS abstractions, math (CRC, MD5, SIMD), ini config parsing, and 54+ C++ std polyfill headers in `common/lang/` (e.g., `string.h` wraps `<string>` to manage namespaces). All other targets depend on this.

**`observer/`** — The database server (`observer` binary + `observer_static` library). The main codebase.

**`obclient/`** — Thin CLI client for connecting to the server.

**`oblsm/`** — Standalone LSM-tree storage engine (`liboblsm.a` + `oblsm_cli` + `oblsm_bench`). Analogous to a minimal LevelDB/RocksDB. Has its own WAL, memtable (skiplist), SSTables, compaction, manifest, block cache (LRU), and transactions. Does NOT depend on `observer`.

**`cpplings/`** — C++ learning exercises (conditionally built).

**`memtracer/`** — Memory tracing utility (conditionally built).

### SQL processing pipeline

```
ParseStage ──> ResolveStage ──> OptimizeStage ──> ExecuteStage
    │               │                │                 │
Flex/Bison     Stmt::create_stmt  LogicalPlanGen   CommandExecutor (DDL)
→ ParsedSqlNode → Stmt tree       → LogicalOp tree  or PhysicalOp (DML)
                                   → PhysicalOp tree
```

- **ParseStage** (`observer/sql/parser/`): Flex (`lex_sql.l`) + Bison (`yacc_sql.y`) produce a `ParsedSqlNode` AST. Note: parser files are auto-generated — do not edit `lex_sql.cpp`/`yacc_sql.cpp` directly; edit the `.l`/`.y` files.
- **ResolveStage** (`observer/sql/stmt/`): `Stmt::create_stmt()` binds names to DB objects → typed `Stmt` subclasses (`SelectStmt`, `InsertStmt`, etc.).
- **OptimizeStage** (`observer/sql/optimizer/`): `LogicalPlanGenerator` (Stmt → LogicalOperator tree) → rewriters (predicate pushdown) → `PhysicalPlanGenerator` (→ PhysicalOperator tree). A cascade optimizer (`observer/sql/optimizer/cascade/`) is partially implemented.
- **ExecuteStage** (`observer/sql/executor/`): DDL goes through `CommandExecutor` subclasses; DML executes the `PhysicalOperator` tree via the Volcano model (`open()` → `next()` → `close()`).

Key types in `observer/sql/expr/`: `Expression` hierarchy (Field, Value, Comparison, Conjunction, Arithmetic, Aggregate), `Tuple`, `TupleCell`.

### Storage engine (`observer/storage/`)

`Table` uses a strategy pattern: it holds a `TableEngine*` (abstract), defaulting to `HeapTableEngine`. `Db` (`storage/db/`) owns the buffer pool, log handler, transaction kit, and optionally an `ObLsm*` when `-E lsm` is passed.

- **Buffer pool** (`storage/buffer/`): `DiskBufferPool`, `BPFrameManager` (LRU), `BufferPoolManager` (maps files to pools).
- **Record management** (`storage/record/`): `Record`, `RID` (PageNum + SlotNum), `RecordFileHandler`, `RecordScanner`.
- **Index** (`storage/index/`): `BplusTreeIndex`, `IVFFlatIndex` (vector index).
- **Transactions** (`storage/trx/`): `TrxKit` factory (VACUOUS/MVCC/LSM), `Trx` base, `Operation` descriptors.
- **Redo log** (`storage/clog/`): `DiskLogHandler`, `LogEntry`, `LogReplayer`.

### LSM engine (`oblsm/`)

Internal state machine: Active MemTable (skiplist) → Immutable MemTable → SSTable (on disk). Backed by WAL, manifest, and a background compaction thread (1-thread pool).

- `ObLsmImpl` (core implementation) manages `mem_table_`, `imem_tables_`, `sstables_`, `wal_`, `manifest_`, `block_cache_`.
- Compaction: `ObCompactionPicker` selects files, `ObCompaction` merges via `ObMerger` (merge-iterator over SSTables + MemTables).
- `ObLsmTransaction` provides snapshot isolation (read-your-writes via inner store) but the stub currently delegates to `ObLsm`.

### Networking (`observer/net/`)

`Server` hierarchy: `NetServer` (libevent-based TCP/Unix socket) and `CliServer` (stdin/stdout). `Communicator` hierarchy handles wire protocol: `PlainCommunicator`, `CliCommunicator`, `MysqlCommunicator`. The `CommunicateProtocol` enum selects the protocol.

### Error handling

All functions return `RC` (defined via X-macro in `src/common/sys/rc.h`). 60+ codes covering SUCCESS, INVALID_ARGUMENT, SQL_SYNTAX, BUFFERPOOL_*, RECORD_*, SCHEMA_*, IOERR_*, LOCKED_*, FILE_*, etc. Always check return codes — exceptions are not used for control flow.

### Configuration

`ProcessParam` singleton (`src/common/os/process_param.h`) holds runtime options (port, protocol, storage engine, trx kit, thread model, buffer pool size). Accessed globally via `the_process_param()`.

## Code style

- C++20, 2-space indent, 120-char line limit (see `.clang-format`)
- Format check: CI runs `clang-format --dry-run -Werror` on PRs (parser-generated files excluded)
- License header: Mulan PSL v2 (see existing files for template)
- Use `common/lang/` polyfill headers instead of raw `<string>`, `<vector>` etc. to keep namespace management consistent
# CLAUDE.md

Behavioral guidelines to reduce common LLM coding mistakes. Merge with project-specific instructions as needed.

**Tradeoff:** These guidelines bias toward caution over speed. For trivial tasks, use judgment.

## 1. Think Before Coding

**Don't assume. Don't hide confusion. Surface tradeoffs.**

Before implementing:
- State your assumptions explicitly. If uncertain, ask.
- If multiple interpretations exist, present them - don't pick silently.
- If a simpler approach exists, say so. Push back when warranted.
- If something is unclear, stop. Name what's confusing. Ask.

## 2. Simplicity First

**Minimum code that solves the problem. Nothing speculative.**

- No features beyond what was asked.
- No abstractions for single-use code.
- No "flexibility" or "configurability" that wasn't requested.
- No error handling for impossible scenarios.
- If you write 200 lines and it could be 50, rewrite it.

Ask yourself: "Would a senior engineer say this is overcomplicated?" If yes, simplify.

## 3. Surgical Changes

**Touch only what you must. Clean up only your own mess.**

When editing existing code:
- Don't "improve" adjacent code, comments, or formatting.
- Don't refactor things that aren't broken.
- Match existing style, even if you'd do it differently.
- If you notice unrelated dead code, mention it - don't delete it.

When your changes create orphans:
- Remove imports/variables/functions that YOUR changes made unused.
- Don't remove pre-existing dead code unless asked.

The test: Every changed line should trace directly to the user's request.

## 4. Goal-Driven Execution

**Define success criteria. Loop until verified.**

Transform tasks into verifiable goals:
- "Add validation" → "Write tests for invalid inputs, then make them pass"
- "Fix the bug" → "Write a test that reproduces it, then make it pass"
- "Refactor X" → "Ensure tests pass before and after"

For multi-step tasks, state a brief plan:
```
1. [Step] → verify: [check]
2. [Step] → verify: [check]
3. [Step] → verify: [check]
```

Strong success criteria let you loop independently. Weak criteria ("make it work") require constant clarification.

---

**These guidelines are working if:** fewer unnecessary changes in diffs, fewer rewrites due to overcomplication, and clarifying questions come before implementation rather than after mistakes.
