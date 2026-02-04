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
```

## Temporal Reasoning & Interpretation

Khronicle offers temporal queries and explanations to help interpret change:

- Recorded facts: events and snapshots at specific times
- Derived diffs: comparisons between snapshots or ranges
- Interpretive explanations: text summaries of likely contributors

Explanations describe temporal correlation and plausibility, not causation.

## Multi-Host / Fleet Usage

Khronicle supports offline, read-only aggregation across multiple hosts.

Generate bundles on each machine:

```bash
khronicle-report bundle --from "2026-01-28T00:00:00Z" --to "2026-01-29T00:00:00Z" --out host-bundle.tar.gz
```

Aggregate bundles on a review machine:

```bash
khronicle-report aggregate --input /path/to/bundles --format json --out aggregate.json
```

Open Fleet Mode:

```bash
khronicle --fleet aggregate.json
```

Fleet Mode is offline and read-only; no network sync or central server is required.
