# Contributing

Thank you for contributing to Khronicle. This project optimizes for long-term
understanding and trust. Changes should preserve clarity and compatibility.

## Contribution Expectations

- Code must be readable and comment intent, not just mechanics
- Documentation updates are required when behavior or architecture changes
- Tests are expected for:
  - logic and parsing
  - rules and signals
  - interpretation layers

## Review Checklist (Self-Review)

Before submitting, confirm:

- Does this change introduce implicit behavior?
- Does it blur the line between fact and inference?
- Does it degrade explainability or auditability?
- Does it break existing data or schema expectations?
- Are docs updated where users or contributors would notice?

## Epistemic Review Checklist

Before submitting, also confirm:

- Does this blur the line between fact and interpretation?
- Does this introduce implicit inference?
- Does this reduce inspectability or auditability?
- Does this weaken or bypass an invariant?

## Style & Philosophy

Khronicle optimizes for long-term understanding, not short-term novelty.
If a change is clever but not clear, favor the clear version.

See `GOVERNANCE.md` for decision-making principles.

## Expectations and Interpretation

Contributors should avoid language that implies certainty, authority, or
reliability. Documentation and UI text should describe what the system attempts
to do, not what it promises. When in doubt, prefer wording that acknowledges
uncertainty, partial observability, and failure modes. Avoid enforcement or
blame language.
