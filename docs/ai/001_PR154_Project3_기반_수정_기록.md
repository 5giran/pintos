# PR154 Project3 기반 수정 기록

## 전제

이번 브랜치 `codex/pr154-project3-review-fixes`에서는 PR 리뷰 대응을 위해 이전 AGENTS 지침 중 "Pintos 과제 구현 코드를 직접 수정하지 않는다"와 "테스트를 직접 수행하지 않는다" 제한을 해제했다.

목표는 Project 3로 이어가기 전에 Project 2 userprog의 process/file lifetime 문제가 숨은 테스트나 이후 VM 구현에 영향을 주지 않도록, 리뷰에서 지적된 문제만 최소 범위로 직접 수정하는 것이다.

## 수정 내용

### 1. child_status ref count release 원자화

- 위치: `pintos/userprog/process.c`의 `child_status_release()`
- 변경: `ref_cnt` 감소와 `ref_cnt == 0` 판단을 `intr_disable()`/`intr_set_level()` 구간으로 보호했다.
- 이유: parent의 `wait`/exit 경로와 child의 exit 경로가 같은 `child_status`를 release할 수 있으므로, timer preemption으로 `ref_cnt` 감소가 lost update 되는 상황을 막아야 한다.
- 유지한 invariant: `free()`는 최종 참조가 내려간 뒤에만 수행하고, `process_wait()`의 `list_remove()`는 release 전에 수행하는 기존 순서를 유지했다.

### 2. fork child의 running executable deny-write lifetime 상속

- 위치: `pintos/userprog/process.c`의 `__do_fork()`
- 변경: fd table 복제 성공 후, parent의 `running_file`이 있으면 `file_duplicate()`로 child의 `running_file`을 별도로 복제한다.
- 이유: `running_file`은 fd table과 별개 필드라서 fd 복제만으로는 실행 중인 파일의 deny-write lifetime이 child에게 이어지지 않는다. parent가 먼저 exit하더라도 fork child가 같은 executable image를 실행 중이면 deny-write가 유지되어야 한다.
- 실패 처리: `file_duplicate()`가 실패하면 fork 전체를 실패 처리한다. 이미 복제된 fd와 `running_file`은 기존 `thread_exit()` -> `process_exit()` cleanup 경로로 정리된다.

### 3. Project 2 load_segment file offset 공유 제거

- 위치: `pintos/userprog/process.c`의 `#ifndef VM` 아래 `load_segment()`
- 변경: `file_seek()` + `file_read()` 조합을 없애고, page별 `ofs`를 사용해 `file_read_at()`으로 읽는다. 파일 시스템 접근은 기존 전역 `filesys_lock`으로 감쌌다.
- 이유: `struct file`의 `pos`는 mutable state라 concurrent load나 duplicate된 file object 경로에서 섞일 수 있다. offset 기반 읽기를 쓰면 segment load가 file position에 의존하지 않는다.
- Project 3 관련: 이 함수는 `#ifndef VM` 경로라 VM 빌드의 lazy load 구현과는 별개지만, Project 2 기반 안정성과 PR 리뷰 해결을 위해 정리했다.

## 검증 결과

컨테이너 `bcb5da26d1e9` 안에서 `source /workspaces/pintos_22.04_lab_docker/pintos/activate` 후 검증했다.

- `make -C pintos/userprog`: 통과
- 직접 영향권 테스트 PASS: `fork-once`, `fork-multiple`, `fork-read`, `wait-simple`, `wait-twice`, `exec-read`, `rox-simple`, `rox-child`, `rox-multichild`
- `tests/userprog/no-vm/multi-oom`: 아직 FAIL

`multi-oom` 실패 로그는 반복 중 `Should return > 0.` 및 최종 반복 깊이 저하를 보고한다. 이번 리뷰의 `child_status` ref count lost update 가능성은 막았지만, 현재 `multi-oom` 실패는 abnormal child가 많은 자원을 연 뒤 죽는 경로에서 별도 자원 회수 또는 fork 실패 처리 문제가 남아 있을 가능성이 크다. 따라서 이 브랜치는 PR 리뷰 3개 항목의 직접 수정으로는 완료되지만, `multi-oom`까지 완전 통과를 목표로 하면 후속 디버깅이 필요하다.

## 참고

macOS host에서 직접 `make -C pintos/userprog`를 실행하면 ARM clang이 Pintos x86 옵션인 `-mno-sse`를 지원하지 않아 실패한다. 검증은 x86_64 Ubuntu devcontainer에서 수행해야 한다.
