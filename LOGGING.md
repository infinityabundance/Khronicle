# Logging

Khronicle uses structured, introspective logs designed for human and LLM-assisted
debugging. Logs describe what happened, why it happened, when it happened, how
it flowed through the system, where in the code it occurred, and who/what
component was responsible. Logs are best-effort observations, not authoritative
truth.

## Purpose

- Make debugging easier by exposing key lifecycle and decision points.
- Provide LLM-friendly context for “code + logs” analysis.
- Preserve a clear separation between facts and interpretations.

## Log Locations

Logs are written under:

`~/.local/share/khronicle/logs/`

Each binary writes its own file:

- `khronicle-daemon.log`
- `khronicle.log`
- `khronicle-tray.log`
- `khronicle-report.log`

Codex trace logs use `-codex.log` suffix.

## Log Format

Each log entry is a single JSON line:

```json
{
  "ts": "2026-02-04T12:34:56.789Z",
  "level": "INFO",
  "process": "khronicle-daemon",
  "thread": "0x7f...",
  "component": "KhronicleDaemon",
  "where": "runIngestionCycle",
  "what": "start_ingestion_cycle",
  "why": "timer_tick",
  "how": "bounded_batch",
  "who": "host:my-host,uid:1000",
  "corr": "ingestion-42",
  "context": {
    "cycleIndex": 42,
    "journalSince": "2026-02-04T12:30:00Z"
  }
}
```

Field meanings:

- `ts`: UTC timestamp
- `level`: DEBUG/INFO/WARN/ERROR
- `process`: binary name
- `thread`: thread id
- `component`: class or module
- `where`: function or scope
- `what`: action performed
- `why`: reason for action
- `how`: strategy or method
- `who`: host/user identity
- `corr`: correlation id for end-to-end tracing
- `context`: structured, non-sensitive details

## Codex Trace Mode

Enable verbose logging for LLM-assisted debugging:

- `--codex-trace` CLI flag (per binary), or
- `KHRONICLE_CODEX_TRACE=1` environment variable

When enabled, all log levels are written to `*-codex.log`. This mode can be
noisy and may include more detailed context. Use it when you intend to pair a
log with a code snapshot for diagnosis.

## Caveats

- Logs are best-effort and may be incomplete or misleading.
- Avoid sharing logs if they contain sensitive system details.
- Large log volumes are possible in Codex trace mode.

## Sharing Logs with an LLM

When requesting LLM assistance:

1. Include the relevant code context.
2. Provide the log segment around the failure.
3. Mention the correlation id (`corr`) if present.
4. Describe what you expected vs. what occurred.

