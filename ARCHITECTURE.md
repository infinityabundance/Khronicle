# Architecture

This document describes intended structure and guiding principles; it does not
promise behavior, correctness, or long-term adherence.

## Overview

Khronicle is built as a local daemon with multiple clients. The daemon performs
continuous ingestion and stores results in SQLite. Clients (GUI, tray, CLI) read
from the daemon via a local JSON-RPC API or directly from the database (CLI).
This keeps ingestion centralized, while UIs and tools remain thin and data-driven.

## Major Components

### Daemon (`khronicle-daemon`)

Responsibilities:

- Periodic ingestion of pacman log and journalctl entries
- Snapshotting of current system state
- Risk tagging and watchpoint evaluation
- SQLite persistence via `KhronicleStore`
- Local JSON-RPC API via `KhronicleApiServer`

The daemon is a long-running user process because ingestion is time-based and
should be continuous. SQLite is used for durability, portability, and the ability
to query historical change without a separate service dependency.

### UI (`khronicle`)

The GUI uses Qt6/Kirigami and a data-driven QML layer. The C++ backend provides
a `KhronicleApiClient` bridge to the daemon, plus fleet-specific models for
reading aggregate JSON. The UI respects KDE theming and uses Kirigami controls
for consistency.

Key QML components include `Main.qml`, `TimelineView`, `SnapshotSelector`,
`DiffView`, and the watchpoints pages (`WatchRulesPage`, `WatchSignalsPage`).

### Tray (`khronicle-tray`)

A minimal QSystemTrayIcon-based tool that periodically asks for “today’s summary”
and displays it as a tooltip or popup. It is intentionally lightweight and uses
local JSON-RPC to avoid direct DB access.

### Reporting (`khronicle-report`)

The CLI connects directly to SQLite (no daemon required), generates Markdown/JSON
reports, exports bundles, and aggregates multi-host bundles into a single file
for fleet review.

## Data Flow

System changes → pacman log / journal
→ parsers → events
→ SQLite store (with provenance & metadata)
→ snapshots capture point-in-time system state
→ diffs compare snapshots
→ UI/API/CLI consume events, snapshots, diffs, and summaries

## Domain Model

- `KhronicleEvent`:
  A single system change, including category, source, and before/after state.
- `SystemSnapshot`:
  A point-in-time view of kernel/driver/firmware/package state.
- `KhronicleDiff`:
  Computed differences between two snapshots.
- `HostIdentity`:
  Stable identity for a host (used by fleet aggregation).
- `WatchRule`:
  Declarative, local rule for matching events/snapshots.
- `WatchSignal`:
  A persisted signal when a watch rule matches.

## API Surface (JSON-RPC)

The daemon exposes a local JSON-RPC API via a UNIX domain socket. Typical
methods include:

- Data retrieval: `get_changes_since`, `get_changes_between`, `summary_since`,
  `list_snapshots`, `diff_snapshots`, `get_snapshot`
- Rules & signals: `list_watch_rules`, `upsert_watch_rule`,
  `delete_watch_rule`, `get_watch_signals_since`
- Interpretive: `explain_change_between`, `what_changed_since_last_good`

All APIs are local-only and intended for on-host tools.

## Extensibility

- New parsers: add a parser module under `src/daemon/` and wire it into the
  ingestion cycle in `KhronicleDaemon`.
- New event categories: extend `EventCategory` and map to serialization helpers
  in `src/common/json_utils.hpp`.
- Rules & signals: extend `WatchRule.extra` for new criteria without breaking
  existing JSON.
- UI: add a QML component and register any needed backend API client helpers.
