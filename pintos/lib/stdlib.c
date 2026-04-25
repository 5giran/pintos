#include <ctype.h>
#include <debug.h>
#include <random.h>
#include <stdlib.h>
#include <stdbool.h>

/* S에 있는 부호 있는 10진 정수의 문자열 표현을
   `int`로 변환하여 반환합니다. */
int
atoi (const char *s) 
{
  bool negative;
  int value;

  ASSERT (s != NULL);

  /* 공백을 건너뜁니다. */
  while (isspace ((unsigned char) *s))
    s++;

  /* 부호를 파싱합니다. */
  negative = false;
  if (*s == '+')
    s++;
  else if (*s == '-')
    {
      negative = true;
      s++;
    }

  /* 숫자를 파싱합니다. 값은 항상 처음에는 음수로 파싱한 다음,
     나중에 양수로 바꾸는데, 이는 2의 보수 시스템에서 `int`의
     음수 범위가 양수 범위보다 더 크기 때문입니다. */
  for (value = 0; isdigit (*s); s++)
    value = value * 10 - (*s - '0');
  if (!negative)
    value = -value;

  return value;
}

/* AUX 함수를 호출하여 A와 B를 비교합니다. */
static int
compare_thunk (const void *a, const void *b, void *aux) 
{
  int (**compare) (const void *, const void *) = aux;
  return (*compare) (a, b);
}

/* SIZE바이트짜리 요소 CNT개를 담고 있는 ARRAY를 COMPARE를 사용해
   정렬합니다. COMPARE에 요소 A와 B의 쌍이 차례로 전달되면
   strcmp()-형식의 결과를 반환해야 합니다. 즉 A < B이면 0보다
   작은 값, A == B이면 0, A > B이면 0보다 큰 값이어야 합니다.
   CNT에 대해 O(n lg n) 시간과 O(1) 공간으로 동작합니다. */
void
qsort (void *array, size_t cnt, size_t size,
       int (*compare) (const void *, const void *)) 
{
  sort (array, cnt, size, compare_thunk, &compare);
}

/* SIZE바이트짜리 요소로 이루어진 ARRAY에서 1부터 시작하는
   인덱스 A_IDX와 B_IDX의 요소를 서로 바꿉니다. */
static void
do_swap (unsigned char *array, size_t a_idx, size_t b_idx, size_t size)
{
  unsigned char *a = array + (a_idx - 1) * size;
  unsigned char *b = array + (b_idx - 1) * size;
  size_t i;

  for (i = 0; i < size; i++)
    {
      unsigned char t = a[i];
      a[i] = b[i];
      b[i] = t;
    }
}

/* SIZE바이트짜리 요소로 이루어진 ARRAY에서 1부터 시작하는
   인덱스 A_IDX와 B_IDX의 요소를 COMPARE를 사용해 비교하고,
   AUX를 보조 데이터로 전달하여 strcmp()-형식의 결과를 반환합니다. */
static int
do_compare (unsigned char *array, size_t a_idx, size_t b_idx, size_t size,
            int (*compare) (const void *, const void *, void *aux),
            void *aux) 
{
  return compare (array + (a_idx - 1) * size, array + (b_idx - 1) * size, aux);
}

/* CNT개의 SIZE바이트짜리 요소로 이루어진 ARRAY에서 1부터 시작하는
   인덱스 I의 요소를 아래로 내립니다. 요소 비교에는 COMPARE를
   사용하고, AUX를 보조 데이터로 전달합니다. */
