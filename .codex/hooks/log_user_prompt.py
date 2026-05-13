#!/usr/bin/env python3
"""Append Codex UserPromptSubmit payloads to a repo-local JSONL log."""

from __future__ import annotations

import json
import os
import sys
from datetime import datetime, timezone, timedelta
from pathlib import Path


KST = timezone(timedelta(hours=9))


def main() -> int:
    try:
        payload = json.load(sys.stdin)
    except json.JSONDecodeError:
        return 0

    hook_dir = Path(__file__).resolve().parent
    codex_dir = hook_dir.parent
    repo_root = codex_dir.parent

    cwd_value = (
        payload.get("cwd")
        or payload.get("current_working_directory")
        or os.getcwd()
    )

    try:
        cwd = Path(cwd_value).resolve()
    except OSError:
        return 0

    try:
        cwd.relative_to(repo_root)
    except ValueError:
        return 0

    now_utc = datetime.now(timezone.utc)
    prompt = payload.get("prompt")
    if prompt is None:
        prompt = payload.get("userPrompt")

    entry = {
        "timestamp_utc": now_utc.isoformat(),
        "timestamp_kst": now_utc.astimezone(KST).isoformat(),
        "cwd": str(cwd),
        "repo_root": str(repo_root),
        "session_id": payload.get("session_id") or payload.get("sessionId"),
        "turn_id": payload.get("turn_id") or payload.get("turnId"),
        "transcript_path": payload.get("transcript_path") or payload.get("transcriptPath"),
        "hook_event_name": payload.get("hook_event_name") or payload.get("hookEventName"),
        "model": payload.get("model"),
        "permission_mode": payload.get("permission_mode") or payload.get("permissionMode"),
        "prompt": prompt,
    }

    try:
        log_dir = codex_dir / "logs"
        log_dir.mkdir(parents=True, exist_ok=True)
        with (log_dir / "user-prompts.jsonl").open("a", encoding="utf-8") as log_file:
            log_file.write(json.dumps(entry, ensure_ascii=False, sort_keys=True))
            log_file.write("\n")
    except OSError:
        return 0

    return 0


if __name__ == "__main__":
    raise SystemExit(main())
