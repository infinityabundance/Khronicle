# Khronicle

Khronicle is a local, auditable system change chronicle for KDE Plasma on
CachyOS/Arch-based systems. It records kernel, driver, firmware, and key package
changes over time, then surfaces those changes through a timeline, snapshots,
explanations, and watchpoints.

The project is built for explainability and trust. It does not enforce system
policy or take automated actions. Instead, it captures what changed, when it
changed, and how changes relate across time so that humans can reason about
stability, regressions, and risk.

## Core Capabilities

- Timeline of system changes (kernel, GPU, firmware, packages, system events)
- Snapshots and diffs for point-in-time comparisons
- Risk tagging and watchpoints (rule-based attention signals)
- Temporal reasoning (explain change between two points)
- Multi-host aggregation and Fleet mode (offline, file-based)
- Reporting and export bundles (Markdown/JSON)

## Components

- `khronicle-daemon` — background ingestion, snapshotting, rules evaluation, and
  local JSON-RPC API.
- `khronicle` — the main GUI (Qt6/Kirigami) for timeline, diffs, explanations,
  and watchpoints.
- `khronicle-tray` — lightweight tray mini-viewer for “what changed today?”.
- `khronicle-report` — CLI reporting, bundle export, and aggregation tools.

## Installation

Khronicle targets CachyOS / Arch-based systems with Qt6 and KDE Plasma 6.
A sample PKGBUILD lives in `packaging/PKGBUILD`.

To build manually:

```bash
cmake -S . -B build
cmake --build build
```

To install via the provided package:

```bash
cd packaging
makepkg -si
```

## Running Khronicle

Start the daemon (systemd user service):

```bash
systemctl --user daemon-reload
systemctl --user enable --now khronicle-daemon
```

Start the GUI:

```bash
khronicle
```

Optional tray mini-viewer:

```bash
systemctl --user enable --now khronicle-tray
```

Run the report tool:

```bash
khronicle-report timeline --from "2026-01-28T00:00:00Z" --to "2026-01-29T00:00:00Z" --format markdown
```

## Key Workflows (Examples)

- See what changed in the last week.
- Compare system state today vs. last month.
- Flag unexpected kernel or firmware changes via watchpoints.
- Aggregate multiple hosts and review them in Fleet mode.

## Architecture & Design

For a deeper architecture walk-through:

- `ARCHITECTURE.md` — software architecture and data flow
- `SYSTEMS-ARCHITECTURE.md` — OS integration and service model
- `CODE.md` — top-down codebase navigation guide
- `GOVERNANCE.md` — stewardship and decision-making principles
- `STABILITY.md` — compatibility expectations and versioning
- `EVOLUTION.md` — how Khronicle grows without losing integrity
- `INVARIANTS.md` — non-negotiable system properties
- `TRUTH-MODEL.md` — what Khronicle claims vs does not claim
- `CONTRIBUTING.md` — contributor contract and review checklist
- `FUTURE.md` — non-binding ideas and open questions

## Trust & Scope

Khronicle is a local recorder and explainer. It does not:

- Enforce policy or block updates
- Automatically remediate changes
- Make causal claims about root cause
- Send data over the network by default

Its accuracy depends on the logs and snapshots it reads.

## What Khronicle Is Not

- Not a monitoring agent
- Not a security product
- Not a policy engine
- Not a predictive or AI-driven system
- Not an enforcement tool

## How to Read This Project

Khronicle’s documentation describes design intent, architectural goals, and
interpretive aims. These descriptions are not promises and not assurances of
correctness, completeness, reliability, or suitability.

In practice, some system changes may not be observed. Recorded data may be
partial, delayed, or misleading. Interpretations and explanations may be wrong.
Rules and signals may fail to trigger or may trigger unnecessarily. This is
expected.

Khronicle is best understood as an exploratory and interpretive aid for
examining system history, not as an authority or source of truth. Users should
assume that any expectation described in this documentation may fail to be met,
and should not rely on Khronicle as a sole basis for decisions, judgments, or
actions.

## Contributing & Development

Run tests with:

```bash
ctest --test-dir build
```

See `CODE.md` for a guided tour of the codebase and key entry points.

## License

See `LICENSE`.
