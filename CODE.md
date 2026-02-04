# Code Guide

This document describes intended structure and navigation; it does not
promise behavior, correctness, or long-term adherence.

## Repository Layout

- `src/`
  - `common/` — shared models and JSON helpers
  - `daemon/` — khronicle-daemon core
  - `ui/` — Qt6/Kirigami GUI
  - `tray/` — tray mini-viewer
  - `report/` — CLI reporting and bundles
- `tests/`
- `packaging/`
- `resources/`

## Entry Points

- `src/daemon/main.cpp` — daemon entry. Creates `KhronicleDaemon` and starts the
  Qt event loop.
- `src/ui/main.cpp` — GUI entry. Initializes the QML engine and connects the
  API client.
- `src/tray/main.cpp` — tray entry. Owns `KhronicleTray` and the Qt event loop.
- `src/report/main.cpp` — CLI entry. Dispatches to `ReportCli`.

## Core Classes (Top-Down)

### Daemon Orchestration

`KhronicleDaemon`

- Owns `KhronicleStore`, parsers, watch engine, and API server.
- Runs a timer-driven ingestion cycle.
- Persists cursor/timestamp state via the `meta` table.

### Storage

`KhronicleStore`

- SQLite access layer for events, snapshots, meta, host identity, rules, signals.
- Provides typed methods for all reads/writes used by daemon and CLI.

### Ingestion

- `pacman_parser.cpp` parses `/var/log/pacman.log` into events.
- `journal_parser.cpp` queries the system journal for relevant events.
- `snapshot_builder.cpp` captures point-in-time system state.

### Interpretation Layers

- Risk tagging is stored in event/snapshot JSON state and used for summaries.
- `WatchEngine` evaluates `WatchRule` entries and persists `WatchSignal` hits.
- `change_explainer.cpp` and `counterfactual.cpp` implement temporal reasoning
  and explanation helpers.

### API

`KhronicleApiServer`

- Receives JSON-RPC over a local UNIX socket.
- Dispatches to store and explanation helpers.
- Serializes responses with `json_utils.hpp`.

### UI Backend

- `KhronicleApiClient` bridges QML to the daemon API.
- `WatchClient` manages rule and signal calls from the UI.
- `FleetModel` loads aggregate JSON for fleet mode.

### QML UI

- `Main.qml` is the GUI entry view.
- `TimelineView`, `SnapshotSelector`, `DiffView` render core data.
- `WatchpointsPage`, `WatchRulesPage`, `WatchSignalsPage` manage watchpoints.
- `FleetMain.qml` is the fleet view.

### Other Tools

- `KhronicleTray` queries today’s summary and displays it in the tray.
- `ReportCli` renders reports and bundle/aggregate outputs.

## How Things Fit Together (Narrative)

On a running system, the daemon wakes up on a timer, reads new pacman and
journal entries, converts them into events, and writes them to SQLite. It also
creates snapshots when needed, evaluates watch rules, and updates meta state.

When the GUI starts, it connects to the daemon’s UNIX socket, asks for recent
changes and summaries, and renders them in the timeline. The tray app makes a
small JSON-RPC request for “today’s summary” and exposes it in a tooltip or
popup. The CLI reads directly from SQLite to generate reports and bundles.

## Coding Conventions & Style Notes

- C++20 is the baseline.
- JSON serialization uses `nlohmann::json`.
- Qt types are used for UI and IPC; standard library types are used for core
  models where possible.
- Exceptions are used in the SQLite store for hard failures; the API server
  catches and returns errors as JSON.
- New event types should be added to `EventCategory` and reflected in
  `json_utils.hpp` mappings.
