#include "threads/loader.h"

OUTPUT_FORMAT("elf64-x86-64")
OUTPUT_ARCH(i386:x86-64)
ENTRY(_start)			/* kernel은 "start" 심벌에서 시작한다. */

SECTIONS
{
  /* kernel base의 virtual address를 지정한다. */
	. = LOADER_KERN_BASE + LOADER_PHYS_BASE;

	PROVIDE(start = .);
  /* kernel은 코드로 시작하고, 그 뒤에 read-only data와 writable data가 온다. */
	.text : AT(LOADER_PHYS_BASE) {
		*(.entry)
		*(.text .text.* .stub .gnu.linkonce.t.*)
	} = 0x90
	.rodata         : { *(.rodata .rodata.* .gnu.linkonce.r.*) }

	. = ALIGN(0x1000);
	PROVIDE(_end_kernel_text = .);

  .data : { *(.data) *(.data.*)}

  /* BSS(0으로 초기화되는 데이터)는 다른 모든 것 뒤에 온다. */
  PROVIDE(_start_bss = .);
  .bss : { *(.bss) }
  PROVIDE(_end_bss = .);

  PROVIDE(_end = .);

	/DISCARD/ : {
		*(.eh_frame .note.GNU-stack .stab)
	}
}
