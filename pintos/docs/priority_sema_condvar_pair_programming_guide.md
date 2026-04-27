# priority-sema / priority-condvar 페어프로그래밍 가이드

이 문서는 구현 정답을 적어두는 문서가 아니다. 직접 작성하면서 막히지 않도록, 어떤 현상을 봐야 하는지와 어떤 질문을 스스로 던지면 좋은지만 정리한다.

## 목표

`priority-sema`와 `priority-condvar`는 둘 다 "대기 중인 스레드 중 가장 높은 priority가 먼저 깨어나는가?"를 확인한다.

- `priority-sema`: 하나의 semaphore에 여러 스레드가 막혀 있을 때, `sema_up()`이 가장 높은 priority의 waiter를 깨우는지 본다.
- `priority-condvar`: 하나의 condition variable에 여러 스레드가 `cond_wait()` 중일 때, `cond_signal()`이 가장 높은 priority의 waiter를 signal하는지 본다.

## 현재 코드에서 관찰할 지점

먼저 아래 파일을 읽으면서 현재 동작을 말로 설명해 보자.

- `pintos/threads/synch.c`
  - `sema_down()`
  - `sema_up()`
  - `cond_wait()`
  - `cond_signal()`
- `pintos/threads/thread.c`
  - `thread_unblock()`
  - `thread_yield()`
  - `thread_create()`
  - `thread_set_priority()`
- `pintos/include/threads/thread.h`
  - `struct thread`의 `elem` 주석
- `pintos/include/threads/synch.h`
  - `struct semaphore`
  - `struct condition`

특히 `ready_list`는 이미 priority를 의식하고 있는 반면, synchronization primitive 내부의 waiter list가 어떤 기준으로 관리되는지 비교해서 보자.

## 테스트가 기대하는 흐름

### priority-sema

테스트는 main thread의 priority를 낮춘 뒤, 서로 다른 priority를 가진 worker thread 여러 개를 만든다. worker들은 모두 같은 semaphore에서 기다린다.

관찰 포인트:

- 생성된 worker들이 실제로 `sema_down()`에서 대기 상태가 되는가?
- main thread가 `sema_up()`을 한 번 호출할 때마다 누가 unblock되는가?
- unblock된 high-priority worker가 main thread보다 먼저 출력할 기회를 얻는가?
- 출력 순서가 priority 30, 29, 28, ... 순서로 이어지는가?

### priority-condvar

테스트는 여러 worker가 같은 condition variable에서 기다리게 만든다. main thread가 `cond_signal()`을 반복 호출하면서 하나씩 깨운다.

관찰 포인트:

- `cond_wait()`에 들어간 worker들이 condition variable의 waiter list에 어떤 형태로 들어가는가?
- `cond_signal()`이 waiter 하나를 고를 때 무엇을 기준으로 고르는가?
- condition variable waiter 자체는 thread가 아니라 wrapper 구조체라는 점을 놓치지 않았는가?
- signal된 worker가 lock을 다시 얻고 나서 priority 순서로 출력되는가?

## 구현 전 체크리스트

아래 질문에 답할 수 있으면 구현을 시작해도 좋다.

- semaphore의 `waiters` list에는 어떤 타입의 원소가 들어가는가?
- condition variable의 `waiters` list에는 어떤 타입의 원소가 들어가는가?
- `struct thread.elem`은 언제 ready list에 있고, 언제 semaphore waiter list에 있는가?
- condition variable waiter 안쪽의 semaphore에는 실제로 어떤 thread가 기다리게 되는가?
- "가장 높은 priority"를 비교하려면 각 list element에서 어떤 실제 구조체까지 도달해야 하는가?
- waiter list를 넣을 때 정렬할지, 꺼낼 때 고를지 결정했는가?
- 같은 priority끼리는 반드시 어떤 순서여야 하는가, 아니면 테스트상 크게 상관없는가?

## 설계 선택지

정답은 하나만 있는 게 아니다. 아래 중 하나의 방향을 택해서 일관되게 밀고 가면 된다.

### 방향 A: 들어갈 때 정렬하기

waiter가 list에 들어가는 순간 priority 순서를 유지한다. 나중에 깨울 때는 list의 앞쪽 원소가 자연스럽게 가장 높은 priority가 되도록 만든다.

