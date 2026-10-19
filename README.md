# OctoCRUX

> Planning stage — no implementation has been approved.

OctoCRUX is a proposed Qt graphical frontend for the native CRUX Linux package and ports tooling. It aims to preserve the familiar Octopi-style experience while respecting CRUX's source-based workflow and minimalist philosophy.

## Goals

- Browse installed packages and available ports separately.
- Search by name and description; inspect version, origin, dependencies, Pkgfile, README and footprint when available.
- Plan and execute install, remove and update operations using native CRUX tools.
- Show the complete build and command output, including errors, without concealing underlying operations.
- Keep privilege escalation explicit and limited to operations requiring it.
- Avoid hardcoded terminal, shell, privilege helper, distribution-specific paths or user preferences where discovery or configuration is appropriate.
- Keep the frontend and package operations independently testable.

## Non-goals for the first milestone

- Replacing `pkgutils`, `pkgmk`, `prt-get` or the ports synchronization mechanisms.
- Implementing a new dependency solver.
- Rewriting Octopi, OctoPkg or YAPO wholesale.
- Making system changes during research or the initial read-only prototype.

## Candidate references

- Octopi: https://github.com/aarnt/octopi
- OctoPkg: https://github.com/aarnt/octopkg
- OctoXBPS: locate and verify the authoritative repository during research.
- YAPO: https://github.com/fjaell/yapo

These are references, not a decision to fork or copy code. Compare source, build dependencies, architecture, maintenance and license compatibility before selecting a base.

## Development stages

1. Evidence-based source audit (read-only).
2. Architecture proposal and owner approval.
3. Minimal read-only Qt proof of concept.
4. Native operations and privileged execution, with explicit approval and tests.
5. Packaging, documentation and broader validation on real CRUX installations.

## Status

No codebase, toolkit integration strategy, command mappings or licensing decision is final yet. See `RESEARCH.md`, `ARCHITECTURE.md` and `AGENTS.md`.
