# PintOS Project 1 - Priority Donation 설계 합의 노트

## 1. `thread_set_priority(21)`이 의미하는 것

`priority-donate-lower` 테스트에서 `thread_set_priority(PRI_DEFAULT - 10)`을 호출하는 것은 사실상 priority를 두 종류로 나누라는 신호에 가깝다.

이 테스트의 핵심 상황은 다음과 같다.

```text
main thread의 원래 priority: 31
high thread가 donation한 priority: 41
main이 thread_set_priority(21) 호출
```

이때 main thread가 lock을 들고 있는 동안 실제 스케줄링 priority가 21로 내려가면 안 된다. high priority thread가 main이 가진 lock을 기다리고 있기 때문이다.

따라서 팀에서 다음 두 개념을 분리해서 합의해야 한다.

```text
base priority
- thread_set_priority()로 직접 설정되는 원래 priority
- donation이 없어지면 돌아가야 하는 값

effective priority
- scheduler가 실제로 비교에 사용하는 priority
- base priority와 donation priority 중 더 높은 값
```

## 2. `effective_priority`를 구조체 멤버로 새로 추가해야 하는가?

필수는 아니다. 선택지는 두 가지다.

## 선택지 A: `priority`를 effective priority로 계속 사용하고, `base_priority`만 추가

이 방식에서는 기존 `priority` 필드를 scheduler가 실제로 쓰는 effective priority로 유지한다.

```text
priority      = effective priority
base_priority = 원래 priority
```

장점은 기존 코드 변경이 적다는 점이다. 이미 ready list 비교, semaphore waiters 비교, priority 반환 코드가 `priority`를 보고 있다면, 그 흐름을 크게 바꾸지 않아도 된다.

대신 `thread_set_priority()`는 `priority`를 바로 새 값으로 덮어쓰는 함수가 아니라, `base_priority`를 바꾸고 남아 있는 donation을 고려해 `priority`를 다시 계산하는 함수가 되어야 한다.

## 선택지 B: `base_priority`, `effective_priority`를 둘 다 명시적으로 추가

이 방식은 의미가 가장 명확하다.

```text
base_priority      = 원래 priority
effective_priority = 실제 스케줄링 priority
```

장점은 읽기 쉽고 개념이 분명하다는 점이다. 단점은 기존의 모든 priority 비교 지점이 `effective_priority`를 보도록 바뀌어야 한다는 점이다.

## 추천

현재 코드 기반에서는 선택지 A가 더 현실적이다.

```text
기존 priority 필드 = effective priority로 유지
새로 base_priority 필드 추가
```

이렇게 하면 기존 priority scheduling 코드와 덜 충돌하면서 donation을 얹을 수 있다.

## 3. donation 기록을 값만 복사해서 리스트에 넣어도 되는가?

단순히 donation priority 값만 복사해서 `donation_list`에 넣는 방식은 부족할 가능성이 높다.

처음에는 다음처럼 생각할 수 있다.

```text
내가 받은 donation 값들을 리스트에 저장
리스트를 priority 내림차순 정렬
가장 큰 donation 값을 effective priority로 사용
```

하지만 이 방식은 lock release 시점에서 문제가 생긴다.

예를 들어 현재 thread가 lock A와 lock B를 둘 다 들고 있다고 하자.

```text
lock A 때문에 priority 32 donation 받음
lock B 때문에 priority 36 donation 받음
```

이후 lock B를 release하면 priority 36 donation은 사라져야 하지만, lock A 때문에 받은 priority 32 donation은 남아 있어야 한다.

즉, donation을 관리할 때는 단순히 값만 아는 것으로는 부족하고, 최소한 다음 정보가 필요하다.

```text
이 donation이 어떤 lock 때문에 발생했는가?
이 donation을 준 thread는 누구인가?
```

값만 복사해두면 특정 lock을 release할 때 어떤 donation을 제거해야 하는지 알기 어렵다.

또한 nested donation까지 고려하면 donor thread의 effective priority 자체가 나중에 바뀔 수 있다. 이 경우 예전에 복사해둔 값은 stale한 값이 될 수 있다.

## 4. thread가 자신이 들고 있는 lock 정보를 가져야 하는가?

