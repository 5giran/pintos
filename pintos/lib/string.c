#include <string.h>
#include <debug.h>

/* 겹치면 안 되는 SRC의 SIZE바이트를 DST로 복사합니다.
   DST를 반환합니다. */
void *
memcpy (void *dst_, const void *src_, size_t size) {
	unsigned char *dst = dst_;
	const unsigned char *src = src_;

	ASSERT (dst != NULL || size == 0);
	ASSERT (src != NULL || size == 0);

	while (size-- > 0)
		*dst++ = *src++;

	return dst_;
}

/* 서로 겹칠 수 있는 SRC의 SIZE바이트를 DST로 복사합니다.
   DST를 반환합니다. */
void *
memmove (void *dst_, const void *src_, size_t size) {
	unsigned char *dst = dst_;
	const unsigned char *src = src_;

	ASSERT (dst != NULL || size == 0);
	ASSERT (src != NULL || size == 0);

	if (dst < src) {
		while (size-- > 0)
			*dst++ = *src++;
	} else {
		dst += size;
		src += size;
		while (size-- > 0)
			*--dst = *--src;
	}

	return dst;
}

/* A와 B에 있는 SIZE바이트 블록에서 첫 번째로 다른 바이트를
   찾습니다. A의 바이트가 더 크면 양수, B의 바이트가 더 크면 음수,
   두 블록 A와 B가 같으면 0을 반환합니다. */
int
memcmp (const void *a_, const void *b_, size_t size) {
	const unsigned char *a = a_;
	const unsigned char *b = b_;

	ASSERT (a != NULL || size == 0);
	ASSERT (b != NULL || size == 0);

	for (; size-- > 0; a++, b++)
		if (*a != *b)
			return *a > *b ? +1 : -1;
	return 0;
}

/* 문자열 A와 B에서 처음으로 다른 문자를 찾습니다. A의 문자(unsigned
   char로 본 값)가 더 크면 양수, B의 문자(unsigned char로 본 값)가
   더 크면 음수, 문자열 A와 B가 같으면 0을 반환합니다. */
int
strcmp (const char *a_, const char *b_) {
	const unsigned char *a = (const unsigned char *) a_;
	const unsigned char *b = (const unsigned char *) b_;

	ASSERT (a != NULL);
	ASSERT (b != NULL);

	while (*a != '\0' && *a == *b) {
		a++;
		b++;
	}

	return *a < *b ? -1 : *a > *b;
}

/* BLOCK에서 시작하는 처음 SIZE바이트 안에서 CH가 처음 나타나는
   위치를 가리키는 포인터를 반환합니다. CH가 BLOCK에 나타나지 않으면
   null 포인터를 반환합니다. */
void *
memchr (const void *block_, int ch_, size_t size) {
	const unsigned char *block = block_;
	unsigned char ch = ch_;

	ASSERT (block != NULL || size == 0);

	for (; size-- > 0; block++)
		if (*block == ch)
			return (void *) block;

	return NULL;
}

/* STRING에서 C가 처음 나타나는 위치를 찾아 반환하고, C가 STRING에
   나타나지 않으면 null 포인터를 반환합니다. C == '\0'이면 STRING
   끝의 null 종료 문자를 가리키는 포인터를 반환합니다. */
char *
strchr (const char *string, int c_) {
	char c = c_;

	ASSERT (string);

	for (;;)
		if (*string == c)
			return (char *) string;
		else if (*string == '\0')
			return NULL;
		else
			string++;
}

/* STRING의 초기 부분 문자열 길이를 반환합니다. 이 부분 문자열은
   STOP에 없는 문자들로만 이루어집니다. */
size_t
strcspn (const char *string, const char *stop) {
	size_t length;

	for (length = 0; string[length] != '\0'; length++)
		if (strchr (stop, string[length]) != NULL)
			break;
	return length;
}

/* STRING에서 STOP에도 포함된 첫 번째 문자를 가리키는 포인터를
   반환합니다. STRING의 어떤 문자도 STOP에 없으면 null 포인터를
   반환합니다. */
char *
strpbrk (const char *string, const char *stop) {
	for (; *string != '\0'; string++)
		if (strchr (stop, *string) != NULL)
			return (char *) string;
	return NULL;
}

/* STRING에서 C가 마지막으로 나타나는 위치를 가리키는 포인터를
   반환합니다. C가 STRING에 나타나지 않으면 null 포인터를 반환합니다. */
char *
strrchr (const char *string, int c_) {
	char c = c_;
	const char *p = NULL;

	for (; *string != '\0'; string++)
		if (*string == c)
			p = string;
	return (char *) p;
}

/* STRING의 초기 부분 문자열 길이를 반환합니다. 이 부분 문자열은
   SKIP에 포함된 문자들로만 이루어집니다. */
size_t
strspn (const char *string, const char *skip) {
	size_t length;

	for (length = 0; string[length] != '\0'; length++)
		if (strchr (skip, string[length]) == NULL)
			break;
	return length;
}

/* HAYSTACK 안에서 NEEDLE이 처음 나타나는 위치를 가리키는 포인터를
   반환합니다. NEEDLE이 HAYSTACK 안에 없으면 null 포인터를 반환합니다. */
char *
strstr (const char *haystack, const char *needle) {
	size_t haystack_len = strlen (haystack);
	size_t needle_len = strlen (needle);

	if (haystack_len >= needle_len) {
		size_t i;

		for (i = 0; i <= haystack_len - needle_len; i++)
			if (!memcmp (haystack + i, needle, needle_len))
				return (char *) haystack + i;
	}

	return NULL;
}

