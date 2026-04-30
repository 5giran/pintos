# General PR Review Checklist

Use this checklist for all repositories. In Pintos mode, apply this first and then layer on `pintos-checklist.md`.

## Core Review Dimensions

- Correctness: logic errors, off-by-one mistakes, null/undefined handling, boundary conditions, invalid assumptions.
- Regression risk: changed existing behavior, breaking changes, semantic compatibility, unexpected side effects.
- Error handling: missing try/catch or cleanup, swallowed errors, partial failure behavior, retry/idempotency issues.
- Concurrency: race conditions, lock ordering, transaction boundaries, shared mutable state, async cancellation.
- Security: input validation, injection, authorization, sensitive data exposure, unsafe logging.
- Data integrity: migration safety, rollback path, data loss risk, consistency between models and schema.
- Performance: N+1 work, algorithmic complexity, avoidable allocation, leaks, hot-path regressions.
- Test quality: coverage for new behavior, edge cases, error paths, precise assertions, appropriate mocks.
- API contract: public interface changes, versioning, documentation, backward compatibility.
- Observability: useful logs, metrics, traces, auditability for new failure modes.

## Diff-Adjacent Checks

- Search other callers of changed functions/classes with `rg`.
- For interface changes, verify all required call sites and docs moved together.
- For new dependencies, check manifest and lockfile consistency.
- For migrations, verify model/schema/test fixtures match the migration.
- For config changes, check environment-specific behavior and safe defaults.
- For tests-only PRs, review whether assertions genuinely catch the intended behavior and avoid overfitting.

## Review Judgment

Prioritize user-visible bugs, correctness, safety, and missing verification. Mention style only when it creates real maintainability or defect risk. If evidence is incomplete, mark confidence as `low` and explain what should be checked.
