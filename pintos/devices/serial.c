#include "devices/serial.h"
#include <debug.h>
#include "devices/input.h"
#include "devices/intq.h"
#include "devices/timer.h"
#include "threads/io.h"
#include "threads/interrupt.h"
#include "threads/synch.h"
#include "threads/thread.h"

/* PC에서 사용하는 16550A UART의 레지스터 정의.
   16550A에는 여기 적힌 것보다 훨씬 더 많은 기능이 있지만, 이것이 우리가 필요한 전부다.
   하드웨어 정보는 [PC16650D]를 참조하라. */

/* 첫 번째 시리얼 포트의 I/O 포트 기준 주소. */
#define IO_BASE 0x3f8

/* DLAB=0 레지스터. */
#define RBR_REG (IO_BASE + 0)   /* 수신 버퍼 레지스터(Receiver Buffer Reg.). */
#define THR_REG (IO_BASE + 0)   /* 송신 보유 레지스터(Transmitter Holding Reg.). */
#define IER_REG (IO_BASE + 1)   /* 인터럽트 허용 레지스터(Interrupt Enable Reg.). */

/* DLAB=1 레지스터. */
#define LS_REG (IO_BASE + 0)    /* 분주기 래치(Divisor Latch) (LSB). */
#define MS_REG (IO_BASE + 1)    /* 분주기 래치(Divisor Latch) (MSB). */

/* DLAB에 영향을 받지 않는 레지스터. */
#define IIR_REG (IO_BASE + 2)   /* 인터럽트 식별 레지스터(Interrupt Identification Reg.) (읽기 전용) */
#define FCR_REG (IO_BASE + 2)   /* FIFO 제어 레지스터(FIFO Control Reg.) (쓰기 전용). */
#define LCR_REG (IO_BASE + 3)   /* 라인 제어 레지스터(Line Control Register). */
#define MCR_REG (IO_BASE + 4)   /* MODEM 제어 레지스터(MODEM Control Register). */
#define LSR_REG (IO_BASE + 5)   /* 라인 상태 레지스터(Line Status Register) (읽기 전용). */

/* 인터럽트 허용 레지스터(Interrupt Enable Register) 비트. */
#define IER_RECV 0x01           /* 데이터 수신 시 인터럽트. */
#define IER_XMIT 0x02           /* 송신 완료 시 인터럽트. */

/* 라인 제어 레지스터(Line Control Register) 비트. */
#define LCR_N81 0x03            /* 패리티 없음, 데이터 비트 8개, 스톱 비트 1개. */
#define LCR_DLAB 0x80           /* 분주기 래치 접근 비트(Divisor Latch Access Bit, DLAB). */

/* MODEM 제어 레지스터(MODEM Control Register). */
#define MCR_OUT2 0x08           /* 출력 라인 2. */

/* 라인 상태 레지스터(Line Status Register). */
#define LSR_DR 0x01             /* Data Ready: 수신된 데이터 바이트가 RBR에 있다. */
#define LSR_THRE 0x20           /* THR 비어 있음. */

/* 전송 모드. */
static enum { UNINIT, POLL, QUEUE } mode;

/* 전송할 데이터. */
static struct intq txq;

static void set_serial (int bps);
static void putc_poll (uint8_t);
static void write_ier (void);
static intr_handler_func serial_interrupt;

/* 폴링 모드로 시리얼 포트 장치를 초기화한다.
   폴링 모드는 쓰기 전에 시리얼 포트가 사용 가능해질 때까지 busy-wait한다.  느리지만, 인터럽트가 초기화되기 전에는 이것이
   우리가 할 수 있는 전부다. */
static void
init_poll (void) {
	ASSERT (mode == UNINIT);
	outb (IER_REG, 0);                    /* 모든 인터럽트를 끕니다. */
	outb (FCR_REG, 0);                    /* FIFO를 비활성화합니다. */
	set_serial (115200);                  /* 115.2 kbps, N-8-1. */
	outb (MCR_REG, MCR_OUT2);             /* 인터럽트를 활성화하는 데 필요합니다. */
	intq_init (&txq);
	mode = POLL;
}

/* 대기열 기반 interrupt-driven
   I/O를 위해 serial 포트 장치를 초기화합니다. interrupt-driven I/O에서는 CPU 시간을 낭비하지 않고
   serial 장치가 준비될 때까지 기다립니다. */
void
serial_init_queue (void) {
	enum intr_level old_level;

	if (mode == UNINIT)
		init_poll ();
	ASSERT (mode == POLL);

	intr_register_ext (0x20 + 4, serial_interrupt, "serial");
	mode = QUEUE;
	old_level = intr_disable ();
	write_ier ();
	intr_set_level (old_level);
}