static void
heapify (unsigned char *array, size_t i, size_t cnt, size_t size,
         int (*compare) (const void *, const void *, void *aux),
         void *aux) 
{
  for (;;) 
    {
      /* I와 그 자식들(있다면) 중 가장 큰 요소의 인덱스를 `max`로
         설정합니다. */
      size_t left = 2 * i;
      size_t right = 2 * i + 1;
      size_t max = i;
      if (left <= cnt && do_compare (array, left, max, size, compare, aux) > 0)
        max = left;
      if (right <= cnt
          && do_compare (array, right, max, size, compare, aux) > 0) 
        max = right;

      /* 최대값이 이미 요소 I에 있으면 끝입니다. */
      if (max == i)
        break;

      /* 서로 바꾸고 힙 아래로 계속 내려갑니다. */
      do_swap (array, i, max, size);
      i = max;
    }
}

/* SIZE바이트짜리 요소 CNT개를 담고 있는 ARRAY를 AUX를 보조
   데이터로 전달하여 COMPARE를 사용해 정렬합니다. COMPARE에 요소 A와
   B의 쌍이 차례로 전달되면 strcmp()-형식의 결과를 반환해야 합니다.
   즉 A < B이면 0보다 작은 값, A == B이면 0, A > B이면 0보다 큰
   값이어야 합니다. CNT에 대해 O(n lg n) 시간과 O(1) 공간으로
   동작합니다. */
void
sort (void *array, size_t cnt, size_t size,
      int (*compare) (const void *, const void *, void *aux),
      void *aux) 
{
  size_t i;

  ASSERT (array != NULL || cnt == 0);
  ASSERT (compare != NULL);
  ASSERT (size > 0);

  /* 힙을 구성합니다. */
  for (i = cnt / 2; i > 0; i--)
    heapify (array, i, cnt, size, compare, aux);

  /* 힙을 정렬합니다. */
  for (i = cnt; i > 1; i--) 
    {
      do_swap (array, 1, i, size);
      heapify (array, 1, i - 1, size, compare, aux); 
    }
}

/* SIZE바이트짜리 요소 CNT개를 담고 있는 ARRAY에서 주어진 KEY를
   검색합니다. 일치 항목을 찾으면 반환하고, 없으면 null 포인터를
   반환합니다. 일치 항목이 여러 개이면 그중 임의의 하나를 반환합니다.

   ARRAY는 COMPARE에 따라 정렬되어 있어야 합니다.

   요소 비교에는 COMPARE를 사용합니다. COMPARE에 요소 A와 B의 쌍이
   차례로 전달되면 strcmp()-형식의 결과를 반환해야 합니다. 즉 A < B이면
   0보다 작은 값, A == B이면 0, A > B이면 0보다 큰 값이어야 합니다. */
void *
bsearch (const void *key, const void *array, size_t cnt,
         size_t size, int (*compare) (const void *, const void *)) 
{
  return binary_search (key, array, cnt, size, compare_thunk, &compare);
}

/* SIZE바이트짜리 요소 CNT개를 담고 있는 ARRAY에서 주어진 KEY를
   검색합니다. 일치 항목을 찾으면 반환하고, 없으면 null 포인터를
   반환합니다. 일치 항목이 여러 개이면 그중 임의의 하나를 반환합니다.

   ARRAY는 COMPARE에 따라 정렬되어 있어야 합니다.

   요소 비교에는 COMPARE를 사용하고, AUX를 보조 데이터로 전달합니다.
   COMPARE에 요소 A와 B의 쌍이 차례로 전달되면 strcmp()-형식의 결과를
   반환해야 합니다. 즉 A < B이면 0보다 작은 값, A == B이면 0, A > B이면
   0보다 큰 값이어야 합니다. */
void *
binary_search (const void *key, const void *array, size_t cnt, size_t size,
               int (*compare) (const void *, const void *, void *aux),
               void *aux) 
{
  const unsigned char *first = array;
  const unsigned char *last = array + size * cnt;

  while (first < last) 
    {
      size_t range = (last - first) / size;
      const unsigned char *middle = first + (range / 2) * size;
      int cmp = compare (key, middle, aux);

      if (cmp < 0) 
        last = middle;
      else if (cmp > 0) 
        first = middle + size;
      else
        return (void *) middle;
    }
  
  return NULL;
}

