# PintOS Project 1 - Priority Donation Debug Notes

## 배경

Priority Donation을 구현하면서 가장 많이 헷갈린 부분은 "donation 관계를 어떤 리스트로 추적하며, lock release 때 무엇을 지워야 하는가"였다. 특히 PintOS의 intrusive list 구조 때문에 `thread.elem`과 `donation_elem`을 구분해야 했고, nested donation에서는 donation list의 정렬 상태가 시간이 지나면서 깨질 수 있다는 점도 디버깅 과정에서 확인했다.

이 문서는 구현 중 질문이 반복되었던 지점과, 실제로 잘못 구현했다가 고친 문제를 정리한다. 코드의 정답을 외우기 위한 문서가 아니라, 왜 그런 구조가 필요한지 다시 확인하기 위한 기록이다.

## 1. `donation_elem`이 따로 필요한 이유

처음에는 holder의 `donations` 리스트에 donor thread를 넣는 용도라면 기존 `thread.elem`을 그대로 써도 되는지 의문이 있었다. 어차피 donations 리스트는 "누가 나에게 donation했는지"를 판단하는 용도이기 때문이다.

하지만 PintOS의 list는 intrusive list다. `struct list_elem` 자체가 `prev`, `next` 포인터를 가지고 있고, 리스트에 삽입될 때 이 포인터들이 해당 리스트 기준으로 설정된다.

```c
struct list_elem {
	struct list_elem *prev;
	struct list_elem *next;
};
```

같은 `list_elem`을 두 리스트에 동시에 넣으면 두 번째 삽입에서 `prev`, `next`가 새 리스트 기준으로 덮어써진다. 그러면 첫 번째 리스트의 연결이 깨진다. 이것은 단순한 스타일 문제가 아니라 실제 메모리 연결 구조가 망가지는 문제다.

Donation에서는 lock을 기다리는 donor thread가 두 관계에 동시에 속할 수 있다.

```text
lock 내부 semaphore waiters:
  donor.thread.elem

holder의 donations 리스트:
  donor.thread.donation_elem
```

그래서 `thread.elem`과 별개로 `donation_elem`이 필요하다. condition variable에서 `condition.waiters`에 `semaphore_elem.elem`을 넣고, 실제 thread는 개인 semaphore의 waiters에 `thread.elem`으로 들어가는 구조와 같은 이유다.

## 2. `refresh_priority()`에서 base를 언제 대입해야 하는가

`refresh_priority()`를 구현하면서 priority를 다시 계산하는 순서 때문에 테스트가 깨졌다. donation을 먼저 반영한 뒤 마지막에 `base_priority`를 대입하면, 앞에서 계산한 donation priority를 스스로 덮어쓰게 된다.

문제 흐름은 다음과 같다.

```text
1. donor priority 41을 확인한다.
2. holder priority를 41로 올린다.
3. 함수 끝에서 holder priority를 base_priority 31로 다시 덮어쓴다.
4. donation이 적용되지 않은 것처럼 보인다.
```

따라서 올바른 계산 흐름은 항상 base에서 시작해야 한다.

```text
1. priority = base_priority
2. 남아 있는 donations를 확인한다.
3. donation priority 중 더 높은 값이 있으면 priority에 반영한다.
```

이 순서가 지켜져야 `priority-donate-lower`도 자연스럽게 설명된다. donation을 받은 상태에서 base priority를 낮춰도, 남아 있는 donation이 더 높으면 effective priority는 donation 값으로 유지된다.

## 3. `priority-donate-multiple`에서 priority가 너무 빨리 내려간 이유

`priority-donate-multiple`에서는 main thread가 두 개의 lock을 가지고 있고, 서로 다른 priority의 thread들이 각각 다른 lock을 기다린다. 이때 더 높은 donor가 기다리던 lock을 release하면, 그 donation은 제거되어야 하지만 다른 lock 때문에 들어온 donation은 남아 있어야 한다.

실패한 결과는 다음 흐름을 보여줬다.

```text
기대: lock b release 후 main priority = 32
실제: lock b release 후 main priority = 31
```

이 결과는 lock b 관련 donation을 제거하면서 lock a 관련 donation까지 함께 사라졌거나, 남아 있는 donation을 다시 반영하지 못했다는 뜻이다.

핵심 규칙은 다음과 같다.

```text
lock release 때 모든 donation을 지우면 안 된다.
donor->wait_lock == release하는 lock인 donation만 지워야 한다.
그 뒤 남은 donations와 base_priority로 effective priority를 다시 계산해야 한다.
```

이 규칙을 지키면 lock b를 release한 뒤에도 lock a를 기다리는 donor의 priority는 남아 있고, main thread의 effective priority는 base가 아니라 남은 donation 값으로 유지된다.

## 4. 리스트 순회 중 삭제할 때 `next`를 먼저 저장해야 하는 이유

`remove_donation()`은 donations 리스트를 순회하면서 특정 lock과 관련된 donor를 제거한다. 이때 현재 원소를 `list_remove(e)`로 제거한 뒤 `list_next(e)`를 호출하면 위험하다.

`list_remove()`는 해당 element를 리스트에서 떼어내면서 주변 element들의 연결을 바꾼다. 제거된 element를 기준으로 다음 element를 찾으려 하면 이미 리스트 연결에서 빠진 노드를 기준으로 이동하게 된다.

그래서 삭제 전에 다음 원소를 먼저 저장해야 한다.

```c
struct list_elem *next = list_next(e);

if (donor->wait_lock == lock) {
	list_remove(e);
}
e = next;
```

