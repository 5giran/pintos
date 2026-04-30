# gh-pr-review 배포 및 사용 매뉴얼

`gh-pr-review`는 현재 브랜치의 GitHub PR을 리뷰하는 Codex 스킬입니다. 로컬 git 상태 점검, pull 여부 확인, PR 컨텍스트 수집, diff 리뷰, unresolved review comments 확인 흐름을 처리합니다. Pintos repo 또는 Pintos 경로가 감지되면 KAIST CS330 64-bit Pintos 리뷰 모드로 전환됩니다.

## 패키지 구성

```text
gh-pr-review-skill-package/
├── gh-pr-review/
│   ├── SKILL.md
│   ├── agents/
│   │   └── openai.yaml
│   └── references/
│       ├── general-checklist.md
│       ├── pintos-checklist.md
│       └── review-workflow.md
├── install.sh
├── install.ps1
└── MANUAL.md
```

`gh-pr-review/` 폴더가 실제 Codex skill입니다. `MANUAL.md`, `install.sh`, `install.ps1`은 배포 편의를 위한 바깥 파일이며 skill 내부에는 포함되지 않습니다.

## 설치

압축을 푼 뒤 패키지 루트에서 운영체제에 맞는 설치 스크립트를 실행합니다.

macOS/Linux:

```bash
./install.sh
```

Windows PowerShell:

```powershell
.\install.ps1
```

PowerShell 실행 정책 때문에 막히면 아래처럼 실행할 수 있습니다.

```powershell
powershell -ExecutionPolicy Bypass -File .\install.ps1
```

설치 위치는 다음 규칙을 따릅니다.

```bash
${CODEX_HOME:-$HOME/.codex}/skills/gh-pr-review
```

Windows에서는 `$env:CODEX_HOME`이 있으면 그 아래 `skills\gh-pr-review`에 설치하고, 없으면 `$HOME\.codex\skills\gh-pr-review`에 설치합니다.

이미 같은 이름의 스킬이 있으면 `gh-pr-review.backup.YYYYMMDDHHMMSS` 형태로 백업한 뒤 새 버전을 설치합니다.

수동 설치를 원하면 아래처럼 복사해도 됩니다.

```bash
mkdir -p "${CODEX_HOME:-$HOME/.codex}/skills"
cp -R gh-pr-review "${CODEX_HOME:-$HOME/.codex}/skills/gh-pr-review"
```

설치 후 새 Codex thread를 열면 스킬 목록에 반영되는 것이 가장 확실합니다.

## 사전 조건

- `gh` GitHub CLI 설치 및 인증 완료
- 현재 작업 디렉터리가 git repository
- 현재 브랜치에 연결된 GitHub PR이 있음
- PR 리뷰를 위해 GitHub repo 접근 권한이 있음

## 기본 사용법

가장 일반적인 호출:

```text
현재 브랜치에서 $gh-pr-review 를 사용해 pull 여부를 먼저 확인한 뒤 PR을 리뷰해줘.
```

pull 없이 현재 로컬 상태 기준으로만 리뷰:

```text
현재 브랜치에서 $gh-pr-review 를 사용해줘. pull은 하지 말고 현재 로컬 상태 기준으로 PR을 리뷰해줘.
```

Pintos 특화 리뷰:

```text
현재 브랜치에서 $gh-pr-review 를 사용해 PR을 리뷰해줘. Pintos 모드가 감지되면 phase와 grading test 영향까지 같이 봐줘.
```

unresolved review comments 확인:

```text
현재 브랜치에서 $gh-pr-review 를 사용해서 unresolved review comments를 확인하고, 어떤 코멘트에 어떻게 대응해야 할지 정리해줘.
```

## GitHub PR 코멘트 게시

기본 리뷰는 GitHub에 아무것도 쓰지 않습니다. PR에 댓글을 남기려면 게시 의도를 명시해야 합니다.

게시 전 초안 확인:

