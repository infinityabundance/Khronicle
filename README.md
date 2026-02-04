# Khronicle
System and driver change timeline for KDE Plasma on CachyOS/ArchLinux

## Running Khronicle

### Starting the Khronicle daemon

Enable and start the user service:

```bash
systemctl --user daemon-reload
systemctl --user enable --now khronicle-daemon
```

(Optional) Enable tray autostart:

```bash
systemctl --user enable --now khronicle-tray
```

Check status:

```bash
systemctl --user status khronicle-daemon
```

## Packaging (Arch / CachyOS)

A sample PKGBUILD is provided under `packaging/PKGBUILD`.

To build and install:

```bash
cd packaging
makepkg -si
```

This will install:

`khronicle` (GUI)
`khronicle-daemon` (background service)
`khronicle-tray` (optional tray mini-viewer)

.desktop launchers under `/usr/share/applications/`
systemd user units under `/usr/share/systemd/user/`

After installation, enable the daemon:

```bash
systemctl --user daemon-reload
systemctl --user enable --now khronicle-daemon.service
```

(Optional) enable the tray:

```bash
systemctl --user enable --now khronicle-tray.service
```

## Command-line reports

Khronicle includes a small CLI tool `khronicle-report` for exporting reports.

Examples:

```bash
# Timeline for last 24h in Markdown
khronicle-report timeline --from "2026-01-28T00:00:00Z" --to "2026-01-29T00:00:00Z" --format markdown > report.md

# Snapshot diff as JSON
khronicle-report diff --snapshot-a "snapshot-123" --snapshot-b "snapshot-456" --format json > diff.json

# Reproducible bundle export
khronicle-report bundle --from "2026-01-28T00:00:00Z" --to "2026-01-29T00:00:00Z" --out bundle.tar.gz
```

## Stability & Guarantees

Khronicle is designed to be safe and predictable:

What Khronicle guarantees:
Append-only event history (no destructive auto-cleanup)
Snapshot diff correctness for stored snapshots

What Khronicle does not guarantee:
Perfect reconstruction of every system change
Root-level auditing or tamper-proof logs

Debugging tips:
Check daemon logs for warnings
Use `khronicle-report` for exports
Query `get_daemon_status` via the local JSON-RPC socket for diagnostics

## Risk & Anomaly Tags

Khronicle applies transparent, rule-based risk tagging to events:

- `info`: normal updates and routine changes
- `important`: GPU driver or firmware updates, package downgrades
- `critical`: kernel version changes

Risk metadata is included in JSON exports and surfaced in the timeline UI.

## Auditability & Trust Model

Khronicle distinguishes between facts and interpretations:

- Facts: raw events and snapshots captured from pacman logs, journalctl, and system probes.
- Interpretations: risk tags and snapshot diffs derived from those facts.

Every event carries provenance (source, parser version, ingestion cycle). Derived
conclusions are logged in an audit trail to make reasoning reproducible.

Khronicle does not provide tamper-proofing or a root of trust; it is designed for
transparent, explainable diagnostics rather than forensic guarantees.
