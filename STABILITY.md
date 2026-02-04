# Stability

## Stability Tiers

### SQLite Schema

- Backward-compatible additions only.
- No destructive migrations without explicit tooling.
- Existing tables/columns will not be repurposed.

### JSON-RPC API

- Additive changes only.
- Fields are never repurposed.
- Existing methods maintain their semantics.

### Export Bundles

- Bundles must remain readable by newer versions.
- Metadata fields may be added but not redefined.

### Watch Rules

- Rule semantics must not silently change.
- Any change to evaluation behavior is treated as a breaking change.

## Versioning Policy

Khronicle follows semantic versioning with explicit meaning:

- MAJOR: schema or API breaks, or any compatibility break
- MINOR: new capabilities that preserve compatibility
- PATCH: fixes only

Not every feature addition justifies a minor bump; changes should be grouped
when possible to keep version churn low.

## Deprecation Policy

Deprecated features must:

- Be documented explicitly
- Emit warnings where appropriate
- Remain supported for at least one minor release

There are no silent removals.

See `EVOLUTION.md` for how future changes should be structured.

