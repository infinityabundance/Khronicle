# Evolution

This document describes intended growth and design direction; it does not
promise behavior, correctness, or long-term adherence.

This document explains how Khronicle should grow without losing its integrity.
It is about design direction, not a roadmap.

## Design Axes

Khronicle evolves along four primary axes:

- Time: richer temporal reasoning and comparisons
- Trust: auditability, provenance, and deterministic behavior
- Scope: single host to offline multi-host fleet review
- Interpretation: facts → signals → explanations (no enforcement)

Growth should remain orthogonal along these axes, avoiding entanglement that
blurs responsibilities.

## Acceptable Future Extensions

Examples of compatible extensions (non-binding):

- New parsers (additional package managers or logs)
- New snapshot dimensions (e.g., extra hardware state)
- Improved visualizations and navigation
- More expressive watch rules while remaining declarative

## Explicit Non-Goals

These are guardrails, not preferences:

- No autonomous remediation
- No remote command execution
- No opaque scoring or black-box inference
- No hidden policy enforcement

See `STABILITY.md` for compatibility expectations.
