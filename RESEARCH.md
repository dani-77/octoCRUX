# OctoCRUX — Research Brief

## Mission

Audit Octopi, OctoXBPS, OctoPkg, YAPO and CRUX's native package/ports tools. Produce a reproducible, source-cited recommendation for the most maintainable starting point. **Do not implement, refactor, install, or modify any upstream project during this phase.**

## Sources

- https://github.com/aarnt/octopi
- https://github.com/aarnt/octopkg
- https://github.com/fjaell/yapo
- OctoXBPS: identify and verify its upstream URL.
- Official CRUX documentation and man pages for `prt-get`, `pkgmk`, `pkgadd`, `pkgrm`, `ports` and related tools, as applicable.

Record upstream URL, commit SHA, branch/tag, review date and license for every inspected source. Distinguish documentation claims from source-code findings and actual tests.

## Questions to answer

1. What is the current build system, Qt version and dependency set of each Octo-family project?
2. Which components implement UI, search, installed package listing, available package listing, metadata, actions, progress, error handling, privilege escalation and terminal embedding?
3. Where do package-manager-specific commands or parsers leak into UI code? Cite file paths, symbols and line ranges.
4. Which components can be reused with minimal changes? What must be replaced?
5. Does any candidate already expose a reusable backend boundary? Show evidence.
6. How does YAPO use `prt-get`, and which CRUX edge cases does it address? Identify behaviors, not merely command names.
7. Which native CRUX commands support each desired operation? Verify syntax and output format from the target version, including errors, localization and ambiguous names.
8. How are ports synchronized, how are dependencies resolved, and what are the implications of locked packages, optional dependencies and repositories/overlays?
9. What privileges are required for each operation? How can they be obtained safely without assuming a particular desktop environment?
10. What license obligations and attribution requirements follow from each possible fork or code reuse?
11. What is the current maintenance state and risk profile of each candidate?
12. Which design allows readable real-time build output and reliable cancellation/cleanup?

## Required comparison

For each candidate document: UI maturity; Qt/build requirements; backend separation; subprocess model; command-output parsing; privilege model; terminal integration; test coverage; license; maintenance activity; adaptation effort; blockers.

## Deliverables

- `RESEARCH_FINDINGS.md`: evidence, source references and verified commands.
- `BASE_SELECTION.md`: at least two viable approaches, explicit tradeoffs and a recommendation; include a clean-room Qt frontend as an alternative if adaptation is costly.
- `OPEN_QUESTIONS.md`: unresolved assumptions and reproducible tests needed on CRUX.

## Exit criteria

No unsupported architectural assertions; no assumed command flags; license compatibility reviewed; proposed base justified by evidence; owner approves the next phase. A green test is valid only if it exercises the behavior it claims to verify.
