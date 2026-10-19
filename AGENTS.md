# OctoCRUX — Agent Working Agreement

## Collaboration

- Discuss with the project owner in European Portuguese. Write technical documentation, source comments and commit messages in English unless directed otherwise.
- Treat the owner's observed behavior as evidence to investigate, not an assertion to accept blindly or dismiss because the source looks correct.
- Disagree when warranted, with reproducible evidence. Clearly label assumptions, hypotheses and verified facts.
- Favor clean, maintainable, modular code and thorough documentation. Do not equate minimal line count with simplicity.
- Do not hardcode personal preferences, particular terminals, desktop environments, machines or distributions where a portable/configurable solution is feasible.
- The owner can test on different systems and bare metal. Ask for the relevant environment only when necessary; do not assume one machine represents all deployments.

## Scope control

- **Research phase: READ ONLY.** No code edits, refactoring, dependency installation, privileged commands or repository rewrites without explicit approval.
- Do not infer permission for implementation from a request for a plan or architecture.
- Before coding, identify files to change, intended behavior, risk and validation plan.
- Prefer small, reviewable patches. Avoid unrelated cleanup or opportunistic rewrites.
- Never claim tests passed unless executed; report exact commands, environment and results.
- Do not conceal warnings, errors, incomplete testing or unknowns.
- Maintain the relevant documentation alongside approved behavior changes.

## SCOFS protocol

**SCOFS = Special Case Oh Foda-Se.** Trigger when a test is green but fails to validate the claimed real-world behavior, or when observed behavior contradicts apparent test success.

When triggered: stop declaring success; identify the actual behavior and missing coverage; produce a minimal reproducer; add a test that exercises the right mechanism; validate the fix in the relevant environment; record the lesson in documentation.

## Handoffs

For every completed phase provide: findings; changed files (if authorized); commands actually executed; test evidence; known limitations; open questions; next suggested step. Do not proceed into the next phase without owner approval.

## Relationship to global instructions

This file governs OctoCRUX-specific work. The owner's forthcoming global instructions should be reviewed and incorporated without duplicating or contradicting them. Keep conversational style distinct from engineering standards.
