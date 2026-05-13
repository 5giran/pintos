# 이번 주 branch fix 로그 정리

작성 기준: 2026-05-13, `git fetch origin --prune` 이후 현재 로컬 저장소에서 확인 가능한 refs.

검토 범위는 2026-05-11 00:00 이후의 local branch와 `origin/*` remote branch다. 테스트는 직접 실행하지 않았고, 커밋 메시지, 브랜치명, 변경 파일, revert/debug/unfixed 기록을 근거로 정리했다. 따라서 아래의 "확정"은 커밋 메시지에 커널 패닉, test fail, unfixed, revert 등 직접 증거가 있는 경우이고, "의심"은 fix/debug가 몰린 위치를 기준으로 한 추정이다.

## 요약

가장 의심도가 높은 흐름은 세 군데다.

1. `issue-317-exec-test-fail`: 브랜치명 자체가 exec test fail이고, `pintos/userprog/syscall.c`의 사용자 버퍼 검증, `SYS_EXEC` 처리, read/write 검증 방향을 짧은 시간에 여러 번 수정했다.
2. `issue-306-lazy_load_segment` 및 여기서 파생된 local `issue-311-vm_stack_page_allocation`: `spt_find_page`, `vm_try_handle_fault`, stack setup, SPT cleanup에서 커널 패닉 또는 page fault 처리 실패를 직접 언급한 커밋이 있다.
3. `issue-309-hash_spt` 및 `origin/revert-312-issue-309-hash_spt`: hash 기반 SPT 구현이 PR merge 후 revert 되었고, hash key, dummy page, malloc 실패 처리 fix가 이어졌다.

fix/debug/revert 커밋이 몰린 파일은 다음 순서다.

| 파일 | 문제성 커밋 수 | 의심 영역 |
| --- | ---: | --- |
| `pintos/vm/vm.c` | 18 | SPT 조회/삽입/삭제, page fault, frame claim |
| `pintos/userprog/syscall.c` | 12 | syscall 인자 검증, read/write 버퍼 검증, `SYS_EXEC` |
| `pintos/userprog/process.c` | 7 | lazy load, stack setup, process load 흐름 |
| `pintos/lib/kernel/hash.c` | 5 | SPT hash key/compare 구현 위치와 방식 |

## branch별 확인 포인트

| branch | 현재 head | 문제 증거 | 확인할 커밋 |
| --- | --- | --- | --- |
| `issue-317-exec-test-fail`, `origin/issue-317-exec-test-fail` | `3c1fcfb` | 확정: branch명 test fail, `unfixed` 커밋, debug/fix 연쇄 | `3c1fcfb`, `8e82a39`, `b2d0861`, `47b47b2`, `d2cf410`, `b933c71`, `dca62f5` |
| `issue-306-lazy_load_segment`, `origin/issue-306-lazy_load_segment` | `7cbbbbc` | 확정: SPT 미매핑 VA 커널 패닉, SPT cleanup 오류 | `7cbbbbc`, `c444a36`, `d7be070`, `974e100`, `41f62c5` |
| `issue-309-hash_spt`, `origin/issue-309-hash_spt` | `28af638` | 확정/강한 의심: hash SPT 구현 후 revert branch 존재, SPT 조회/키 계산 fix 다수 | `28af638`, `974e100`, `41f62c5`, `d3cb66d`, `1e1146f`, `5398ca4` |
| `origin/revert-312-issue-309-hash_spt` | `9742bbd` | 확정: PR #312의 hash 기반 SPT 구현 revert | `9742bbd` |
| `issue-307-exec-lazy-loading`, `origin/issue-307-exec-lazy-loading` | `4d053f2` | 의심: uninit/page claim 실패 시 자원 해제 책임을 수정했다가 revert 후 재수정 | `4d053f2`, `638c615`, `a87f2e3`, `03a157c` |
| local `issue-311-vm_stack_page_allocation` | `7e9f0c3` | 의심: local에 VM/stack 관련 커밋이 많이 쌓였고, 커널 패닉 fix를 포함한다 | `7e9f0c3`, `d7be070`, `c444a36`, `28af638` |
| `origin/issue-311-vm_stack_page_allocation` | `1b19ab9` | 낮음: remote에는 변수명 refactor만 있음 | `1b19ab9` |
| `origin/issue-308-uninitanon-initializer` | `47f054a` | 낮음: initializer ASSERT/type 변경, 명시적 fail/fix 메시지는 없음 | `47f054a`, `35338e3` |
| `issue-316-fix_exec`, `origin/issue-316-fix_exec` | `d16aba8` | 낮음: 테스트 방법/Makefile 문서화 성격 | `d16aba8`, `7e9f0c3` |
| `origin/main` | `9815445` | 낮음: docs commit만 확인 | `9815445` |