/* DELIMITERS로 구분된 토큰으로 문자열을 나눕니다. 이 함수가 처음
 * 호출될 때는 S가 토큰화할 문자열이어야 하며, 이후 호출에서는 null
 * 포인터여야 합니다. SAVE_PTR는 토크나이저의 위치를 추적하는 데
 * 사용되는 `char *' 변수의 주소입니다. 매번 반환값은 문자열의 다음
 * 토큰이며, 더 이상 토큰이 없으면 null 포인터입니다.
 *
 * 이 함수는 서로 인접한 여러 구분자를 하나의 구분자로 취급합니다.
 * 반환되는 토큰의 길이는 절대 0이 되지 않습니다. DELIMITERS는 같은
 * 문자열 안에서도 호출마다 바뀔 수 있습니다.
 *
 * strtok_r()는 문자열 S를 수정하여 구분자를 null 바이트로 바꿉니다.
 * 따라서 S는 수정 가능한 문자열이어야 합니다. 특히 문자열 리터럴은 C에서
 * *수정할 수 없으며*, 하위 호환성 때문에 `const'가 아니더라도
 * 마찬가지입니다.
 *
 * 사용 예시:
 *
 * char s[] = "  String to  tokenize. ";
 * char *token, *save_ptr;
 *
 * for (token = strtok_r (s, " ", &save_ptr); token != NULL;
 * token = strtok_r (NULL, " ", &save_ptr))
 * printf ("'%s'\n", token);
 *
 * 출력:
 *
 * 'String'
 * 'to'
 * 'tokenize.' */
char *
strtok_r (char *s, const char *delimiters, char **save_ptr) {
	char *token;

	ASSERT (delimiters != NULL);
	ASSERT (save_ptr != NULL);

	/* S가 null이 아니면 거기서 시작합니다.
	   S가 null이면 저장된 위치에서 시작합니다. */
	if (s == NULL)
		s = *save_ptr;
	ASSERT (s != NULL);

	/* 현재 위치의 DELIMITERS를 모두 건너뜁니다. */
	while (strchr (delimiters, *s) != NULL) {
		/* null 바이트를 찾는 경우라면 strchr()는 항상 null이 아닌 값을
		   반환합니다. 모든 문자열에는 끝에 null 바이트가 있기
		   때문입니다. */
		if (*s == '\0') {
			*save_ptr = s;
			return NULL;
		}

		s++;
	}

	/* 문자열 끝까지 DELIMITERS가 아닌 문자들을 모두 건너뜁니다. */
	token = s;
	while (strchr (delimiters, *s) == NULL)
		s++;
	if (*s != '\0') {
		*s = '\0';
		*save_ptr = s + 1;
	} else
		*save_ptr = s;
	return token;
}

/* DST의 SIZE바이트를 VALUE로 설정합니다. */
void *
memset (void *dst_, int value, size_t size) {
	unsigned char *dst = dst_;

	ASSERT (dst != NULL || size == 0);

	while (size-- > 0)
		*dst++ = value;

	return dst_;
}

/* STRING의 길이를 반환합니다. */
size_t
strlen (const char *string) {
	const char *p;

	ASSERT (string);

	for (p = string; *p != '\0'; p++)
		continue;
	return p - string;
}

/* STRING의 길이가 MAXLEN 문자보다 짧으면 실제 길이를 반환합니다.
   그렇지 않으면 MAXLEN을 반환합니다. */
size_t
strnlen (const char *string, size_t maxlen) {
	size_t length;

	for (length = 0; string[length] != '\0' && length < maxlen; length++)
		continue;
	return length;
}

/* 문자열 SRC를 DST에 복사합니다. SRC가 SIZE - 1문자보다 길면
   SIZE - 1문자만 복사합니다. SIZE가 0이 아닌 한 DST에는 항상 null
   종료 문자가 기록됩니다. 반환값은 null 종료 문자를 제외한 SRC의
   길이입니다.

   strlcpy()는 표준 C 라이브러리에 없지만 점점 더 널리 쓰이는
   확장입니다. 자세한 내용은
   http://www.courtesan.com/todd/papers/strlcpy.html을 참조하십시오. */
size_t
strlcpy (char *dst, const char *src, size_t size) {
	size_t src_len;

	ASSERT (dst != NULL);
	ASSERT (src != NULL);

	src_len = strlen (src);
	if (size > 0) {
		size_t dst_len = size - 1;
		if (src_len < dst_len)
			dst_len = src_len;
		memcpy (dst, src, dst_len);
		dst[dst_len] = '\0';
	}
	return src_len;
}

/* SRC 문자열을 DST에 이어붙입니다. 이어붙인 문자열은 SIZE - 1문자로
   제한됩니다. SIZE가 0이 아닌 한 DST에는 항상 null 종료 문자가
   기록됩니다. 반환값은 충분한 공간이 있다고 가정했을 때 이어붙인
   문자열의 길이이며, null 종료 문자는 제외합니다.

   strlcat()는 표준 C 라이브러리에 없지만 점점 더 널리 쓰이는
   확장입니다. 자세한 내용은
   http://www.courtesan.com/todd/papers/strlcpy.html을 참조하십시오. */
size_t
strlcat (char *dst, const char *src, size_t size) {
	size_t src_len, dst_len;

	ASSERT (dst != NULL);
	ASSERT (src != NULL);

	src_len = strlen (src);
	dst_len = strlen (dst);
	if (size > 0 && dst_len < size) {
		size_t copy_cnt = size - dst_len - 1;
		if (src_len < copy_cnt)
			copy_cnt = src_len;
		memcpy (dst + dst_len, src, copy_cnt);
		dst[dst_len + copy_cnt] = '\0';
	}
	return src_len + dst_len;
}

