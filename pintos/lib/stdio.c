#include <stdio.h>
#include <ctype.h>
#include <inttypes.h>
#include <round.h>
#include <stdint.h>
#include <string.h>

/* vsnprintf_helper()용 보조 데이터. */
struct vsnprintf_aux {
	char *p;            /* 현재 출력 위치. */
	int length;         /* 출력 문자열 길이. */
	int max_length;     /* 출력 문자열의 최대 길이. */
};

static void vsnprintf_helper (char, void *);

/* vprintf()와 같지만, 출력이 BUFFER에 저장된다는 점이 다릅니다. BUFFER는 BUF_SIZE 문자만큼의 공간이 있어야
   합니다. BUFFER에는 최대 BUF_SIZE - 1자만 쓰고, 뒤에 null 종료자를 붙입니다. BUF_SIZE가 0이 아닌 한
   BUFFER는 항상 null 종료됩니다. 충분한 공간이 있었다면 BUFFER에 쓰였을 문자 수를 반환하며, null 종료자는 포함하지
   않습니다. */
int
vsnprintf (char *buffer, size_t buf_size, const char *format, va_list args) {
	/* vsnprintf_helper()용 보조 데이터를 설정합니다. */
	struct vsnprintf_aux aux;
	aux.p = buffer;
	aux.length = 0;
	aux.max_length = buf_size > 0 ? buf_size - 1 : 0;

	/* 대부분의 작업을 수행합니다. */
	__vprintf (format, args, vsnprintf_helper, &aux);

	/* null 종료자를 추가합니다. */
	if (buf_size > 0)
		*aux.p = '\0';

	return aux.length;
}

/* vsnprintf()용 헬퍼 함수. */
static void
vsnprintf_helper (char ch, void *aux_) {
	struct vsnprintf_aux *aux = aux_;

	if (aux->length++ < aux->max_length)
		*aux->p++ = ch;
}

/* printf()와 같지만, 출력이 BUFFER에 저장된다는 점이 다릅니다. BUFFER는 BUF_SIZE 문자만큼의 공간이 있어야
   합니다. BUFFER에는 최대 BUF_SIZE - 1자만 쓰고, 뒤에 null 종료자를 붙입니다. BUF_SIZE가 0이 아닌 한
   BUFFER는 항상 null 종료됩니다. 충분한 공간이 있었다면 BUFFER에 쓰였을 문자 수를 반환하며, null 종료자는 포함하지
   않습니다. */
int
snprintf (char *buffer, size_t buf_size, const char *format, ...) {
	va_list args;
	int retval;

	va_start (args, format);
	retval = vsnprintf (buffer, buf_size, format, args);
	va_end (args);

	return retval;
}

/* 서식화된 출력을 콘솔에 씁니다.
   커널에서는 콘솔이 비디오 디스플레이이자 첫 번째 직렬 포트입니다.
   사용자 공간에서는 콘솔이 파일 디스크립터 1입니다. */
int
printf (const char *format, ...) {
	va_list args;
	int retval;

	va_start (args, format);
	retval = vprintf (format, args);
	va_end (args);

	return retval;
}

/* printf() 서식 지정 내부. */

/* printf() 변환 항목. */
struct printf_conversion {
	/* 플래그. */
	enum {
		MINUS = 1 << 0,         /* '-' */
		PLUS = 1 << 1,          /* '+' */
		SPACE = 1 << 2,         /* ' ' */
		POUND = 1 << 3,         /* '#' */
		ZERO = 1 << 4,          /* '0' */
		GROUP = 1 << 5          /* '\'' */
	} flags;

	/* 최소 필드 너비. */
	int width;

	/* 숫자 정밀도.
	   -1은 정밀도가 지정되지 않았음을 나타냅니다. */
	int precision;

	/* 서식화할 인수의 형식. */
	enum {
		CHAR = 1,               /* hh */
		SHORT = 2,              /* h */
		INT = 3,                /* (없음) */
		INTMAX = 4,             /* j */
		LONG = 5,               /* l */
		LONGLONG = 6,           /* ll */
		PTRDIFFT = 7,           /* t */
		SIZET = 8               /* z */
	} type;
};

struct integer_base {
	int base;                   /* 기수. */
	const char *digits;         /* 숫자 집합. */
	int x;                      /* base 16에서만 사용할 `x' 문자. */
	int group;                  /* ' 플래그로 묶을 숫자 수. */
};

static const struct integer_base base_d = {10, "0123456789", 0, 3};
static const struct integer_base base_o = {8, "01234567", 0, 3};
static const struct integer_base base_x = {16, "0123456789abcdef", 'x', 4};
static const struct integer_base base_X = {16, "0123456789ABCDEF", 'X', 4};

