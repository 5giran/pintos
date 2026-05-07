# PintOS Project 2 - User Memory / Pointer Safety Context Notes

## 배경

User Memory Access를 처음 볼 때 가장 헷갈리는 부분은 "사용자 프로그램이 넘긴 포인터를 커널이 왜 따로 검사해야 하는가"와 "1바이트 단위 검사와 page 단위 검사가 무엇이 다른가"였다.

이 문서는 구현 코드를 외우기 위한 문서가 아니라, syscall에서 user pointer가 등장하는 맥락을 잡기 위한 기록이다. 특히 `read()`와 `write()`의 방향을 커널 관점에서 다시 정리한다.

## 1. syscall은 사용자 프로그램이 커널에게 하는 부탁이다

사용자 프로그램은 파일, 화면, 키보드, 디스크를 직접 만질 수 없다. 그래서 syscall로 커널에게 부탁한다.

예를 들어 사용자 프로그램이 다음을 호출한다고 하자.

```c
write (1, "hello", 5);
```

이 말은 다음 뜻이다.

```text
커널아, 내 메모리에 있는 "hello" 5바이트를 읽어서 화면에 출력해줘.
```

반대로 다음 호출은 다르다.

```c
read (fd, buffer, 100);
```

이 말은 다음 뜻이다.

```text
커널아, 파일에서 100바이트를 읽어서 내 메모리의 buffer에 넣어줘.
```

즉 syscall 인자로 넘어온 포인터는 대부분 "사용자 프로그램의 가상 주소"다. 커널은 그 주소가 진짜로 안전한지 확인하기 전까지 믿으면 안 된다.

## 2. 왜 포인터를 믿으면 안 되는가

사용자 프로그램은 실수하거나 악의적으로 이상한 주소를 넘길 수 있다.

```text
NULL
매핑되지 않은 주소
커널 메모리 주소
처음 일부만 valid하고 뒤쪽은 invalid인 buffer
read-only page인데 커널에게 쓰라고 넘긴 buffer
```

커널이 이런 주소를 확인 없이 만지면 문제가 커진다.

```text
사용자 프로그램 하나의 잘못된 syscall
  -> 커널이 잘못된 주소 접근
  -> kernel mode page fault
  -> OS panic 또는 테스트 실패
```

Project 2의 목표는 잘못한 사용자 프로세스만 죽이는 것이다.

```text
invalid user pointer 발견
  -> 현재 user process exit(-1)
  -> 커널과 다른 프로세스는 계속 정상 동작
```

이게 User Memory Access 과제의 핵심이다.

## 3. `write()`는 커널이 user buffer를 읽는 syscall이다

이름이 `write`라서 헷갈리지만, 커널 관점에서 보면 `write(fd, buffer, size)`는 user memory를 읽는다.

```c
write (1, buffer, size);
```

사용자 관점:

```text
내 buffer 내용을 화면이나 파일에 써줘.
```

커널 관점:

```text
1. user buffer 주소에서 size 바이트를 읽는다.
2. 그 데이터를 stdout 또는 파일에 쓴다.
```

따라서 `write()`의 `buffer`는 readable이어야 한다.

```text
write(fd, buffer, size)
  buffer 검증 방향: validate_user_read(buffer, size)
```

`write-bad-ptr`, `write-boundary`, `write-normal` 계열 테스트가 이 방향을 확인한다.

## 4. `read()`는 커널이 user buffer에 쓰는 syscall이다

`read(fd, buffer, size)`는 커널이 파일이나 키보드에서 읽은 데이터를 사용자 메모리에 넣어 주는 syscall이다.

```c
read (fd, buffer, size);
```

사용자 관점:

```text
파일 내용을 읽어서 내 buffer 배열에 채워줘.
```

커널 관점:

```text
1. fd가 가리키는 파일에서 데이터를 읽는다.
2. 읽은 데이터를 user buffer 주소에 쓴다.
```

따라서 `read()`의 `buffer`는 writable이어야 한다.

```text
read(fd, buffer, size)
  buffer 검증 방향: validate_user_write(buffer, size)
```

단순히 mapped 여부만 보면 부족하다. 코드 영역처럼 읽기는 가능하지만 쓰기는 안 되는 page일 수 있기 때문이다. 그런 주소에 커널이 쓰면 kernel context에서 page fault가 날 수 있다.

`read-bad-ptr`, `read-boundary`, `read-normal` 계열 테스트가 이 방향을 확인한다.

## 5. filename과 cmdline은 user string이다

파일 이름이나 실행 명령도 포인터로 넘어온다.

```c
open ("sample.txt");
create ("quux.dat", 0);
remove ("file.txt");
exec ("child-simple arg");
fork ("child-simple");
```

