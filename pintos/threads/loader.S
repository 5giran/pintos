/* 이 파일은 MIT의 6.828 강의에서 사용된 소스 코드에서 파생되었다.
   원본 저작권 고지는 아래에 전문을 다시 실었다. */

/*
 * Copyright (C) 1997 Massachusetts Institute of Technology
 *
 * 이 소프트웨어는 저작권 보유자가 아래 라이선스에 따라 제공합니다.
 * 이 소프트웨어를 취득, 사용 및/또는 복제함으로써, 귀하는 아래
 * 조건을 읽고 이해했으며 이를 준수하는 데 동의합니다:
 *
 * 이 소프트웨어와 그 문서를 어떤 목적이든 수수료나 로열티 없이
 * 사용, 복제, 수정, 배포, 판매할 수 있는 권한을 부여합니다.
 * 단, 귀하가 만든 수정본을 포함하여 소프트웨어와 문서의 전체 복사본 또는
 * 일부마다 이 NOTICE 전문이 포함되어야 합니다.
 *
 * 이 소프트웨어는 "AS IS" 상태로 제공되며, 저작권 보유자는 명시적이든
 * 묵시적이든 어떠한 진술이나 보증도 하지 않습니다. 예시일 뿐이며 이에
 * 한정되지 않게, 저작권 보유자는 상품성 또는 특정 목적 적합성에 대한 보증이나
 * 이 소프트웨어 또는 문서의 사용이 제3자의 특허, 저작권, 상표 또는 기타 권리를
 * 침해하지 않는다는 보증을 하지 않습니다. 저작권 보유자는 이 소프트웨어나
 * 문서의 사용에 대해 어떠한 책임도 지지 않습니다.
 *
 * 저작권 보유자의 이름과 상표는 사전에 구체적인 서면 허가를 받지 않는 한
 * 이 소프트웨어와 관련된 광고나 홍보에 사용해서는 안 됩니다. 이 소프트웨어와
 * 관련 문서의 저작권은 항상 저작권 보유자에게 귀속됩니다. 모든 저작권
 * 보유자 목록은 이 소프트웨어와 함께 제공되었어야 하는 AUTHORS 파일을 보십시오.
 *
 * 이 파일은 이전에 저작권이 있던 소프트웨어에서 파생되었을 수 있습니다.
 * 이 저작권은 AUTHORS 파일에 나열된 저작권 보유자가 가한 변경 사항에만
 * 적용됩니다. 이 파일의 나머지 부분은 아래에 적힌 저작권 고지(있다면)의
 * 적용을 받습니다.
 */

#include "threads/loader.h"
	
#### Kernel loader.

#### This code should be stored in the first sector of the hard disk.
#### When the BIOS runs, it loads this code at physical address
#### 0x7c00-0x7e00 (512 bytes).  Then it jumps to the beginning of it,
#### in real mode.  This code switches into protected mode (32-bit
#### mode) so that all of memory can accessed, loads the kernel into
#### memory, and jumps to the first byte of the kernel, where start.S
#### is linked.
	
/* control register 0의 플래그들. */
#define CR0_PE 0x00000001      /* Protection Enable(보호 활성화) */
#define CR0_EM 0x00000004      /* (Floating-point) Emulation(에뮬레이션) */
#define CR0_PG 0x80000000      /* Paging(페이징) */
#define CR0_WP 0x00010000      /* kernel mode에서 Write-Protect 활성화. */


.globl start
start:
	
# Code runs in real mode, which is a 16-bit segment.
	.code16

# Disable interrupts, because we will not be prepared to handle them
# in protected mode until much later.
# String instructions go upward (e.g. for "rep stosl" below).

	cli
	cld

# Set up data segments.

	subw %ax, %ax
	movw %ax, %es
	movw %ax, %ds

# Set up stack segment.
# Stack grows downward starting from us.
# We don't ever use the stack, but we call into the BIOS,
# which might.

	movw %ax, %ss
	movw $0x7c00, %sp
	
#### Enable A20.  Address line 20 is tied to low when the machine
#### boots, which prevents addressing memory about 1 MB.  This code
#### fixes it.
	
# Poll status register while busy.

1:	inb $0x64, %al
	testb $0x2, %al
	jnz 1b

# Send command for writing output port.

	movb $0xd1, %al
	outb %al, $0x64

# Poll status register while busy.

1:	inb $0x64, %al
	testb $0x2, %al
	jnz 1b

# Enable A20 line.

	movb $0xdf, %al
	outb %al, $0x60


#### Get memory size, via interrupt 15h function e820h. Now our pintos
#### runs on x86_64, "Get Extended Memory Size" operation is not sufficient
#### for our purpose.
	mov $0xe820, %eax  # command
	mov $(E820_MAP4), %edi  # dst
	xor %ebx, %ebx
	mov $0x534d4150, %edx # magic
	mov $24, %ecx
	int $0x15
	cmp %eax, %edx
	test %ebx, %ebx
	je panic
	mov $24, %ebp

