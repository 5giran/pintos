#!/usr/bin/env bash
set -euo pipefail

PACKAGE_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
SOURCE_DIR="$PACKAGE_DIR/gh-pr-review"
SKILLS_DIR="${CODEX_HOME:-$HOME/.codex}/skills"
TARGET_DIR="$SKILLS_DIR/gh-pr-review"

if [[ ! -f "$SOURCE_DIR/SKILL.md" ]]; then
  echo "ERROR: Cannot find skill source at $SOURCE_DIR" >&2
  exit 1
fi

mkdir -p "$SKILLS_DIR"

if [[ -e "$TARGET_DIR" ]]; then
  BACKUP_DIR="${TARGET_DIR}.backup.$(date +%Y%m%d%H%M%S)"
  mv "$TARGET_DIR" "$BACKUP_DIR"
  echo "Backed up existing skill to: $BACKUP_DIR"
fi

cp -R "$SOURCE_DIR" "$TARGET_DIR"

echo "Installed gh-pr-review skill to: $TARGET_DIR"
echo "Open a new Codex thread, then invoke it with: 현재 브랜치에서 \$gh-pr-review 를 사용해 pull 여부를 먼저 확인한 뒤 PR을 리뷰해줘."
