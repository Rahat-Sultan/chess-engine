# Architecture Decisions and Open Questions

This document tracks questions that must be confirmed with the project
lead before making irreversible design choices, and records decisions
that have already been made.

---

## Phase 0 — Open questions (confirm before Phase 1)

| # | Question | Default assumption | Status |
|---|----------|--------------------|--------|
| 1 | Build an original engine, or wrap Stockfish? | Build original | ⬜ Confirm |
| 2 | Are pretrained Stockfish NNUE weights permitted? | No, use original evaluator first | ⬜ Confirm |
| 3 | Is C++ the required language? | Yes, C++17 | ⬜ Confirm |
| 4 | Which OS targets must be supported? | Ubuntu/Linux only initially | ⬜ Confirm |
| 5 | Minimum CPU / RAM / storage requirements? | Unknown — benchmark first | ⬜ Confirm |
| 6 | Target move-response time? | Unknown — benchmark first | ⬜ Confirm |
| 7 | First deliverable: UCI executable only, or UCI + HTTP? | UCI executable first | ⬜ Confirm |
| 8 | Engine deployment: local process beside backend, or separate network service? | Local process initially | ⬜ Confirm |
| 9 | External opening books or endgame tablebases permitted? | No, not for initial milestone | ⬜ Confirm |
| 10 | Licensing and attribution requirements for any reused code or weights? | Review before adding any dependency | ⬜ Confirm |

---

## Resolved decisions

| # | Decision | Rationale | Date |
|---|----------|-----------|------|
| D-1 | Use CMake + Ninja as build system | Cross-platform, reproducible, standard for C++ | Phase 0 |
| D-2 | C++17 standard | Widespread compiler support; structured bindings and `std::optional` useful for move/search types | Phase 0 |
| D-3 | Engine stdout is UCI-only; all diagnostics go to stderr | Required by the UCI specification to avoid corrupting the protocol stream | Phase 0 |
| D-4 | No third-party engine code or network weights committed to the repo without explicit lead approval | Licensing safety; follows project spec default assumption | Phase 0 |
| D-5 | Start with a 64-square array board representation | Clarity and correctness first; bitboard optimization can be added later behind the Board interface if performance requires it | Phase 0 (planned for Phase 1) |
| D-6 | Evaluation function hidden behind an abstract interface (`Evaluator`) | Allows the handcrafted evaluator to be swapped for NNUE without touching the search code | Phase 0 (planned for Phase 3) |

---

## Notes

- This file should be updated at the start of every phase.
- Never make an irreversible architecture change (new external dependency,
  change of language, change of protocol contract) without first recording
  it here and getting confirmation.
- See `docs/PROJECT_SPEC.md` §2 for the full original list of open questions.