생각할 점:

- 삽입 시점의 priority와 signal/up 시점의 priority가 달라질 수 있는가?
- 기존 코드의 ready list 정렬 방식과 비슷한 사고방식으로 볼 수 있는가?
- condition variable의 waiter는 thread 자체가 아니므로 비교 기준을 어떻게 잡을 것인가?

### 방향 B: 깨울 때 선택하기

waiter list는 평소에는 단순히 모아두고, `up` 또는 `signal` 순간에 가장 높은 priority의 waiter를 찾아 깨운다.

생각할 점:

- 깨우기 직전에 고르면 priority 변화에 더 강한가?
- 선택한 원소를 list에서 제거하는 흐름을 정확히 이해했는가?
- interrupt disabled 구간 안에서 list를 만지는 이유를 설명할 수 있는가?

## 직접 구현할 때의 순서 제안

1. `priority-sema`만 먼저 통과시키는 것을 목표로 한다.
2. semaphore waiter list에서 "누가 먼저 깨어나야 하는지" 기준을 세운다.
3. `sema_up()` 이후 스케줄러가 바로 high-priority thread에게 실행 기회를 주는지 확인한다.
4. 그 다음 `priority-condvar`로 넘어간다.
5. condition variable waiter가 감싼 semaphore의 내부 waiter를 어떻게 해석할지 정리한다.
6. `cond_signal()`이 어떤 waiter를 signal해야 하는지 결정한다.
7. 두 테스트를 각각 단독으로 돌린 뒤, threads 테스트 전체에서 다른 테스트가 깨지지 않는지 본다.

## 디버깅 힌트

막히면 출력 순서를 기준으로 어느 단계가 어긋났는지 좁혀보자.

- FIFO 순서로 깨어난다: waiter list가 아직 삽입 순서 그대로 사용되고 있을 가능성이 높다.
- 가장 높은 thread가 unblock은 되지만 main 출력이 먼저 나온다: unblock 이후 preemption/yield 흐름을 확인한다.
- `priority-sema`는 통과하는데 `priority-condvar`만 실패한다: condition variable waiter 비교 기준이 thread priority까지 도달하지 못했을 가능성이 있다.
- assertion이 난다: list element가 어느 list에 들어가 있는 상태인지 다시 확인한다. `struct thread.elem`은 동시에 여러 list에 들어갈 수 없다.
- 가끔만 실패한다: 정렬 기준, 동률 처리, interrupt disable 구간을 확인한다.

## 보면 좋은 list API

구현하면서 필요할 때만 `pintos/lib/kernel/list.c`와 `pintos/include/lib/kernel/list.h`를 열어보자.

특히 다음 API의 의미를 말로 설명할 수 있으면 충분하다.

- ordered insert 계열
- max/min 선택 계열
- remove/pop 계열
- list element에서 container 구조체를 얻는 매크로

여기서 중요한 건 API 이름을 외우는 게 아니라, "이 list의 element가 실제로 어떤 구조체의 멤버인가?"를 계속 추적하는 것이다.

## 완료 기준

최소 확인:

- `priority-sema`가 기대 순서대로 통과한다.
- `priority-condvar`가 기대 순서대로 통과한다.
- 기존 priority scheduling 테스트가 새 변경 때문에 퇴행하지 않는다.

추천 확인:

- threads 테스트 묶음을 돌려본다.
- 변경한 함수마다 interrupt level을 왜 그렇게 다루는지 직접 설명해 본다.
- `sema_down()` / `sema_up()` / `cond_wait()` / `cond_signal()`에서 list에 들어가는 원소 타입을 종이에 그려본다.

## 페어프로그래밍 규칙

네가 직접 구현한다.

내 역할은 다음으로 제한한다.

- 테스트 실패 로그를 같이 읽고 원인을 좁힌다.
- list와 scheduler 흐름을 질문으로 정리해 준다.
- 네가 작성한 코드에서 위험한 지점을 리뷰한다.
- 필요하면 특정 함수의 현재 동작을 말로 풀어 설명한다.

내가 하지 않을 것:

- 완성 코드를 먼저 제시하지 않는다.
- 비교 함수나 삽입/선택 로직을 그대로 써주지 않는다.
- 네가 해결하고 싶은 핵심 부분을 대신 구현하지 않는다.