```text
현재 브랜치에서 $gh-pr-review 로 리뷰한 뒤, GitHub PR에 올릴 리뷰 댓글 초안을 먼저 보여줘. 내가 확인하면 게시해줘.
```

일반 PR 코멘트로 게시:

```text
현재 브랜치에서 $gh-pr-review 로 리뷰하고, 발견한 주요 findings를 GitHub PR에 review comment로 게시해줘.
```

라인별 inline comment:

```text
현재 브랜치에서 $gh-pr-review 로 리뷰하고, line anchor가 정확한 findings만 GitHub inline review comment로 달아줘. 파일 전반 의견은 일반 PR 코멘트로 남겨줘.
```

GitHub에 게시되는 코멘트에는 Codex 내부 작업 안내, 예를 들어 `"고쳐줘"`라고 말하라는 문구를 넣지 않도록 되어 있습니다.

## 동작 요약

1. 로컬 상태 확인: clean, dirty, detached HEAD, upstream 여부
2. pull 여부 확인: 자동 pull 금지, 사용자가 허용하면 `git pull --ff-only`
3. PR 식별: `gh pr view`
4. PR 메타데이터와 변경 파일 수집
5. Pintos 모드 및 phase 자동 감지
6. 일반 또는 Pintos 체크리스트로 diff 리뷰
7. findings-first 형식으로 결과 출력
8. 명시 요청이 있을 때만 GitHub write action 수행

## 안전 정책

- 코드 수정은 하지 않습니다.
- PR 댓글 작성, review submit, thread resolve는 명시 요청 없이는 하지 않습니다.
- dirty 상태에서는 먼저 멈추고 선택지를 제시합니다.
- `git pull --ff-only` 외 merge 전략은 자동 사용하지 않습니다.
- PR이 없으면 자동 생성하지 않습니다.

## Pintos 모드

다음 중 하나라도 감지되면 Pintos 모드가 켜집니다.

- repo 이름 또는 remote URL에 `pintos` 포함
- 변경 파일 경로에 `threads/`, `userprog/`, `vm/`, `filesys/` 포함
- `pintos/threads/`, `pintos/userprog/`, `pintos/vm/`, `pintos/filesys/` 같은 중간 경로 포함
- `Makefile.build` 또는 Pintos 유틸 경로 존재
- README/docs에 `KAIST CS330`, `Pintos`, `64-bit Pintos` 언급

phase 추정:

- `threads/` -> Phase 1
- `userprog/` -> Phase 2
- `vm/` -> Phase 3
- `filesys/` -> Phase 4

## 검증

스킬 구조 검증:

```bash
python3 /path/to/skill-creator/scripts/quick_validate.py gh-pr-review
```

확인할 핵심 항목:

- `SKILL.md` frontmatter에 `name`, `description` 존재
- `agents/openai.yaml`의 `default_prompt`에 `$gh-pr-review` 포함
- `policy.allow_implicit_invocation: false`
- `references/`에 세 체크리스트 파일 존재

## 문제 해결

스킬이 호출되지 않는 경우:

- `$gh-pr-review`처럼 `$` 포함해 명시 호출했는지 확인합니다.
- 설치 후 새 Codex thread를 열어 다시 시도합니다.
- 설치 경로가 `${CODEX_HOME:-$HOME/.codex}/skills/gh-pr-review`인지 확인합니다.

PR을 못 찾는 경우:

- 현재 브랜치에 PR이 연결되어 있는지 확인합니다.
- `gh pr view`가 현재 repo에서 동작하는지 확인합니다.
- `gh auth status`로 인증 상태를 확인합니다.

GitHub에 댓글이 안 달리는 경우:

- 프롬프트에 `게시해줘`, `PR 댓글로 남겨줘`, `inline comment로 달아줘`처럼 write action을 명시했는지 확인합니다.
- repo 권한과 `gh` 인증 상태를 확인합니다.