parse_e820:
	mov %ecx, -4(%edi)
	add $24, %edi
	mov $0xe820, %eax
	mov $24, %ecx
	int $0x15
	jc e820_parse_done
	add $24, %ebp
	test %ebx, %ebx
	jne parse_e820

e820_parse_done:
	mov %ecx, -4(%edi)
	movl $0x40, MULTIBOOT_FLAG
	movl %ebp, MULTIBOOT_MMAP_LEN
	movl $(E820_MAP), MULTIBOOT_MMAP_ADDR # e820

#### Switch to protected mode.

# Note that interrupts are still off.

# Point the GDTR to our GDT.  Protected mode requires a GDT.
# We need a data32 prefix to ensure that all 32 bits of the GDT
# descriptor are loaded (default is to load only 24 bits).

	data32 lgdt gdtdesc

# Then we turn on the following bits in CR0:
#    PE (Protect Enable): this turns on protected mode.
#    PG (Paging): turns on paging.
#    WP (Write Protect): if unset, ring 0 code ignores
#       write-protect bits in page tables (!).
#    EM (Emulation): forces floating-point instructions to trap.
#       We don't support floating point. 
	
	movl %cr0, %eax
	orl $CR0_PE, %eax
	movl %eax, %cr0
	
# We're now in protected mode in a 16-bit segment.  The CPU still has
# the real-mode code segment cached in %cs's segment descriptor.  We
# need to reload %cs, and the easiest way is to use a far jump.
# Because we're not in a 32-bit segment the data32 prefix is needed to
# jump to a 32-bit offset.

	data32 ljmp $SEL_KCSEG, $protcseg

# We're now in protected mode in a 32-bit segment.

	.code32

# Reload all the other segment registers and the stack pointer to
# point into our new GDT.

protcseg:
	movw $SEL_KDSEG, %ax
	movw %ax, %ds		
	movw %ax, %es		
	movw %ax, %fs		
	movw %ax, %gs		
	movw %ax, %ss
	movl $LOADER_PHYS_BASE + 0x30000, %esp

#### Load kernel starting at physical address LOADER_PHYS_BASE by
#### frobbing the IDE controller directly.

	movl $1, %ebx
	movl $LOADER_PHYS_BASE, %edi

# Disable interrupt delivery by IDE controller, because we will be
# polling for data.
# (If we don't do this, Bochs 2.2.6 will never deliver any IDE
# interrupt to us later after we reset the interrupt controller during
# boot, even if we also reset the IDE controller.)

	movw $0x3f6, %dx
	movb $0x02, %al
	outb %al, %dx
	
read_sector:

# Poll status register while controller busy.

	movl $0x1f7, %edx
1:	inb %dx, %al
	testb $0x80, %al
	jnz 1b

# Read a single sector.

	movl $0x1f2, %edx
	movb $1, %al
	outb %al, %dx

# Sector number to write in low 28 bits.
# LBA mode, device 0 in top 4 bits.

	movl %ebx, %eax
	andl $0x0fffffff, %eax
	orl $0xe0000000, %eax

# Dump %eax to ports 0x1f3...0x1f6.

	movl $4, %ecx
1:	incw %dx
	outb %al, %dx
	shrl $8, %eax
	loop 1b

# READ command to command register.

	incw %dx
	movb $0x20, %al
	outb %al, %dx

# Poll status register while controller busy.

1:	inb %dx, %al
	testb $0x80, %al
	jnz 1b

# Poll status register until data ready.

1:	inb %dx, %al
	testb $0x08, %al
	jz 1b

# Transfer sector.

	movl $256, %ecx
	movl $0x1f0, %edx
	rep insw

# Next sector.

	incl %ebx
	cmpl $KERNEL_LOAD_PAGES*8 + 1, %ebx
	jnz read_sector

#### Jump to kernel entry point.
	movl $LOADER_PHYS_BASE, %eax
	call *%eax
	jmp panic

#### GDT

gdt:
	.quad 0x0000000000000000	# null seg
	.quad 0x00cf9a000000ffff	# code seg
	.quad 0x00cf92000000ffff  # data seg
	
gdtdesc:
	.word	0x17			# sizeof (gdt) - 1
	.long	gdt

#### Fatal error.
#### Print panic_message (with help from the BIOS) and spin.

panic:  .code16			# We only panic in real mode.
	movw $panic_message, %si
	movb $0xe, %ah
	subb %bh, %bh
1:	lodsb
	test %al, %al
2:	jz 2b			# Spin.
	int $0x10
	jmp 1b

panic_message:
	.ascii "Panic!"
	.byte 0

#### Command-line arguments and their count.
#### This is written by the `pintos' utility and read by the kernel.
#### The loader itself does not do anything with the command line.
	.org LOADER_ARG_CNT - LOADER_BASE
arg_cnt:
	.long 0
	.org LOADER_ARGS - LOADER_BASE
args:
	.fill 0x80, 1, 0

#### Boot-sector signature.
#### The BIOS checks that this is set properly.
	.org LOADER_SIG - LOADER_BASE
	.word 0xaa55