사용자 프로그램 입장에서는 문자열을 넘긴 것처럼 보이지만, 커널이 실제로 받는 것은 문자열이 있는 user virtual address다.

따라서 커널은 문자열을 직접 믿으면 안 된다.

```text
1. user string 주소가 user address인지 확인한다.
2. 문자열의 각 page가 mapped인지 확인한다.
3. '\0'을 만날 때까지 안전하게 읽을 수 있는지 확인한다.
4. 확인된 문자열만 filesys_open(), filesys_create(), process_exec() 같은 커널 로직에서 사용한다.
```

따라서 커널은 user string을 사용할 때 `'\0'`을 만날 때까지 안전하게 읽을 수 있는지 확인해야 한다.

문자열도 page boundary를 걸칠 수 있다.

```text
page A 끝부분: "sample"
page B 시작부분: ".txt\0"
```

이런 경우도 valid하면 성공해야 한다. `open-boundary`, `create-bound`, `exec-boundary`, `fork-boundary` 계열 테스트가 이 상황을 만든다.

## 6. 1바이트 단위 검사는 무엇인가

메모리를 아주 긴 칸 배열로 생각하면 된다.

```text
주소 1000: 1바이트
주소 1001: 1바이트
주소 1002: 1바이트
주소 1003: 1바이트
...
```

사용자가 다음을 요청했다고 하자.

```c
write (1, buffer, 5);
```

`buffer`가 주소 1000이면 커널이 읽을 범위는 다음과 같다.

```text
1000
1001
1002
1003
1004
```

1바이트 단위 검사는 이 주소들을 하나씩 전부 확인한다.

```text
1000 주소 괜찮아?
1001 주소 괜찮아?
1002 주소 괜찮아?
1003 주소 괜찮아?
1004 주소 괜찮아?
```

장점은 직관적이라는 것이다. 단점은 느리고 중복 검사가 많다는 것이다. `size`가 10000이면 주소를 10000번 확인하게 된다.

## 7. page 단위 검사는 무엇인가

운영체제는 메모리를 보통 4096바이트 묶음으로 관리한다. 이 묶음을 page라고 한다.

```text
page 1: 주소 0 ~ 4095
page 2: 주소 4096 ~ 8191
page 3: 주소 8192 ~ 12287
```

page table도 이 page 단위로 "이 user virtual page가 실제 물리 메모리에 매핑되어 있는가"를 관리한다.

예를 들어 `buffer = 1000`, `size = 5`이면 접근 범위는 전부 page 1 안에 있다.

```text
page 1: 0 ~ 4095
        1000 ~ 1004 포함
```

그러면 page 단위 검사는 page 1만 확인한다.

```text
page 1이 user page table에 mapped 되어 있어?
필요하면 writable이야?
```

다른 예로 `buffer = 4090`, `size = 20`이면 범위가 page 1과 page 2를 걸친다.

```text
page 1: 0 ~ 4095       4090 ~ 4095 포함
page 2: 4096 ~ 8191    4096 ~ 4109 포함
```

그러면 두 page를 확인한다.

```text
page 1 괜찮아?
page 2 괜찮아?
```

page 단위 검사는 1바이트 단위 검사보다 OS 관점에 더 자연스럽고 효율적이다. 대신 buffer가 걸치는 page를 정확히 계산해야 한다.

## 8. 시작 주소와 마지막 주소만 검사하면 왜 부족한가

buffer가 여러 page를 걸칠 수 있다.

```text
buffer 범위:

[page A] [page B] [page C]
   ^                 ^
 시작 주소          마지막 주소
```

상태가 다음과 같다고 하자.

```text
page A: valid
page B: invalid
page C: valid
```

시작 주소와 마지막 주소만 검사하면 page A와 page C만 확인한다.

```text
시작 주소 valid
마지막 주소 valid
통과
```

하지만 실제로 복사하다가 중간 page B에 닿으면 page fault가 난다.

따라서 page 단위 검사를 하더라도 buffer가 지나가는 모든 page를 확인해야 한다.

```text
page A 검사
page B 검사
page C 검사
```

이 차이가 현재 `syscall.c`의 단순한 `check_address(buffer)`와 `check_address(buffer + size - 1)` 방식이 부족한 이유다.

## 9. 한 줄 정리

User Memory Access는 "사용자 포인터를 커널이 믿지 않도록 만드는 작업"이다.

```text
write()
  user buffer를 커널이 읽는다
  readable 검사

read()
  커널이 user buffer에 쓴다
  writable 검사

open/create/remove/exec/fork()
  user string을 커널이 읽는다
  '\0'까지 readable 검사

검사 단위
  1바이트 단위도 가능하지만 느리다
  page 단위가 더 자연스럽다
  단, 시작/끝만이 아니라 걸치는 모든 page를 확인해야 한다
```