참고: local `issue-311-vm_stack_page_allocation`은 현재 upstream이 `origin/issue-306-lazy_load_segment`로 잡혀 있다. remote `origin/issue-311-vm_stack_page_allocation`과는 내용이 크게 다르므로, 팀원이 branch 기준으로 확인할 때 이 점을 먼저 확인하는 편이 좋다.

## 사고 로그 후보

### 1. exec test fail 및 syscall 사용자 버퍼 검증

- branch: `issue-317-exec-test-fail`, `origin/issue-317-exec-test-fail`
- 주요 파일: `pintos/userprog/syscall.c`, 일부 `pintos/vm/vm.c`
- 확정 근거: branch명에 `exec-test-fail`, `dca62f5`의 `unfixed`, debug 커밋 2개, fix 커밋 다수

확인할 커밋:

| 커밋 | 증상/문제 | 해결 방향 |
| --- | --- | --- |
| `dca62f5` | "unfixed" checkpoint. 퇴근 전 미해결 상태를 올린 기록 | 이후 커밋에서 syscall 검증과 page fault 처리를 계속 수정 |
| `8e82a39` | `SYS_EXEC` case가 다음 case로 떨어질 수 있는 switch 흐름 | `SYS_EXEC` 처리 뒤 break 추가 |
| `b933c71` | page fault 처리 중 NULL 주소나 SPT lookup 실패를 정상 실패로 다루지 못함 | `vm_try_handle_fault`에서 NULL/fault 불가 조건을 false로 반환 |
| `d2cf410` | non-present PTE 상태에서 user bit 검사 조건이 맞지 않음 | present 여부와 user 영역 검사를 분리하도록 조건 수정 |
| `47b47b2` | `validate_user_read/write` 호출 인자 오류 | read/write 검증 함수로 넘기는 인자 정정 |
| `b2d0861` | read/write syscall에서 이미 내부 검증이 있는데 중복 검증 수행 | 중복 validate 제거 |
| `3c1fcfb` | read/write 접근 방향을 거꾸로 넘긴 최종 오류 | `validate_user_read`와 `validate_user_write`의 접근 방향 정상화 |

판단: 실제 문제는 단일 원인이라기보다 `exec` syscall 주변에서 사용자 포인터 검증, lazy page claim, switch 흐름이 함께 흔들린 것으로 보인다. 팀원이 확인할 때는 `3c1fcfb`만 보지 말고 `dca62f5..3c1fcfb` 범위를 같이 보는 것이 좋다.

### 2. SPT 미매핑 VA에서 커널 패닉

- branch: `issue-306-lazy_load_segment`, local `issue-311-vm_stack_page_allocation`, `origin/issue-317-exec-test-fail`에도 포함
- 주요 파일: `pintos/vm/vm.c`
- 확정 근거: `c444a36` 커밋 메시지가 "커널 패닉 발생하던 문제 해결"을 직접 언급

확인할 커밋:

| 커밋 | 증상/문제 | 해결 방향 |
| --- | --- | --- |
| `c444a36` | VA가 SPT에 매핑되어 있지 않은데 예외처리 없이 hash lookup 결과를 사용해 커널 패닉 | `spt_find_page`에서 lookup 실패 시 NULL 반환 |
| `b933c71` | page fault 처리에서도 NULL 주소/SPT miss를 실패로 처리해야 함 | `vm_try_handle_fault`에서 fault 처리 불가 조건을 false로 반환 |
| `974e100` | `spt_find_page`의 조회용 dummy page를 동적 할당하던 문제 | 조회용 객체 관리 방식을 정리해 불필요한 할당/해제 위험 축소 |
| `41f62c5` | SPT hash key를 VA 값이 아니라 VA가 가리키는 메모리처럼 다룰 수 있는 구현 | page-aligned user virtual address 값을 key로 쓰도록 수정 |

