# Systems Architecture

This document describes intended integration and operating context; it does not
promise behavior, correctness, or long-term adherence.

## Host-Level Overview

CachyOS / Arch
→ systemd user services
→ `khronicle-daemon`
→ SQLite DB under `~/.local/share/khronicle/`
→ GUI (`khronicle`) and tray (`khronicle-tray`) as user apps
→ CLI tools (`khronicle-report`)

## Processes & Services

- `khronicle-daemon` runs as a user-level systemd service. It performs ingestion
  and serves a local JSON-RPC API over a UNIX domain socket.
- `khronicle-tray` is an optional user-level service for a lightweight summary
  view. It can be started/stopped independently of the main GUI.
- `khronicle` (GUI) is a normal user app that connects to the daemon.

Typical commands:

```bash
systemctl --user enable --now khronicle-daemon
systemctl --user enable --now khronicle-tray
systemctl --user status khronicle-daemon
```

## File System Layout

- Database: `~/.local/share/khronicle/khronicle.db`
- Bundles (exported by `khronicle-report`): user-chosen output paths
- Aggregates: user-chosen output paths

Khronicle does not currently store configuration outside the database.

## Interactions with System Logs

- pacman log: `/var/log/pacman.log`
- system journal: accessed via `journalctl`

The daemon reads logs but never modifies them. Ingestion is read-only.

## Security & Trust Posture

- Runs as an unprivileged user process (no root requirement).
- Uses a local UNIX domain socket for API access.
- No outbound network connections by default.
- Accuracy is limited by the completeness of system logs and snapshots.

## Fleet Mode (Offline, File-Based)

1. Export bundles on each host with `khronicle-report bundle`.
2. Transfer bundles to a review machine (scp/rsync/USB).
3. Aggregate bundles with `khronicle-report aggregate`.
4. Launch `khronicle --fleet aggregate.json` for read-only review.