이 방식은 같은 lock을 기다리는 donor가 여러 명 있어도 끝까지 순회할 수 있게 해준다. 중간에서 삭제 후 바로 `break`하면 같은 lock 때문에 들어온 donation이 일부 남아 stale donation이 될 수 있다.

## 5. donations 리스트의 front를 항상 믿으면 안 되는 이유

처음에는 `donations` 리스트를 삽입 시점에 priority 순으로 넣으면, 이후에는 front만 보면 가장 높은 donor를 알 수 있다고 생각할 수 있다. 하지만 nested donation이 들어가면 donor thread의 effective priority가 삽입 이후에도 바뀔 수 있다.

예를 들어 donor A가 holder H의 donations 리스트에 들어갈 때는 priority 32였다고 하자. 이후 donor A가 다른 높은 priority thread에게 nested donation을 받아 effective priority가 45로 올라갈 수 있다. 그러면 H의 donations 리스트 안에서 A의 위치는 삽입 당시 기준으로는 맞지만, 현재 priority 기준으로는 더 이상 맞지 않을 수 있다.

이 상태에서 `list_front()`만 믿으면 실제 최고 priority donor가 아닌 thread를 기준으로 holder priority를 계산할 위험이 있다.

현재 구현에서는 `refresh_priority()` 안에서 front를 보기 전에 다시 정렬한다.

```c
list_sort(&t->donations, donor_priority_compare, NULL);
```

다른 방법으로는 매번 donations 전체를 순회하면서 최댓값을 찾는 방식도 가능하다. 중요한 것은 "삽입 당시 정렬 상태가 현재도 유효하다"는 가정을 하지 않는 것이다.

## 6. `thread_set_priority()`가 donation chain을 다시 전파해야 하는가

구현 중 한 가지 의문은 현재 thread가 이미 다른 thread에게 donation을 주고 있는 상태에서 `thread_set_priority()`를 호출하면, 그 변경이 receiver에게도 전파되어야 하지 않느냐는 것이었다.

여기서 구분해야 할 점은 `thread_set_priority()`를 호출할 수 있는 thread는 현재 실행 중인 thread라는 점이다. 일반적으로 lock을 기다리며 blocked 상태인 thread는 CPU를 잡고 있지 않으므로 그 상태에서 직접 `thread_set_priority()`를 호출할 수 없다.

따라서 "wait_lock이 있는 blocked donor가 갑자기 set_priority를 호출해 holder에게 준 donation 값이 바뀐다"는 흐름은 일반적인 실행 경로에서는 발생하지 않는다. donation 전파는 lock을 얻으려다 block되기 직전의 `lock_acquire()` 흐름에서 일어난다.

다만 이 질문은 중요한 약점을 드러냈다. donation list 안의 donor priority가 삽입 이후 바뀔 수 있다는 점이다. 이 문제는 `thread_set_priority()` 자체보다 nested donation 전파에서 실제로 나타날 수 있고, 그래서 `refresh_priority()`가 donations 리스트를 다시 정렬하거나 전체 최댓값을 계산해야 한다.

## 7. nested donation에서 위쪽 holder의 donations 리스트에는 왜 donor를 다시 넣지 않는가

리팩토링 과정에서 chain 전파를 `donate_priority()` 안으로 옮기면서, 위쪽 holder의 `donations` 리스트에도 최초 donor를 넣어야 하는지 고민했다.

현재 구현에서는 직접 lock을 기다리는 관계에 대해서만 donations 리스트에 donor를 넣는다. 예를 들어 A가 B의 lock을 기다리고, B가 C의 lock을 기다리는 상황이면 B의 donations에는 A가 들어간다. C의 donations에는 C의 lock을 직접 기다리는 B가 이미 들어가 있어야 한다.

이 상태에서 A의 priority가 B에게 반영되고, B의 effective priority가 다시 C에게 전파된다. 즉 C는 "A가 직접 나에게 donation했다"가 아니라 "나를 기다리는 B의 effective priority가 올라갔다"는 형태로 영향을 받는다.

이 방식은 같은 `donation_elem`을 여러 리스트에 넣지 않는다는 제약과도 맞다. A의 `donation_elem`을 B의 donations와 C의 donations에 동시에 넣으면 intrusive list 구조가 깨진다.

따라서 chain 위쪽에서는 직접 donation 관계를 새로 기록하기보다, 이미 존재하는 직접 대기 관계를 기준으로 effective priority 값을 전파하는 것이 자연스럽다.

## 8. 디버깅하면서 확정한 핵심 규칙

이번 donation 구현에서 최종적으로 확정한 규칙은 다음과 같다.

```text
donation 관계는 holder의 donations 리스트에 donor thread로 기록한다.
donations 리스트에는 thread.elem이 아니라 donation_elem을 넣는다.
lock release 때는 donor->wait_lock == lock인 donation만 제거한다.
리스트 순회 중 원소를 제거할 때는 삭제 전에 next를 먼저 저장한다.
refresh_priority()는 base_priority에서 시작해 남은 donation 중 최댓값을 반영한다.
donations 리스트의 삽입 당시 정렬 상태를 영구적으로 믿지 않는다.
nested donation의 위쪽 holder에는 직접 donor를 중복 삽입하지 않고 effective priority만 전파한다.
MLFQS에서는 priority donation을 적용하지 않는다.
```

이 규칙들이 함께 유지되어야 donation one, multiple, lower, nest, chain 계열 테스트를 같은 구조 안에서 설명할 수 있다.
