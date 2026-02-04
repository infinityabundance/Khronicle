# Scenarios

Scenarios are small, explicit bundles that capture a reproducible slice of state
and steps for a specific issue. They are meant for deterministic replay and LLM-
assisted debugging.

Each scenario directory may include:

- `scenario.json` — metadata and steps to replay
- `db.sqlite` — minimal DB snapshot
- `pacman.log` — trimmed pacman log (optional)
- `journal.txt` — trimmed journal output (optional)
- `api_calls.json` — list of requests (optional)
- `notes.md` — human notes and observations

Use `khronicle-replay` to execute a scenario.
