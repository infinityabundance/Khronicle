# Governance

This document describes intended stewardship and decision-making; it does not
promise behavior, correctness, or long-term adherence.

## Project Scope & Intent

Khronicle is a system chronicle. It records, explains, and surfaces signals
about system changes. It is not a security product, a monitoring agent, or a
policy engine. Its role is to observe and describe, never to enforce.

These boundaries are non-negotiable:

- Record changes with provenance and auditability
- Explain change with explicit separation between facts and interpretation
- Surface local signals for attention, not action
- Never block, remediate, or enforce policy

## Decision-Making Philosophy

Khronicle favors coherence over velocity. Design integrity matters more than
feature count, and breaking changes are costly and rare.

Principles:

- Preserve auditability, explainability, and determinism
- Prefer clarity and correctness over performance or new features
- Avoid “clever” solutions unless the benefit is explicit and documented
- Keep behavior transparent and inspectable

Preference order:

1. Clarity
2. Correctness
3. Stability
4. Performance
5. Features

## Accepting Contributions

All contributions must:

- Preserve existing invariants and data compatibility
- Avoid implicit behavior and hidden side effects
- Include documentation updates where relevant
- Keep experimental features opt-in and explicitly labeled

Changes that introduce network automation, enforcement, or ML inference require
explicit justification and careful review because they risk violating the
project’s boundaries.

See `CONTRIBUTING.md` for the contributor contract and review checklist.

## Maintainer Responsibilities

Maintainers are responsible for protecting:

- Schema stability and data longevity
- API compatibility and additive evolution
- Clear migration paths when change is unavoidable
- The separation between facts, interpretation, and signals

The role is to preserve trust and continuity over time, not to maximize
short-term velocity.
