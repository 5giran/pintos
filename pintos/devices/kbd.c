#include "devices/kbd.h"
#include <ctype.h>
#include <debug.h>
#include <stdio.h>
#include <string.h>
#include "devices/input.h"
#include "threads/interrupt.h"
#include "threads/io.h"

/* 키보드 데이터 레지스터 포트. */
#define DATA_REG 0x60

/* Shift 키의 현재 상태.
   눌려 있으면 True, 아니면 false. */
static bool left_shift, right_shift;    /* 왼쪽과 오른쪽 Shift 키. */
static bool left_alt, right_alt;        /* 왼쪽과 오른쪽 Alt 키. */
static bool left_ctrl, right_ctrl;      /* 왼쪽과 오른쪽 Ctl 키. */

/* Caps Lock 상태.
   켜져 있으면 True, 꺼져 있으면 false. */
static bool caps_lock;

/* 눌린 키의 개수. */
static int64_t key_cnt;

static intr_handler_func keyboard_interrupt;

/* 키보드를 초기화한다. */
void
kbd_init (void) {
	intr_register_ext (0x21, keyboard_interrupt, "8042 Keyboard");
}

/* 키보드 통계를 출력한다. */
void
kbd_print_stats (void) {
	printf ("Keyboard: %lld keys pressed\n", key_cnt);
}

/* 연속된 scancode 집합을 문자로 매핑한다. */
struct keymap {
	uint8_t first_scancode;     /* 첫 번째 scancode. */
	const char *chars;          /* chars[0]에는 scancode가 first_scancode이고, chars[1]에는 scancode가 first_scancode + 1이며, 그다음도 문자열 끝까지 같은 방식이다. */
};

/* Shift 키가 눌려 있든 아니든 같은 문자를 내는 키들.
   글자의 대소문자는 예외이며, 이는 다른 곳에서 처리한다. */
static const struct keymap invariant_keymap[] = {
	{0x01, "\033"},
	{0x0e, "\b"},
	{0x0f, "\tQWERTYUIOP"},
	{0x1c, "\r"},
	{0x1e, "ASDFGHJKL"},
	{0x2c, "ZXCVBNM"},
	{0x37, "*"},
	{0x39, " "},
	{0, NULL},
};

/* Shift를 누르지 않았을 때의 문자들, 그 차이가 중요한 키들에 대해. */
static const struct keymap unshifted_keymap[] = {
	{0x02, "1234567890-="},
	{0x1a, "[]"},
	{0x27, ";'`"},
	{0x2b, "\\"},
	{0x33, ",./"},
	{0, NULL},
};

/* Shift를 누른 상태에서의 문자들, 그 차이가 중요한 키들에 대해. */
static const struct keymap shifted_keymap[] = {
	{0x02, "!@#$%^&*()_+"},
	{0x1a, "{}"},
	{0x27, ":\"~"},
	{0x2b, "|"},
	{0x33, "<>?"},
	{0, NULL},
};

static bool map_key (const struct keymap[], unsigned scancode, uint8_t *);

static void
keyboard_interrupt (struct intr_frame *args UNUSED) {
	/* Shift 키 상태. */
	bool shift = left_shift || right_shift;
	bool alt = left_alt || right_alt;
	bool ctrl = left_ctrl || right_ctrl;

	/* 키보드 scancode. */
	unsigned code;

	/* 키가 눌리면 false, 해제되면 true. */
	bool release;

	/* `code'에 대응하는 문자. */
	uint8_t c;

	/* prefix code일 경우 두 번째 바이트를 포함해 scancode를 읽는다. */
	code = inb (DATA_REG);
	if (code == 0xe0)
		code = (code << 8) | inb (DATA_REG);

	/* 비트 0x80은 키 누름과 키 해제를 구분한다.
	   (prefix가 있어도 마찬가지다). */
	release = (code & 0x80) != 0;
	code &= ~0x80u;

	/* 키를 해석한다. */
	if (code == 0x3a) {
		/* Caps Lock. */
		if (!release)
			caps_lock = !caps_lock;
	} else if (map_key (invariant_keymap, code, &c)
			|| (!shift && map_key (unshifted_keymap, code, &c))
			|| (shift && map_key (shifted_keymap, code, &c))) {
		/* 일반 문자. */
		if (!release) {
			/* Ctrl, Shift를 처리한다.
			   Ctrl이 Shift보다 우선한다. */
			if (ctrl && c >= 0x40 && c < 0x60) {
				/* A는 0x41이고, Ctrl+A는 0x01이며, 등등. */
				c -= 0x40;
			} else if (shift == caps_lock)
				c = tolower (c);

			/* Alt는 최상위 비트를 설정해 처리한다.
			   이 0x80은 키 누름과 키 해제를 구분하는 데 쓰는 0x80과는 무관하다. */
			if (alt)
				c += 0x80;

			/* 키보드 버퍼에 추가한다. */
			if (!input_full ()) {
				key_cnt++;
				input_putc (c);
			}
		}
	} else {
		/* keycode를 shift 상태 변수로 매핑한다. */
		struct shift_key {
			unsigned scancode;
			bool *state_var;
		};

		/* Shift 키 테이블. */
		static const struct shift_key shift_keys[] = {
			{  0x2a, &left_shift},
			{  0x36, &right_shift},
			{  0x38, &left_alt},
			{0xe038, &right_alt},
			{  0x1d, &left_ctrl},
			{0xe01d, &right_ctrl},
			{0,      NULL},
		};

		const struct shift_key *key;

		/* 테이블을 스캔한다. */
		for (key = shift_keys; key->scancode != 0; key++)
			if (key->scancode == code) {
				*key->state_var = !release;
				break;
			}
	}
}

/* SCANCODE에 대해 keymaps K 배열을 스캔한다.
   찾으면 *C를 대응하는 문자로 설정하고 true를 반환한다.
   찾지 못하면 false를 반환하고 C는 무시된다. */
static bool
map_key (const struct keymap k[], unsigned scancode, uint8_t *c) {
	for (; k->first_scancode != 0; k++)
		if (scancode >= k->first_scancode
				&& scancode < k->first_scancode + strlen (k->chars)) {
			*c = k->chars[scancode - k->first_scancode];
			return true;
		}

	return false;
}