/* BYTE를 serial 포트로 보냅니다. */
void
serial_putc (uint8_t byte) {
	enum intr_level old_level = intr_disable ();

	if (mode != QUEUE) {
		/* 아직 interrupt-driven I/O가 설정되지 않았다면,
		   바이트를 전송하기 위해 dumb polling을 사용합니다. */
		if (mode == UNINIT)
			init_poll ();
		putc_poll (byte);
	} else {
		/* 그렇지 않으면 바이트를 큐에 넣고 interrupt enable
		   레지스터를 업데이트합니다. */
		if (old_level == INTR_OFF && intq_full (&txq)) {
			/* 인터럽트가 꺼져 있고 전송 큐가 가득 찼습니다.
			   큐가 비워질 때까지 기다리려면
			   인터럽트를 다시 활성화해야 합니다.
			   그건 예의가 아니므로, 대신 폴링으로
			   문자를 보냅니다. */
			putc_poll (intq_getc (&txq));
		}

		intq_putc (&txq, byte);
		write_ier ();
	}

	intr_set_level (old_level);
}

/* serial buffer의 내용을 폴링
   모드로 포트를 통해 비웁니다. */
void
serial_flush (void) {
	enum intr_level old_level = intr_disable ();
	while (!intq_empty (&txq))
		putc_poll (intq_getc (&txq));
	intr_set_level (old_level);
}

/* 입력 버퍼의 가득 찬 상태가 바뀌었을 수 있습니다. 다시
   수신 인터럽트를 차단해야 하는지 검토합니다.
   문자가 버퍼에 추가되거나 제거될 때 input buffer 루틴에서 호출됩니다. */
void
serial_notify (void) {
	ASSERT (intr_get_level () == INTR_OFF);
	if (mode == QUEUE)
		write_ier ();
}

/* serial 포트를 BPS(비트/초)로 설정합니다. */
static void
set_serial (int bps) {
	int base_rate = 1843200 / 16;         /* 16550A의 기본 주파수, Hz 단위. */
	uint16_t divisor = base_rate / bps;   /* 클럭 속도 분주값. */

	ASSERT (bps >= 300 && bps <= 115200);

	/* DLAB을 활성화합니다. */
	outb (LCR_REG, LCR_N81 | LCR_DLAB);

	/* 데이터 전송률을 설정합니다. */
	outb (LS_REG, divisor & 0xff);
	outb (MS_REG, divisor >> 8);

	/* DLAB을 해제합니다. */
	outb (LCR_REG, LCR_N81);
}

/* interrupt enable 레지스터를 갱신합니다. */
static void
write_ier (void) {
	uint8_t ier = 0;

	ASSERT (intr_get_level () == INTR_OFF);

	/* 전송할 문자가 있으면 송신 인터럽트를
	   활성화합니다. */
	if (!intq_empty (&txq))
		ier |= IER_XMIT;

	/* 수신한 문자를 저장할 공간이 있으면 수신 인터럽트를
	   활성화합니다. */
	if (!input_full ())
		ier |= IER_RECV;

	outb (IER_REG, ier);
}

/* serial 포트가 준비될 때까지 폴링한 뒤,
   BYTE를 전송합니다. */
static void
putc_poll (uint8_t byte) {
	ASSERT (intr_get_level () == INTR_OFF);

	while ((inb (LSR_REG) & LSR_THRE) == 0)
		continue;
	outb (THR_REG, byte);
}

/* serial 인터럽트 핸들러. */
static void
serial_interrupt (struct intr_frame *f UNUSED) {
	/* UART의 interrupt 상태를 조회합니다. 이것이 없으면
	   QEMU에서 실행할 때 가끔 인터럽트를 놓칠 수 있습니다. */
	inb (IIR_REG);

	/* 바이트를 받을 공간이 있고 하드웨어가
	   우리를 위한 바이트를 가지고 있는 동안, 바이트를 수신합니다. */
	while (!input_full () && (inb (LSR_REG) & LSR_DR) != 0)
		input_putc (inb (RBR_REG));

	/* 전송할 바이트가 있고 하드웨어가
	   전송용 바이트를 받을 준비가 되어 있는 동안, 바이트를 전송합니다. */
	while (!intq_empty (&txq) && (inb (LSR_REG) & LSR_THRE) != 0)
		outb (THR_REG, intq_getc (&txq));

	/* 큐 상태에 따라 interrupt enable 레지스터를 업데이트합니다. */
	write_ier ();
}