static const char *parse_conversion (const char *format,
		struct printf_conversion *,
		va_list *);
static void format_integer (uintmax_t value, bool is_signed, bool negative,
		const struct integer_base *,
		const struct printf_conversion *,
		void (*output) (char, void *), void *aux);
static void output_dup (char ch, size_t cnt,
		void (*output) (char, void *), void *aux);
static void format_string (const char *string, int length,
		struct printf_conversion *,
		void (*output) (char, void *), void *aux);

void
__vprintf (const char *format, va_list args,
		void (*output) (char, void *), void *aux) {
	for (; *format != '\0'; format++) {
		struct printf_conversion c;

		/* 변환이 아닌 부분은 그대로 출력에 복사합니다. */
		if (*format != '%') {
			output (*format, aux);
			continue;
		}
		format++;

		/* %% => %. */
		if (*format == '%') {
			output ('%', aux);
			continue;
		}

		/* 변환 지정자를 파싱합니다. */
		format = parse_conversion (format, &c, &args);

		/* 변환을 수행합니다. */
		switch (*format) {
			case 'd':
			case 'i':
				{
					/* 부호 있는 정수 변환. */
					intmax_t value;

					switch (c.type) {
						case CHAR:
							value = (signed char) va_arg (args, int);
							break;
						case SHORT:
							value = (short) va_arg (args, int);
							break;
						case INT:
							value = va_arg (args, int);
							break;
						case INTMAX:
							value = va_arg (args, intmax_t);
							break;
						case LONG:
							value = va_arg (args, long);
							break;
						case LONGLONG:
							value = va_arg (args, long long);
							break;
						case PTRDIFFT:
							value = va_arg (args, ptrdiff_t);
							break;
						case SIZET:
							value = va_arg (args, size_t);
							if (value > SIZE_MAX / 2)
								value = value - SIZE_MAX - 1;
							break;
						default:
							NOT_REACHED ();
					}

					format_integer (value < 0 ? -value : value,
							true, value < 0, &base_d, &c, output, aux);
				}
				break;

			case 'o':
			case 'u':
			case 'x':
			case 'X':
				{
					/* 부호 없는 정수 변환. */
					uintmax_t value;
					const struct integer_base *b;

					switch (c.type) {
						case CHAR:
							value = (unsigned char) va_arg (args, unsigned);
							break;
						case SHORT:
							value = (unsigned short) va_arg (args, unsigned);
							break;
						case INT:
							value = va_arg (args, unsigned);
							break;
						case INTMAX:
							value = va_arg (args, uintmax_t);
							break;
						case LONG:
							value = va_arg (args, unsigned long);
							break;
						case LONGLONG:
							value = va_arg (args, unsigned long long);
							break;
						case PTRDIFFT:
							value = va_arg (args, ptrdiff_t);
#if UINTMAX_MAX != PTRDIFF_MAX
							value &= ((uintmax_t) PTRDIFF_MAX << 1) | 1;
#endif
							break;
						case SIZET:
							value = va_arg (args, size_t);
							break;
						default:
							NOT_REACHED ();
					}

					switch (*format) {
						case 'o': b = &base_o; break;
						case 'u': b = &base_d; break;
						case 'x': b = &base_x; break;
						case 'X': b = &base_X; break;
						default: NOT_REACHED ();
					}

					format_integer (value, false, false, b, &c, output, aux);
				}
				break;

			case 'c':
				{
					/* 문자를 한 글자 문자열로 취급합니다. */
					char ch = va_arg (args, int);
					format_string (&ch, 1, &c, output, aux);
				}
				break;

			case 's':
				{
					/* 문자열 변환. */
					const char *s = va_arg (args, char *);
					if (s == NULL)
						s = "(null)";

					/* 정밀도에 따라 문자열 길이를 제한합니다.
					   참고: c.precision == -1이면 strnlen()은
					   MAXLEN으로 SIZE_MAX를 받게 되는데, 이것이 바로 우리가 원하는 것입니다. */
					format_string (s, strnlen (s, c.precision), &c, output, aux);
				}
				break;

			case 'p':
				{
					/* 포인터 변환.
					   포인터를 %#x 형식으로 포맷합니다. */
					void *p = va_arg (args, void *);

					c.flags = POUND;
					format_integer ((uintptr_t) p, false, false,
							&base_x, &c, output, aux);
				}
				break;

			case 'f':
			case 'e':
			case 'E':
			case 'g':
			case 'G':
			case 'n':
				/* 부동소수점 연산은 지원하지 않으며,
				   %n은 보안 취약점의 일부가 될 수 있습니다. */
				__printf ("<<no %%%c in kernel>>", output, aux, *format);
				break;

			default:
				__printf ("<<no %%%c conversion>>", output, aux, *format);
				break;
		}
	}
}

