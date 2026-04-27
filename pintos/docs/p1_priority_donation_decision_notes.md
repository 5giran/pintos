# PintOS Project 1 - Priority Donation 결정 사항 정리

## 1. Priority 필드 설계

`base_priority`와 `effective_priority`를 둘 다 명시적으로 관리하는 방향으로 결정한다.

```text
base_priority
- thread_set_priority()로 직접 설정되는 원래 priority
- donation이 모두 사라졌을 때 돌아가야 하는 값

effective_priority
- scheduler가 실제로 비교에 사용하는 priority
- base_priority와 donation으로 받은 priority 중 가장 높은 값
```

이 방식은 기존 `priority` 필드를 effective priority처럼 계속 사용하는 방식보다 수정 범위는 조금 늘어난다. 하지만 donation 구현에서는 "원래 priority"와 "실제로 적용되는 priority"를 계속 구분해야 하므로, 두 값을 명시적으로 분리하는 편이 이후 구현과 디버깅에 더 명확하다.

따라서 priority 비교가 필요한 모든 곳에서는 base priority가 아니라 effective priority를 기준으로 판단해야 한다.

```text
ready list 비교 기준
semaphore waiters 비교 기준
condition variable waiters 비교 기준
lock donation 비교 기준
```

위 비교들은 모두 effective priority를 기준으로 맞춘다.

## 2. Donation 기록 방식

thread가 자신에게 donation한 donor thread 목록을 관리하는 방식으로 결정한다.

이 방식에서는 donation을 받는 thread가 donor thread들을 리스트로 가지고, donor thread는 자신이 현재 어떤 lock을 기다리고 있는지 기억해야 한다.

```text
donation을 받는 thread
- 자신에게 donation한 thread 목록을 관리

donation을 하는 thread
- 자신이 기다리는 lock을 기록
```

이 구조를 사용하면 lock release 시점에 다음 질문에 답할 수 있다.

```text
이 donor는 지금 release하는 lock 때문에 나에게 donation한 것인가?
```

즉, lock을 release할 때 해당 lock 때문에 들어온 donation만 제거하고, 다른 lock 때문에 들어온 donation은 유지할 수 있다.

이 방식은 단순히 donation priority 값만 복사해서 저장하는 방식보다 안전하다. donation 값만 저장하면 어떤 lock 때문에 발생한 donation인지 추적하기 어렵고, 특정 lock을 release할 때 어떤 donation을 제거해야 하는지 판단하기 어렵다.

## 3. 리스트 정렬 정책

선택 직전에 다시 정렬하거나, 선택 직전에 가장 높은 effective priority 원소를 찾아 꺼내는 방식으로 결정한다.

priority 값은 thread가 ready list나 semaphore waiters에 들어간 뒤에도 donation 때문에 바뀔 수 있다. PintOS의 list는 단순 연결 리스트이므로, 원소 안의 priority 값이 바뀌어도 리스트 순서는 자동으로 바뀌지 않는다.

따라서 삽입 시점에 priority 기준으로 정렬해두더라도, donation 이후에는 리스트 순서가 stale해질 수 있다.

결정한 방향은 다음과 같다.

```text
삽입 시에는 기존처럼 priority 기준 정렬을 유지한다.
하지만 실제로 원소를 선택하는 시점에 최신 effective priority 기준을 다시 반영한다.
```

이 방식은 다음 상황에 대응하기 좋다.

```text
ready list 안에 있는 thread가 donation을 받아 priority가 바뀐 경우
semaphore waiters 안에 있는 thread가 donation을 받아 priority가 바뀐 경우
condition variable waiters 안의 대기 대상 priority가 바뀐 경우
```

즉, "리스트에 넣을 때 정렬했으니 항상 정렬되어 있다"고 가정하지 않는다. 실제로 다음 thread를 고르는 시점에 최신 effective priority가 반영되어 있는지 확인하는 방향으로 구현한다.

## 4. Nested Donation 깊이

nested donation은 지원한다. 다만 무제한으로 일반화하지 않고, 테스트 통과에 충분한 합리적인 깊이 제한을 둔다.

공식 문서에서도 8단계 정도의 합리적인 제한을 언급하므로, donation 전달 깊이는 8단계 정도로 제한하는 방향으로 결정한다.

```text
nested donation은 지원한다.
전달 깊이는 8단계 정도로 제한한다.
제한을 둔다는 사실을 팀 전체가 공유한다.
```

이 결정은 `priority-donate-nest`와 `priority-donate-chain`을 염두에 둔 것이다.

핵심은 높은 priority thread가 기다리는 lock의 holder에게 donation하고, 그 holder가 다시 다른 lock을 기다리고 있다면 donation이 다음 holder에게도 이어져야 한다는 점이다.