방향은 맞다. 다만 반드시 "thread가 held lock list를 가진다" 한 가지 방식만 가능한 것은 아니다.

필요한 핵심 정보는 다음이다.

```text
현재 thread가 어떤 lock들을 들고 있는가?
각 lock 때문에 어떤 donation들이 들어왔는가?
현재 thread가 기다리고 있는 lock은 무엇인가?
```

보통은 다음 두 방향 중 하나를 선택한다.

## 방식 A: thread가 받은 donor thread 목록을 관리

thread가 자신에게 donation한 donor thread들을 리스트로 관리한다.

이때 donor thread 쪽에는 "내가 지금 어떤 lock을 기다리는 중인지" 정보가 있어야 한다. 그래야 lock release 시점에 다음처럼 판단할 수 있다.

```text
이 donor는 내가 지금 release하는 lock 때문에 나에게 donation한 것인가?
```

이 방식은 PintOS에서 많이 쓰는 방향이다.

## 방식 B: thread가 자신이 들고 있는 lock 목록을 관리

thread가 held locks 목록을 가지고 있고, 각 lock이 자신의 waiters 중 최고 priority를 계산할 수 있게 한다.

이 방식에서는 thread의 effective priority를 계산할 때 다음처럼 생각한다.

```text
내 base priority
내가 들고 있는 lock들에 기다리는 thread들의 priority
이 중 최댓값
```

이 방식도 논리적으로 깔끔하다. 다만 lock별 max priority 갱신과 held lock 목록 관리가 필요하다.

## 추천

처음 구현에서는 방식 A가 더 단순할 가능성이 높다.

```text
thread는 donor thread 목록을 가진다.
donor thread는 자신이 기다리는 lock을 기억한다.
lock release 시, 그 lock 때문에 들어온 donation만 제거한다.
```

중요한 것은 어떤 방식을 택하든 "donation 값만 저장"하는 방식은 피하는 것이다. donation을 나중에 정확히 제거하려면 donation과 lock 사이의 관계를 추적해야 한다.

## 5. lock release에서 priority를 다시 계산해야 하는가?

그렇다. lock release 시점에는 현재 thread의 effective priority를 다시 계산해야 한다.

이유는 lock을 release하면 그 lock 때문에 들어온 donation이 사라지기 때문이다.

예를 들어 다음 상황을 보자.

```text
base priority: 31
lock A 때문에 받은 donation: 32
lock B 때문에 받은 donation: 36
현재 effective priority: 36
```

여기서 lock B를 release하면 effective priority는 31이 아니라 32가 되어야 한다.

```text
lock B donation 제거
lock A donation은 유지
새 effective priority = max(31, 32)
```

따라서 lock release 시점에는 다음 규칙이 필요하다.

```text
release하는 lock과 관련된 donation만 제거한다.
남아 있는 donation 중 가장 큰 priority를 확인한다.
base priority와 남은 donation priority 중 최댓값을 effective priority로 사용한다.
```

nested donation까지 고려하면, 이 재계산 결과가 다른 thread에게 다시 영향을 줄 수도 있다. 하지만 base 단계에서는 먼저 "release한 lock과 관련된 donation만 제거한다"는 규칙을 정확히 잡는 것이 중요하다.

## 6. 리스트 정렬은 삽입 시점인가, 선택 직전인가?

priority 값만 바꾼다고 이미 리스트에 들어간 원소의 위치가 자동으로 바뀌지는 않는다.

따라서 ready list나 semaphore waiters 안에 있는 thread가 donation으로 priority가 바뀔 수 있다면, 언젠가는 리스트 순서를 다시 맞춰야 한다.

선택지는 두 가지다.

## 선택지 A: priority가 바뀌는 순간 재삽입

priority가 바뀐 thread가 어떤 리스트 안에 있다면, 그 리스트에서 제거한 뒤 priority 변경 후 다시 정렬 삽입한다.

장점은 리스트가 항상 정렬된 상태로 유지된다는 점이다.

단점은 구현 난도가 높다.

```text
이 thread가 지금 ready list에 있는가?
semaphore waiters에 있는가?
condition waiters에 있는가?
어떤 리스트에도 없는가?
```

이 정보를 정확히 알아야 한다. 잘못된 리스트에서 제거하려고 하면 list 구조가 깨질 수 있다.