/* FORMAT에서 시작하는 변환 옵션 문자를 파싱하고
 * C를 적절히 초기화합니다. FORMAT에서 변환을 나타내는 문자를
 * 반환합니다(예: `%d`의 `d`). `*` 필드 너비와 정밀도에는
 * ARGS를 사용합니다. */
static const char *
parse_conversion (const char *format, struct printf_conversion *c,
		va_list *args) {
	/* 플래그 문자를 파싱합니다. */
	c->flags = 0;
	for (;;) {
		switch (*format++) {
			case '-':
				c->flags |= MINUS;
				break;
			case '+':
				c->flags |= PLUS;
				break;
			case ' ':
				c->flags |= SPACE;
				break;
			case '#':
				c->flags |= POUND;
				break;
			case '0':
				c->flags |= ZERO;
				break;
			case '\'':
				c->flags |= GROUP;
				break;
			default:
				format--;
				goto not_a_flag;
		}
	}
not_a_flag:
	if (c->flags & MINUS)
		c->flags &= ~ZERO;
	if (c->flags & PLUS)
		c->flags &= ~SPACE;

	/* 필드 너비를 파싱합니다. */
	c->width = 0;
	if (*format == '*') {
		format++;
		c->width = va_arg (*args, int);
	} else {
		for (; isdigit (*format); format++)
			c->width = c->width * 10 + *format - '0';
	}
	if (c->width < 0) {
		c->width = -c->width;
		c->flags |= MINUS;
	}

	/* 정밀도를 파싱합니다. */
	c->precision = -1;
	if (*format == '.') {
		format++;
		if (*format == '*') {
			format++;
			c->precision = va_arg (*args, int);
		} else {
			c->precision = 0;
			for (; isdigit (*format); format++)
				c->precision = c->precision * 10 + *format - '0';
		}
		if (c->precision < 0)
			c->precision = -1;
	}
	if (c->precision >= 0)
		c->flags &= ~ZERO;

	/* 타입을 파싱합니다. */
	c->type = INT;
	switch (*format++) {
		case 'h':
			if (*format == 'h') {
				format++;
				c->type = CHAR;
			}
			else
				c->type = SHORT;
			break;

		case 'j':
			c->type = INTMAX;
			break;

		case 'l':
			if (*format == 'l') {
				format++;
				c->type = LONGLONG;
			}
			else
				c->type = LONG;
			break;

		case 't':
			c->type = PTRDIFFT;
			break;

		case 'z':
			c->type = SIZET;
			break;

		default:
			format--;
			break;
	}

	return format;
}

/* 정수 변환을 수행하여 보조 데이터 AUX와 함께 OUTPUT에
   기록합니다. 변환할 정수의 절댓값은 VALUE입니다. IS_SIGNED가
   참이면 NEGATIVE가 음수 값을 나타내는 부호 있는 변환을 수행하고,
   그렇지 않으면 부호 없는 변환을 수행하며 NEGATIVE는 무시합니다.
   출력은 제공된 기수 B에 따라 수행됩니다. 변환의 세부 사항은 C에
   있습니다. */
