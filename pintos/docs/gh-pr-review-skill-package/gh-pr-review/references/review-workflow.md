# Review Workflow

This workflow is mandatory for `$gh-pr-review`. It is intentionally conservative: inspect local state first, ask before pulling, gather context before judging the diff, and never perform GitHub write actions without explicit user request.

## 1. Local State Gate

Run:

```bash
git status --short --branch
git branch --show-current
git rev-parse --abbrev-ref --symbolic-full-name @{u}
```

Use this decision tree:

```text
clean + upstream exists
  -> ask whether to run git pull --ff-only
     -> if user says pull: run git pull --ff-only
        -> success: continue
        -> failure/divergence: ask user to choose rebase attempt, review current state, or stop
     -> if user says do not pull: continue

dirty staged/unstaged/untracked changes
  -> stop, summarize status, and ask user to choose stash then pull, review current state only, or stop

detached HEAD
  -> explain that the PR cannot be identified reliably and stop

no upstream
  -> explain that pull is skipped and continue with review if a PR can be identified
```

Only use `git pull --ff-only` in this skill. Do not auto-merge, auto-rebase, or discard local changes.

## 2. PR Identification

Use `gh pr view --json number,title,body,baseRefName,headRefName,url` on the current branch first.

- If exactly one PR is identified, continue.
- If no PR is found, tell the user and stop. Do not create a PR.
- If multiple PRs are possible, list open candidates with `gh pr list --head <branch> --json number,title,url,baseRefName,headRefName` and ask the user to choose.

## 3. Required Context Before Diff Review

Before writing findings:

1. Read PR metadata: title, body, base branch, head branch, URL, draft state when available.
2. Follow linked issue references from the PR body when practical, using `gh issue view` or linked URLs.
3. Inspect changed files with `gh pr diff <number> --name-only` and estimate impacted domains.
4. Detect Pintos mode and phase using the rules below.
5. For modified large functions/classes or public interfaces, inspect the base-branch version and nearby callers with `rg`.
6. Compare PR intent with actual changes. Treat unrelated behavior changes, hidden refactors, or broad churn as possible scope creep.

Default diff scope is the full PR diff against the base branch, usually `gh pr diff <number>`.

## 4. Pintos Mode Detection

Activate Pintos mode if any of these are true:

- Repository name or remote URL contains `pintos`.
- Changed paths include phase directories anywhere in the path, such as `threads/`, `userprog/`, `vm/`, `filesys/`, `pintos/threads/`, `pintos/userprog/`, `pintos/vm/`, `pintos/filesys/`, `pintos/include/threads/`, or `pintos/tests/threads/`.
- `Makefile.build`, Pintos utilities, or Pintos-specific build paths are present.
- README or docs mention `KAIST CS330`, `Pintos`, or `64-bit Pintos`.

Estimate phases from changed paths. Multiple phases may be active:

- `threads/`, `include/threads/`, `tests/threads/` -> Phase 1: threads
- `userprog/`, `include/userprog/`, `tests/userprog/` -> Phase 2: userprog
- `vm/`, `include/vm/`, `tests/vm/` -> Phase 3: vm
- `filesys/`, `include/filesys/`, `tests/filesys/` -> Phase 4: filesys

When active, include an output header such as:

```text
(Pintos Phase 1: threads 모드)
```

For multiple phases, list them all.

## 5. Large Diff Split Gate

Before reviewing, consider asking the user whether to split the review by file or domain if any trigger is met after excluding generated/non-production files:

- Hand-written production code changes are at least 1000 lines.
- Production file count is at least 15.
- The PR clearly spans multiple domains, such as backend + frontend + infra.

Exclude generated and low-signal files from the split calculation: `build/`, `dist/`, `out/`, `.next/`, `coverage/`, `node_modules/`, minified assets, generated files, lock files, vendored code, compiled artifacts, and Pintos build outputs.

## 6. Findings-First Format

Prefer Markdown findings because they are easy to share. Use inline `::code-comment{...}` only when there are 5 or fewer findings and every finding has an exact line anchor. If both are suitable, prefer Markdown.

Markdown finding format:

```markdown
### [High, confidence: high] path/to/file.c:123 - 한 줄 요약
근거: 어떤 코드/패턴 때문에 문제인지.
영향: 무엇이 잘못될 수 있는지. Pintos 모드라면 관련 grading test 영향도 포함.
제안: 어떻게 바꾸면 좋은지.
```

Confidence levels:

- `high`: diff and surrounding context strongly support the finding.
- `medium`: likely issue, but one assumption remains.
- `low`: plausible issue needing confirmation. Use cautious wording such as `확인 필요`.

Priority mapping:

| Priority | General mode | Pintos mode |
| --- | --- | --- |
| High | Bugs, regressions, security issues, data loss | Synchronization bugs, memory leaks, undefined behavior, likely grading test failure |
| Medium | Performance, missing tests, error handling | Inefficiency, partial race risk, duplicated fragile logic |
| Low | Readability, naming, maintainability | Style, comments, small clarity issues |

Avoid findings for pure taste, quote style, line wrapping, or unrelated refactor preferences.

End every review with:

```markdown
## 다음 단계
1. High 항목 N개는 머지 전 반드시 수정 권장
2. Pintos 모드라면 영향받을 수 있는 grading test 또는 make check 범위
```

When posting or drafting a GitHub PR comment, do not include internal Codex workflow guidance such as asking the user to say `"고쳐줘"` for a separate code-change task. Keep posted comments focused on review findings, merge risk, and verification.

If no issues are found, say `주요 이슈 없음` and keep the remaining risk section brief.

## 7. Unresolved Review Comments

If the user asks to inspect or respond to unresolved review comments:

1. Prefer the `gh-address-comments` skill/flow when it is available.
2. Otherwise use GitHub GraphQL review threads. Query `reviewThreads` and group by thread, including `isResolved`, `isOutdated`, path, line anchor, author, body, URL, and diff hunk where available.
3. As a last resort, use REST `pulls/{number}/comments`. Clearly state that resolved status is unreliable or unknown with this fallback.

Do not post replies, resolve threads, submit reviews, or modify code unless the user explicitly asks for that write action.
