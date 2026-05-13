# Codex prompt logging

This repository uses a Codex `UserPromptSubmit` hook to append local prompt logs to:

```text
.codex/logs/user-prompts.jsonl
```

The log directory is intentionally ignored by Git. Do not commit raw prompt logs because prompts may contain private notes, credentials, or other sensitive data.

To check whether hooks are enabled in your local Codex installation:

```sh
codex features list
```

The `hooks` feature should be enabled. If needed, enable it in `~/.codex/config.toml`:

```toml
[features]
hooks = true
```

Open this repository as a trusted Codex project so repo-local hooks can run.
