# Truth Model

This document describes what Khronicle claims, and what it explicitly does not
claim. It prevents epistemic overreach.

## Claims Khronicle Makes

Khronicle makes observational claims tied to recorded inputs:

- “This event was observed in this log at this time.”
- “This system state snapshot reflects these values.”
- “These changes occurred between these two times.”

These are observational, not explanatory. They are rooted in inputs and stored
facts.

## Claims Khronicle Does NOT Make

Khronicle does not claim:

- Causation or intent
- Correctness of system configuration
- Security guarantees
- Absence of tampering

Logs and snapshots can be incomplete or misleading; Khronicle does not assert
otherwise.

## Role of Provenance & Audit

Provenance increases inspectability, not truthfulness. Audit logs explain how
conclusions were reached, not that they are correct. Interpretations remain
reversible and must be traceable to their inputs.