## 선택지 B: 선택 직전에 다시 정렬하거나 최댓값을 찾아 꺼내기

리스트에 넣을 때는 정렬을 해두더라도, 실제로 다음 thread를 고르는 시점에 다시 정렬하거나 최고 priority 원소를 찾아 꺼낸다.

장점은 donation으로 인해 리스트 안의 priority가 바뀌어도 선택 시점에 최신 priority를 반영할 수 있다는 점이다.

단점은 매번 O(n) 또는 정렬 비용이 든다는 점이다. 하지만 PintOS Project 1 테스트 규모에서는 보통 문제가 되지 않는다.

## 그럼 삽입 시 정렬은 의미가 없는가?

완전히 의미가 없는 것은 아니다.

삽입 시 정렬은 donation이 없는 일반 priority scheduling 상황에서 리스트를 자연스럽게 정렬 상태로 유지한다. `priority-sema`, `priority-condvar`, `priority-preempt` 같은 기본 priority 테스트에서는 삽입 시 정렬만으로도 대부분의 기대 동작을 만족할 수 있다.

다만 donation이 들어오면 리스트 안의 priority가 나중에 바뀔 수 있으므로, 삽입 시 정렬만으로는 충분하지 않다.

그래서 현실적인 선택은 다음과 같다.

```text
삽입 시에도 priority 정렬을 유지한다.
실제로 원소를 선택하기 직전에 한 번 더 최신 priority 기준을 반영한다.
```

이 방식은 약간 중복처럼 보이지만, 구현 안정성이 좋다.

## 추천

현재 팀 상황에서는 선택지 B를 추천한다.

```text
삽입은 지금처럼 priority 기준 정렬 유지
선택 직전에 다시 정렬하거나 최고 priority 원소 선택
```

이렇게 하면 Session 2 구현과 donation 구현이 충돌할 가능성이 낮고, ready list나 waiters 안에서 priority가 바뀌는 상황에도 대응할 수 있다.

## 7. nested donation 깊이

nested donation은 고려해야 한다. 다만 무제한 recursion까지 일반화할 필요는 없다.

공식 문서에서도 8단계 정도의 합리적인 제한을 언급하므로, 테스트 통과 목적이라면 donation 전달 깊이에 제한을 두는 방향이 현실적이다.

합의할 내용은 다음이다.

```text
nested donation은 지원한다.
전달 깊이는 8단계 정도로 제한한다.
제한을 둔다는 사실을 팀 전체가 공유한다.
```

이 제한은 `priority-donate-chain` 같은 테스트를 염두에 둔 것이다.

## 8. Session 2와 donation base 병렬 작업 시 합의

Session 2는 일단 target 테스트인 `priority-sema`, `priority-condvar` 통과를 목표로 진행한다.

Donation base 담당자는 별도 브랜치에서 다음을 먼저 잡는다.

```text
base priority와 effective priority 분리
lock holder에게 donation하는 모델
lock release 시 donation 제거와 priority 재계산
```

나중에 두 작업을 합칠 때 priority 비교 기준은 effective priority여야 한다.

```text
ready list 비교 기준
semaphore waiters 비교 기준
condition variable waiters 비교 기준
lock donation 비교 기준
```

이 모든 비교가 base priority가 아니라 effective priority 기준으로 맞춰져야 한다.

## 9. donation base의 1차 목표

donation base 담당자가 처음 목표로 삼기 좋은 테스트는 다음 두 개다.

```text
priority-donate-one
priority-donate-lower
```

이 두 테스트를 통해 확인할 수 있는 것은 다음이다.

```text
lock holder가 donation을 받는가?
여러 donor 중 가장 높은 priority가 적용되는가?
donation 중 thread_set_priority()가 base priority만 바꾸는가?
lock release 후 effective priority가 올바르게 복구되는가?
```

이후 확장 목표는 다음 두 개다.

```text
priority-donate-multiple
priority-donate-multiple2
```

이 두 테스트부터는 lock별 donation 제거 규칙이 중요해진다.

마지막 확장 목표는 다음이다.

```text
priority-donate-nest
priority-donate-chain
priority-donate-sema
```

이 세 테스트는 nested donation, chain donation, semaphore waiters 안에서 priority가 바뀌는 상황을 검증한다.