static void
format_integer (uintmax_t value, bool is_signed, bool negative,
		const struct integer_base *b,
		const struct printf_conversion *c,
		void (*output) (char, void *), void *aux) {
	char buf[64], *cp;            /* 버퍼와 현재 위치. */
	int x;                        /* 사용할 `x' 문자, 없으면 0. */
	int sign;                     /* 부호 문자, 없으면 0. */
	int precision;                /* 표시된 정밀도. */
	int pad_cnt;                  /* 필드 너비를 채울 패딩 문자 수. */
	int digit_cnt;                /* 지금까지 출력된 숫자 수. */

	/* 부호 문자가 있으면 결정합니다.
	   부호 없는 변환은 플래그가 부호를 요청하더라도
	   절대로 부호 문자를 가지지 않습니다. */
	sign = 0;
	if (is_signed) {
		if (c->flags & PLUS)
			sign = negative ? '-' : '+';
		else if (c->flags & SPACE)
			sign = negative ? '-' : ' ';
		else if (negative)
			sign = '-';
	}

	/* `0x` 또는 `0X`를 포함할지 결정합니다.
	   이는 # 플래그가 있는 0이 아닌 값의 16진수 변환에서만
	   포함됩니다. */
	x = (c->flags & POUND) && value ? b->x : 0;

	/* 숫자를 버퍼에 누적합니다.
	   이 알고리즘은 숫자를 역순으로 생성하므로, 나중에
	   버퍼의 내용을 역순으로 출력합니다. */
	cp = buf;
	digit_cnt = 0;
	while (value > 0) {
		if ((c->flags & GROUP) && digit_cnt > 0 && digit_cnt % b->group == 0)
			*cp++ = ',';
		*cp++ = b->digits[value % b->base];
		value /= b->base;
		digit_cnt++;
	}

	/* 정밀도에 맞도록 필요한 만큼 0을 덧붙입니다.
	   요청한 정밀도가 0이면 값 0은 빈 문자열로 렌더링되고,
	   그렇지 않으면 "0"으로 렌더링됩니다.
	   # 플래그를 base 8과 함께 사용하면 결과는 항상
	   0으로 시작해야 합니다. */
	precision = c->precision < 0 ? 1 : c->precision;
	while (cp - buf < precision && cp < buf + sizeof buf - 1)
		*cp++ = '0';
	if ((c->flags & POUND) && b->base == 8 && (cp == buf || cp[-1] != '0'))
		*cp++ = '0';

	/* 필드 너비를 채우는 패딩 문자 수를 계산합니다. */
	pad_cnt = c->width - (cp - buf) - (x ? 2 : 0) - (sign != 0);
	if (pad_cnt < 0)
		pad_cnt = 0;

	/* 출력을 수행합니다. */
	if ((c->flags & (MINUS | ZERO)) == 0)
		output_dup (' ', pad_cnt, output, aux);
	if (sign)
		output (sign, aux);
	if (x) {
		output ('0', aux);
		output (x, aux);
	}
	if (c->flags & ZERO)
		output_dup ('0', pad_cnt, output, aux);
	while (cp > buf)
		output (*--cp, aux);
	if (c->flags & MINUS)
		output_dup (' ', pad_cnt, output, aux);
}

/* 보조 데이터 AUX와 함께 CH를 CNT번 OUTPUT에 씁니다. */
static void
output_dup (char ch, size_t cnt, void (*output) (char, void *), void *aux) {
	while (cnt-- > 0)
		output (ch, aux);
}

/* STRING에서 시작하는 LENGTH개의 문자를 C에 지정된 변환에 따라
   형식화합니다. 보조 데이터 AUX와 함께 OUTPUT에 기록합니다. */
static void
format_string (const char *string, int length,
		struct printf_conversion *c,
		void (*output) (char, void *), void *aux) {
	int i;
	if (c->width > length && (c->flags & MINUS) == 0)
		output_dup (' ', c->width - length, output, aux);
	for (i = 0; i < length; i++)
		output (string[i], aux);
	if (c->width > length && (c->flags & MINUS) != 0)
		output_dup (' ', c->width - length, output, aux);
}

/* 가변 인수를 va_list로 변환하는 __vprintf()용 래퍼입니다. */
void
__printf (const char *format,
		void (*output) (char, void *), void *aux, ...) {
	va_list args;

	va_start (args, aux);
	__vprintf (format, args, output, aux);
	va_end (args);
}

/* BUF의 SIZE 바이트를 한 줄에 16개씩 배열된 16진수 바이트로
   콘솔에 덤프합니다. 숫자 오프셋도 포함되며, BUF의 첫 바이트에
   대해 OFS부터 시작합니다. ASCII가 참이면 대응하는 ASCII 문자도
   옆에 함께 표시됩니다. */
void
hex_dump (uintptr_t ofs, const void *buf_, size_t size, bool ascii) {
	const uint8_t *buf = buf_;
	const size_t per_line = 16; /* 줄당 최대 바이트 수. */

	while (size > 0) {
		size_t start, end, n;
		size_t i;

		/* 이 줄의 바이트 수. */
		start = ofs % per_line;
		end = per_line;
		if (end - start > size)
			end = start + size;
		n = end - start;

		/* 줄을 출력합니다. */
		printf ("%016llx  ", (uintmax_t) ROUND_DOWN (ofs, per_line));
		for (i = 0; i < start; i++)
			printf ("   ");
		for (; i < end; i++)
			printf ("%02hhx%c",
					buf[i - start], i == per_line / 2 - 1? '-' : ' ');
		if (ascii) {
			for (; i < per_line; i++)
				printf ("   ");
			printf ("|");
			for (i = 0; i < start; i++)
				printf (" ");
			for (; i < end; i++)
				printf ("%c",
						isprint (buf[i - start]) ? buf[i - start] : '.');
			for (; i < per_line; i++)
				printf (" ");
			printf ("|");
		}
		printf ("\n");

		ofs += n;
		buf += n;
		size -= n;
	}
}
