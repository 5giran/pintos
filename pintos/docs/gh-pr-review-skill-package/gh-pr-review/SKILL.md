---
name: gh-pr-review
description: 명시 호출 시 현재 브랜치 GitHub PR을 로컬 상태 점검, pull 여부 확인, 컨텍스트 수집, diff 리뷰, unresolved review comments 대응 흐름으로 처리하는 리뷰 전용 스킬입니다. Pintos repo/파일 패턴을 감지하면 KAIST CS330 64-bit Pintos 특화 리뷰를 적용합니다.
---

# gh-pr-review

Use this skill only when the user explicitly invokes `$gh-pr-review` or directly asks to use the `gh-pr-review` skill. Default response language is Korean; keep code identifiers, commands, file paths, and error messages in their original English.

This is a review-only skill. Do not edit code, create commits, post PR comments, submit reviews, or resolve review threads unless the user explicitly asks for that write action. If the user says "고쳐줘" or otherwise asks for implementation, leave review mode and handle the code-change task separately.

## Required References

- Always read `references/review-workflow.md` before starting a PR review.
- Read `references/general-checklist.md` for normal repositories and as the base checklist for every review.
- If Pintos mode is detected by `references/review-workflow.md`, also read `references/pintos-checklist.md` and apply it in addition to the general checklist.
- For unresolved review comments, follow the routing and fallback rules in `references/review-workflow.md`; use the `gh-address-comments` flow when available.

## Operating Rules

1. Start by checking local git state. If the branch is dirty, detached, or ambiguous, pause and ask the user to choose from the documented options.
2. Ask before running `git pull --ff-only`. Never auto-merge or auto-rebase after a pull failure.
3. Identify the current branch's PR with `gh`. If there is no PR, report that and stop; never create a draft PR automatically.
4. Gather PR metadata, changed files, intent, linked issue context, and relevant base-branch code before reviewing the diff.
5. Detect Pintos mode and phase before producing findings. If active, state the mode in the output header, for example `(Pintos Phase 1: threads 모드)`.
6. Report review results findings-first. Prefer real bugs, regressions, memory safety, synchronization, API contract, data integrity, and missing tests over style preferences.
7. If there are no substantive issues, say `주요 이슈 없음` clearly and mention only remaining test or verification risk.

## Output Shape

Use Korean explanations with English code tokens. Findings should include priority, confidence, file and line anchor when possible, evidence, impact, and suggested fix. End with a short `다음 단계` section that states whether High findings should block merge and what tests are likely affected. When preparing text to post on GitHub, omit internal workflow guidance such as asking the user to say "고쳐줘" for code changes.