판단: 이 라인은 SPT 조회 실패를 정상적인 "page 없음"으로 처리하지 못한 것이 핵심 사고다. hash key 계산 오류와 dummy page 관리 방식도 같은 영역의 전조로 볼 수 있다.

### 3. SPT cleanup 누락

- branch: `issue-306-lazy_load_segment`, `origin/issue-306-lazy_load_segment`
- 주요 파일: `pintos/vm/vm.c`, `pintos/include/threads/thread.h`, `pintos/userprog/process.c`
- 확정 근거: `7cbbbbc` 커밋 메시지가 "spt 청소 안 해줘서 생기던 오류 해결"을 직접 언급

확인할 커밋:

| 커밋 | 증상/문제 | 해결 방향 |
| --- | --- | --- |
| `7cbbbbc` | thread/process 종료 시 SPT에 남은 page/frame 정리가 되지 않는 오류 | `supplemental_page_table_kill` 및 hash destroy 흐름 추가 |

주의: 이 커밋에는 cleanup 구현 외에 debug `printf`가 포함되어 있다. 팀원이 실제 최종 코드로 가져갈 때는 debug 출력이 남아 있는지 별도로 확인해야 한다.

### 4. hash 기반 SPT 구현 revert

- branch: `issue-309-hash_spt`, `origin/issue-309-hash_spt`, `origin/revert-312-issue-309-hash_spt`
- 주요 파일: `pintos/vm/vm.c`, `pintos/lib/kernel/hash.c`, `pintos/include/vm/vm.h`
- 확정 근거: `origin/revert-312-issue-309-hash_spt`가 존재하고 `9742bbd`가 PR #312의 hash 기반 SPT 구현을 revert

확인할 커밋:

| 커밋 | 증상/문제 | 해결 방향 |
| --- | --- | --- |
| `9742bbd` | hash 기반 supplemental page table 구현 전체가 revert 됨 | `pintos/vm/vm.c`, `pintos/lib/kernel/hash.c`, `pintos/include/vm/vm.h`의 hash SPT 변경 제거 |
| `41f62c5` | hash key 계산이 VA 값 기준이 아니어서 잘못된 메모리를 읽을 수 있음 | SPT key를 page-aligned VA 값 기준으로 계산 |
| `5398ca4` | `spt_find_page`에서 hash miss를 NULL로 처리하지 못함 | hash miss 시 NULL 반환 및 조회용 객체 정리 |
| `1e1146f` | `hash_insert`에 넘기는 대상이 잘못됨 | SPT 내부 hash table을 삽입 대상으로 넘기도록 수정 |
| `d3cb66d` | `vm_alloc_page_with_initializer` 성공 경로 반환 및 실패 정리 누락 | 성공 시 true 반환, 실패 시 page 정리 |
| `28af638` | malloc 실패 처리 누락 | 할당 실패 시 실패 반환/정리 흐름 추가 |

판단: 이 영역은 fix가 많은 정도를 넘어 revert branch가 남아 있으므로, 실제 사고 가능성이 매우 높다. 팀원이 보려면 `issue-309-hash_spt`의 초기 hash 함수 커밋부터 `9742bbd` revert까지 보는 것이 가장 빠르다.

### 5. lazy loading의 자원 해제 책임

- branch: `issue-307-exec-lazy-loading`, `origin/issue-307-exec-lazy-loading`
- 주요 파일: `pintos/vm/uninit.c`, `pintos/vm/vm.c`, `pintos/userprog/process.c`
- 근거: fix 이후 revert, 다시 fix가 이어진 흐름

확인할 커밋:

