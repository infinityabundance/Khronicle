# Invariants

This document describes intended invariants and meaning-preservation anchors;
it does not promise behavior, correctness, or long-term adherence.

The invariants described here are conceptual anchors, not enforced promises.
They exist to preserve meaning and intent over time, but may be violated by
bugs, incomplete implementations, or future changes. Their purpose is to make
such violations visible and discussable, not impossible.

This document defines what Khronicle is. These invariants are stronger than
preferences or style guides. Violating them means the system is no longer
Khronicle.

## Invariant 1 — Facts Precede Interpretation

Raw events and snapshots are facts derived from logs and system state. Risk
levels, watchpoints, explanations, and summaries are interpretations.
Interpretations must always be derivable from stored facts and must never
overwrite, obscure, or delete raw data.

## Invariant 2 — No Silent Inference

Khronicle must never infer intent, assign blame, or assert causation.
Interpretive output must be labeled, explainable, and traceable to inputs.

## Invariant 3 — Append-Only Historical Record

Events, snapshots, signals, and audit records are append-only. Deletion or
mutation of historical records is exceptional, explicit, and auditable.
Khronicle prefers accumulation over correction.

## Invariant 4 — Local First, Explicit Federation

Khronicle is local by default. Multi-host views are file-based, explicit, and
pull-only. There is no background synchronization or hidden networking.

## Invariant 5 — Declarative Rules Only

Watchpoints and rules are declarative, inspectable, and non-Turing-complete.
No embedded scripting engines or opaque evaluation logic.

## Invariant 6 — Meaningful Time

Time is central: ordering and temporal context matter. Khronicle does not
collapse history into scores or aggregates without preserving chronology.
