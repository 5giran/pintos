# PintOS Project 1 - Priority Scheduling Session 2 계획

## 1. Session 2 목표

Session 2의 목표는 semaphore와 condition variable에서 기다리는 스레드들이 priority 순서대로 깨어나도록 만드는 것이다.

현재 priority scheduling 구현은 READY 상태의 스레드들이 priority 순서대로 실행되도록 ready queue를 관리한다. 하지만 스레드가 READY 상태가 아니라 semaphore나 condition variable에서 BLOCKED 상태로 기다리고 있다면, 그 스레드는 아직 ready queue에 들어가 있지 않다.

따라서 priority scheduling을 완성하려면 READY 상태가 된 이후의 순서뿐 아니라, 동기화 객체에서 기다리던 스레드가 다시 READY 상태로 돌아오는 순서도 priority 규칙을 따라야 한다.

Session 2에서는 priority donation은 다루지 않는다. 목표는 donation 없이도 semaphore와 condition variable의 대기 스레드들이 priority 높은 순서대로 깨어나는지 확인하는 것이다.

## 2. 전제 조건

Session 2를 시작하기 전에 다음 테스트들이 이미 통과한 상태라고 가정한다.

```text
alarm-priority
priority-change
priority-fifo
priority-preempt
```

이 테스트들이 통과한다는 것은 READY 상태의 스레드들 사이에서는 priority scheduling이 어느 정도 동작하고 있다는 뜻이다.

Session 2는 여기서 한 단계 더 나아가, BLOCKED 상태로 기다리던 스레드들이 깨어나는 순서까지 priority 기준을 따르는지 확인한다.

## 3. 통과 목표 테스트

## `priority-sema`

`priority-sema`는 semaphore에서 기다리는 여러 스레드가 있을 때, 가장 높은 priority를 가진 스레드부터 깨어나는지 확인하는 테스트다.

테스트의 핵심 흐름은 다음과 같다.

- main thread가 자기 priority를 낮춘다.
- 서로 다른 priority를 가진 여러 스레드를 만든다.
- 생성된 스레드들은 모두 같은 semaphore에서 기다린다.
- main thread가 semaphore를 여러 번 signal한다.
- signal될 때마다 가장 높은 priority의 대기 스레드가 먼저 깨어나야 한다.

기대되는 출력 순서는 priority가 높은 스레드부터 낮은 스레드 순서다.

```text
Thread priority 30 woke up.
Back in main thread.
Thread priority 29 woke up.
Back in main thread.
Thread priority 28 woke up.
...
Thread priority 21 woke up.
Back in main thread.
```

이 테스트가 확인하는 핵심은 semaphore의 대기 순서가 단순 FIFO가 아니라 priority 기준이어야 한다는 점이다.

또한 높은 priority 스레드가 깨어났다면, 낮은 priority의 현재 스레드가 계속 실행되는 것이 아니라 높은 priority 스레드가 먼저 실행될 수 있어야 한다.

## `priority-condvar`

`priority-condvar`는 condition variable에서 기다리는 여러 스레드가 있을 때, 가장 높은 priority를 가진 스레드부터 signal을 받는지 확인하는 테스트다.

테스트의 핵심 흐름은 다음과 같다.

- 서로 다른 priority를 가진 여러 스레드를 만든다.
- 각 스레드는 같은 condition variable에서 기다린다.
- main thread가 condition variable에 signal을 여러 번 보낸다.
- signal을 받을 때마다 가장 높은 priority의 대기 스레드가 먼저 깨어나야 한다.

기대되는 출력 순서는 priority가 높은 스레드부터 낮은 스레드 순서다.

```text
Signaling...
Thread priority 30 woke up.
Signaling...
Thread priority 29 woke up.
Signaling...
Thread priority 28 woke up.
...
Thread priority 21 woke up.
```

이 테스트가 확인하는 핵심은 condition variable의 대기 스레드 선택도 priority scheduling 규칙을 따라야 한다는 점이다.

condition variable은 lock과 함께 사용되므로, 단순히 어떤 스레드를 깨우는지만이 아니라 signal 이후 lock을 다시 획득하고 실행되는 순서까지 priority 기대 결과와 맞아야 한다.

## 4. 직접 분석할 때 확인할 질문

구현 전에 다음 질문들을 팀끼리 먼저 정리하면 좋다.

- READY 상태의 스레드 순서와 BLOCKED 상태의 대기 순서는 각각 어디에서 관리되는가?
- semaphore에서 여러 스레드가 기다릴 때, 어떤 기준으로 다음에 깨어날 스레드를 선택해야 하는가?
- condition variable에서 여러 스레드가 기다릴 때, 어떤 기준으로 signal을 받을 스레드를 선택해야 하는가?
- 같은 priority를 가진 스레드들이 있다면 기존 FIFO 성질을 유지해야 하는가?
- 높은 priority 스레드가 깨어난 직후, 현재 실행 중인 낮은 priority 스레드는 계속 실행해도 되는가?
- 기존에 통과하던 priority scheduling 테스트들이 깨지지 않게 하려면 어떤 동작을 유지해야 하는가?

## 5. Session 2 완료 기준

Session 2의 완료 기준은 다음과 같다.

- `priority-sema` 통과
- `priority-condvar` 통과
- 기존 priority scheduling 테스트 유지
- donation 관련 테스트는 아직 통과하지 않아도 됨

전체 threads 테스트는 다음 명령으로 실행할 수 있다.

```bash
make -C pintos/threads/build check
```