| 커밋 | 증상/문제 | 해결 방향 |
| --- | --- | --- |
| `03a157c` | `uninit_initialize` 실패 시 자원 정리 책임을 단일화하려는 fix | 실패 처리 책임을 한쪽으로 모으는 방향 |
| `a87f2e3` | 위 fix를 revert | 책임 위치가 맞지 않았거나 부작용이 있었던 것으로 추정 |
| `638c615` | `process.c` 쪽 자원 해제 책임 조정 | 다른 위치의 해제 제거 |
| `4d053f2` | 최종적으로 자원 해제 책임을 `vm_do_claim_page` 쪽으로 넘김 | claim 실패 처리 경로에서 정리하도록 조정 |

판단: 명시적인 kernel panic/test fail 메시지는 없지만, 실패 경로에서 double free, leak, use-after-free 류 문제가 있었을 가능성이 있다. 단정하려면 해당 시점의 실패 로그가 필요하다.

### 6. setup_stack 실패

- branch: local `issue-311-vm_stack_page_allocation`, `issue-306-lazy_load_segment` 계열
- 주요 파일: `pintos/userprog/process.c`
- 근거: `d7be070` 커밋 메시지

확인할 커밋:

| 커밋 | 증상/문제 | 해결 방향 |
| --- | --- | --- |
| `d7be070` | `setup_stack`이 무조건 false를 반환 | 성공/실패 반환 흐름 수정. 단, 커밋 메시지상 추가 예외처리 필요 |

판단: stack setup이 항상 실패하면 exec/load 계열 테스트가 연쇄적으로 실패할 수 있다. 이 커밋은 `issue-317-exec-test-fail`의 exec 실패 흐름과도 연결해서 보는 것이 좋다.

## 이번 주 refs 상태

현재 refs 기준으로 2026-05-11 이후 커밋이 확인된 branch는 다음과 같다.

| ref | 이번 주 reachable commit 수 | 비고 |
| --- | ---: | --- |
| `issue-317-exec-test-fail` / `origin/issue-317-exec-test-fail` | 66 | exec test fail fix 라인. 조상 커밋 포함 |
| `issue-306-lazy_load_segment` / `origin/issue-306-lazy_load_segment` | 51 | lazy loading/SPT fix 라인 |
| local `issue-311-vm_stack_page_allocation` | 50 | local 작업이 많음. upstream 설정이 `origin/issue-306-lazy_load_segment`로 되어 있음 |
| `issue-309-hash_spt` / `origin/issue-309-hash_spt` | 38 | hash SPT fix 라인 |
| `origin/revert-312-issue-309-hash_spt` | 38 | hash SPT revert 라인 |
| `issue-307-exec-lazy-loading` / `origin/issue-307-exec-lazy-loading` | 19 | exec lazy loading/resource ownership |
| `origin/issue-308-uninitanon-initializer` | 3 | initializer 타입/ASSERT 변경 |
| `issue-316-fix_exec` / `origin/issue-316-fix_exec` | 2 | 테스트 방법 문서화 |
| `origin/main` | 1 | docs commit |

`git fetch origin --prune` 중 삭제된 remote refs도 있었다: `origin/develop`, `origin/feature/p1-alarm-clock`, `origin/feature/p1-priority-scheduling`, `origin/merge/ys-ts-into-develop`, `origin/pr-310`. 현재 저장소의 remote ref로는 더 이상 남아 있지 않으므로 이 문서의 branch별 표에서는 제외했다.

## 팀원이 직접 확인할 때 쓸 명령

커밋 하나의 변경 요약:

```bash
git show --stat <commit>
```

커밋 전후 흐름 확인:

```bash
git log --oneline --decorate --date=short --since='2026-05-11 00:00' <branch>
```

특정 커밋이 어느 branch에 포함되어 있는지 확인:

```bash
git branch --all --contains <commit>
```

문제성 커밋만 빠르게 보기:

```bash
git log --all --since='2026-05-11 00:00' --regexp-ignore-case \
  --grep='fix\|debug\|unfixed\|revert\|fail\|panic\|오류\|문제\|실패' \
  --date=iso-strict --pretty=format:'%h %ad %D %s'
```

