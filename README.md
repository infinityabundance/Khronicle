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
