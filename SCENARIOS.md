# Scenarios

A Scenario is a small captured slice of state and steps that reproduces a
specific issue. It is designed to be deterministic, fast, and shareable.

## How to Capture

1. Enable capture:

```bash
export KHRONICLE_SCENARIO_CAPTURE=1
export KHRONICLE_SCENARIO_ID="example-bug-1"
export KHRONICLE_SCENARIO_TITLE="Describe the issue"
export KHRONICLE_SCENARIO_DESC="Short description of what goes wrong"
```

2. Reproduce the issue using the daemon/UI/CLI.
3. Locate the scenario directory under:

`~/.local/share/khronicle/scenarios/<id>`

## How to Replay

Copy the scenario into the repo `scenarios/` directory and run:

```bash
khronicle-replay scenarios/example-bug-1
```

## Using Scenarios with an LLM

Provide:

- The scenario directory
- The relevant code context
- The replay log (especially `*-codex.log` if enabled)

Scenarios are best-effort approximations. They make issues discussable and
reproducible, but they are not perfect time machines.
