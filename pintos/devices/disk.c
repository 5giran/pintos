#include "devices/disk.h"
#include <ctype.h>
#include <debug.h>
#include <stdbool.h>
#include <stdio.h>
#include "devices/timer.h"
#include "threads/io.h"
#include "threads/interrupt.h"
#include "threads/synch.h"

/* 이 파일의 코드는 ATA (IDE)
   컨트롤러에 대한 인터페이스다.  가능한 한 [ATA-3]를 따르려고 한다. */

/* ATA command block 포트 주소. */
#define reg_data(CHANNEL) ((CHANNEL)->reg_base + 0)     /* 데이터. */
#define reg_error(CHANNEL) ((CHANNEL)->reg_base + 1)    /* 오류. */
#define reg_nsect(CHANNEL) ((CHANNEL)->reg_base + 2)    /* 섹터 수. */
#define reg_lbal(CHANNEL) ((CHANNEL)->reg_base + 3)     /* LBA 0:7. */
#define reg_lbam(CHANNEL) ((CHANNEL)->reg_base + 4)     /* LBA 15:8. */
#define reg_lbah(CHANNEL) ((CHANNEL)->reg_base + 5)     /* LBA 23:16. */
#define reg_device(CHANNEL) ((CHANNEL)->reg_base + 6)   /* Device/LBA 27:24. */
#define reg_status(CHANNEL) ((CHANNEL)->reg_base + 7)   /* 상태 (읽기 전용). */
#define reg_command(CHANNEL) reg_status (CHANNEL)       /* 명령 (쓰기 전용). */

/* ATA control block 포트 주소.
   (non-legacy ATA 컨트롤러를 지원했다면 이것으로는
   충분히 유연하지 않았겠지만, 우리가 하는 용도에는 충분하다.) */
#define reg_ctl(CHANNEL) ((CHANNEL)->reg_base + 0x206)  /* 제어 (쓰기 전용). */
#define reg_alt_status(CHANNEL) reg_ctl (CHANNEL)       /* 대체 상태 (읽기 전용). */

/* Alternate Status Register 비트. */
#define STA_BSY 0x80            /* 바쁨. */
#define STA_DRDY 0x40           /* 장치 준비됨. */
#define STA_DRQ 0x08            /* 데이터 요청. */

/* Control Register 비트. */
#define CTL_SRST 0x04           /* 소프트웨어 리셋. */

/* Device Register 비트. */
#define DEV_MBS 0xa0            /* 반드시 설정되어 있어야 한다. */
#define DEV_LBA 0x40            /* 선형 기반 주소 지정. */
#define DEV_DEV 0x10            /* 장치 선택: 0=master, 1=slave. */

/* 명령.
   더 많은 명령이 정의되어 있지만, 우리가 사용하는 것은
   이 작은 부분집합뿐이다. */
#define CMD_IDENTIFY_DEVICE 0xec        /* IDENTIFY DEVICE. */
#define CMD_READ_SECTOR_RETRY 0x20      /* 재시도 포함 READ SECTOR. */
#define CMD_WRITE_SECTOR_RETRY 0x30     /* 재시도 포함 WRITE SECTOR. */

/* ATA 장치. */
struct disk {
	char name[8];               /* 이름, 예: "hd0:1". */
	struct channel *channel;    /* 디스크가 연결된 채널. */
	int dev_no;                 /* master 또는 slave에 대한 장치 0 또는 1. */

	bool is_ata;                /* 1이면 이 장치는 ATA 디스크다. */
	disk_sector_t capacity;     /* 섹터 단위 용량 (is_ata인 경우). */

	long long read_cnt;         /* 읽은 섹터 수. */
	long long write_cnt;        /* 기록한 섹터 수. */
};

/* ATA 채널(=컨트롤러).
   각 채널은 최대 두 개의 디스크를 제어할 수 있다. */
struct channel {
	char name[8];               /* 이름, 예: "hd0". */
	uint16_t reg_base;          /* 기본 I/O 포트. */
	uint8_t irq;                /* 사용 중인 인터럽트. */

	struct lock lock;           /* 컨트롤러에 접근하려면 획득해야 한다. */
	bool expecting_interrupt;   /* 인터럽트가 예상되면 true, 그렇지 않고 어떤 인터럽트라도 spurious라면 false. */
	struct semaphore completion_wait;   /* 인터럽트 핸들러에 의해 up된다. */

	struct disk devices[2];     /* 이 채널의 장치들. */
};

/* 표준 PC에 있는 두 개의 "legacy" ATA 채널을 지원한다. */
#define CHANNEL_CNT 2
static struct channel channels[CHANNEL_CNT];

static void reset_channel (struct channel *);
static bool check_device_type (struct disk *);
static void identify_ata_device (struct disk *);

static void select_sector (struct disk *, disk_sector_t);
static void issue_pio_command (struct channel *, uint8_t command);
static void input_sector (struct channel *, void *);
static void output_sector (struct channel *, const void *);

static void wait_until_idle (const struct disk *);
static bool wait_while_busy (const struct disk *);
static void select_device (const struct disk *);
static void select_device_wait (const struct disk *);

static void interrupt_handler (struct intr_frame *);

/* 디스크 서브시스템을 초기화하고 디스크를 감지한다. */
void
disk_init (void) {
	size_t chan_no;

	for (chan_no = 0; chan_no < CHANNEL_CNT; chan_no++) {
		struct channel *c = &channels[chan_no];
		int dev_no;

		/* 채널을 초기화한다. */
		snprintf (c->name, sizeof c->name, "hd%zu", chan_no);
		switch (chan_no) {
			case 0:
				c->reg_base = 0x1f0;
				c->irq = 14 + 0x20;
				break;
			case 1:
				c->reg_base = 0x170;
				c->irq = 15 + 0x20;
				break;
			default:
				NOT_REACHED ();
		}
		lock_init (&c->lock);
		c->expecting_interrupt = false;
		sema_init (&c->completion_wait, 0);

		/* 장치를 초기화한다. */
		for (dev_no = 0; dev_no < 2; dev_no++) {
			struct disk *d = &c->devices[dev_no];
			snprintf (d->name, sizeof d->name, "%s:%d", c->name, dev_no);
			d->channel = c;
			d->dev_no = dev_no;

			d->is_ata = false;
			d->capacity = 0;

			d->read_cnt = d->write_cnt = 0;
		}

		/* 인터럽트 핸들러를 등록한다. */
		intr_register_ext (c->irq, interrupt_handler, c->name);

		/* 하드웨어를 리셋한다. */
		reset_channel (c);

		/* ATA 하드 디스크를 다른 장치와 구별한다. */
		if (check_device_type (&c->devices[0]))
			check_device_type (&c->devices[1]);

		/* 하드 디스크 식별 정보를 읽는다. */
		for (dev_no = 0; dev_no < 2; dev_no++)
			if (c->devices[dev_no].is_ata)
				identify_ata_device (&c->devices[dev_no]);
	}

	/* 아래 줄은 수정하지 마시오. */
	register_disk_inspect_intr ();
}

/* 디스크 통계를 출력한다. */
void
disk_print_stats (void) {
	int chan_no;

	for (chan_no = 0; chan_no < CHANNEL_CNT; chan_no++) {
		int dev_no;

		for (dev_no = 0; dev_no < 2; dev_no++) {
			struct disk *d = disk_get (chan_no, dev_no);
			if (d != NULL && d->is_ata)
				printf ("%s: %lld reads, %lld writes\n",
						d->name, d->read_cnt, d->write_cnt);
		}
	}
}

/* CHAN_NO로 번호가 매겨진 채널 안에서 DEV_NO로 번호가 매겨진 디스크를 반환한다--즉, master 또는 slave에 대해
 * 각각 0 또는 1이다.
 *
 * Pintos는 디스크를 다음처럼 사용한다:
 * 0:0 - boot loader, 명령줄 인자, 운영 체제 커널
 * 0:1 - 파일 시스템
 * 1:0 - scratch
 * 1:1 - swap */
struct disk *
disk_get (int chan_no, int dev_no) {
	ASSERT (dev_no == 0 || dev_no == 1);

	if (chan_no < (int) CHANNEL_CNT) {
		struct disk *d = &channels[chan_no].devices[dev_no];
		if (d->is_ata)
			return d;
	}
	return NULL;
}

/* DISK_SECTOR_SIZE바이트 섹터 단위로 측정한 디스크 D의 크기를 반환한다. */
disk_sector_t
disk_size (struct disk *d) {
	ASSERT (d != NULL);

	return d->capacity;
}

/* 디스크 D의 SEC_NO 섹터를 BUFFER로 읽어들이는데, BUFFER에는
   DISK_SECTOR_SIZE바이트를 담을 공간이 있어야 한다.
   내부적으로 디스크 접근을 동기화하므로, 외부의
   디스크별 락은 필요 없다. */
void
disk_read (struct disk *d, disk_sector_t sec_no, void *buffer) {
	struct channel *c;

	ASSERT (d != NULL);
	ASSERT (buffer != NULL);

	c = d->channel;
	lock_acquire (&c->lock);
	select_sector (d, sec_no);
	issue_pio_command (c, CMD_READ_SECTOR_RETRY);
	sema_down (&c->completion_wait);
	if (!wait_while_busy (d))
		PANIC ("%s: disk read failed, sector=%"PRDSNu, d->name, sec_no);
	input_sector (c, buffer);
	d->read_cnt++;
	lock_release (&c->lock);
}

/* BUFFER에 들어 있는 DISK_SECTOR_SIZE바이트를 사용해
   디스크 D의 SEC_NO 섹터를 기록한다.  디스크가 데이터를
   수신했다고 확인한 뒤 반환한다.
   내부적으로 디스크 접근을 동기화하므로, 외부의
   디스크별 락은 필요 없다. */
void
disk_write (struct disk *d, disk_sector_t sec_no, const void *buffer) {
	struct channel *c;

	ASSERT (d != NULL);
	ASSERT (buffer != NULL);

	c = d->channel;
	lock_acquire (&c->lock);
	select_sector (d, sec_no);
	issue_pio_command (c, CMD_WRITE_SECTOR_RETRY);
	if (!wait_while_busy (d))
		PANIC ("%s: disk write failed, sector=%"PRDSNu, d->name, sec_no);
	output_sector (c, buffer);
	sema_down (&c->completion_wait);
	d->write_cnt++;
	lock_release (&c->lock);
}

/* 디스크 감지 및 식별. */

static void print_ata_string (char *string, size_t size);

/* ATA 채널을 리셋하고, 그 위에 존재하는 모든 장치가
   리셋을 끝낼 때까지 기다린다. */
static void
reset_channel (struct channel *c) {
	bool present[2];
	int dev_no;

	/* ATA 리셋 시퀀스는 어떤 장치가 존재하느냐에 따라 달라지므로,
	   먼저 장치 존재 여부를 감지하는 것부터 시작한다. */
	for (dev_no = 0; dev_no < 2; dev_no++) {
		struct disk *d = &c->devices[dev_no];

		select_device (d);

		outb (reg_nsect (c), 0x55);
		outb (reg_lbal (c), 0xaa);

		outb (reg_nsect (c), 0xaa);
		outb (reg_lbal (c), 0x55);

		outb (reg_nsect (c), 0x55);
		outb (reg_lbal (c), 0xaa);

		present[dev_no] = (inb (reg_nsect (c)) == 0x55
				&& inb (reg_lbal (c)) == 0xaa);
	}

	/* soft reset 시퀀스를 수행한다. 이 과정에서 부수 효과로 device 0이 선택된다.
	   또한 인터럽트를 활성화한다. */
	outb (reg_ctl (c), 0);
	timer_usleep (10);
	outb (reg_ctl (c), CTL_SRST);
	timer_usleep (10);
	outb (reg_ctl (c), 0);

	timer_msleep (150);

	/* device 0이 BSY를 해제할 때까지 기다린다. */
	if (present[0]) {
		select_device (&c->devices[0]);
		wait_while_busy (&c->devices[0]);
	}

	/* device 1이 BSY를 해제할 때까지 기다린다. */
	if (present[1]) {
		int i;

		select_device (&c->devices[1]);
		for (i = 0; i < 3000; i++) {
			if (inb (reg_nsect (c)) == 1 && inb (reg_lbal (c)) == 1)
				break;
			timer_msleep (10);
		}
		wait_while_busy (&c->devices[1]);
	}
}

/* device D가 ATA 디스크인지 확인하고 D의 is_ata
   멤버를 적절히 설정한다.  D가 device 0(master)라면, 이 채널에
   slave(device 1)가 존재할 가능성이 있으면 true를 반환한다.  D가 device 1(slave)라면
   반환값은 의미가 없다. */
static bool
check_device_type (struct disk *d) {
	struct channel *c = d->channel;
	uint8_t error, lbam, lbah, status;

	select_device (d);

	error = inb (reg_error (c));
	lbam = inb (reg_lbam (c));
	lbah = inb (reg_lbah (c));
	status = inb (reg_status (c));

	if ((error != 1 && (error != 0x81 || d->dev_no == 1))
			|| (status & STA_DRDY) == 0
			|| (status & STA_BSY) != 0) {
		d->is_ata = false;
		return error != 0x81;
	} else {
		d->is_ata = (lbam == 0 && lbah == 0) || (lbam == 0x3c && lbah == 0xc3);
		return true;
	}
}

/* 디스크 D에 IDENTIFY DEVICE 명령을 보내고 응답을 읽는다.  결과를 바탕으로
   D의 capacity 멤버를 초기화하고, 디스크를 설명하는 메시지를
   콘솔에 출력한다. */
static void
identify_ata_device (struct disk *d) {
	struct channel *c = d->channel;
	uint16_t id[DISK_SECTOR_SIZE / 2];

	ASSERT (d->is_ata);

	/* IDENTIFY DEVICE 명령을 보내고, 장치의 응답이 준비되었음을
	   알리는 인터럽트를 기다린 뒤, 데이터를
	   우리 버퍼로 읽어들인다. */
	select_device_wait (d);
	issue_pio_command (c, CMD_IDENTIFY_DEVICE);
	sema_down (&c->completion_wait);
	if (!wait_while_busy (d)) {
		d->is_ata = false;
		return;
	}
	input_sector (c, id);

	/* 용량을 계산한다. */
	d->capacity = id[60] | ((uint32_t) id[61] << 16);

	/* 식별 메시지를 출력한다. */
	printf ("%s: detected %'"PRDSNu" sector (", d->name, d->capacity);
	if (d->capacity > 1024 / DISK_SECTOR_SIZE * 1024 * 1024)
		printf ("%"PRDSNu" GB",
				d->capacity / (1024 / DISK_SECTOR_SIZE * 1024 * 1024));
	else if (d->capacity > 1024 / DISK_SECTOR_SIZE * 1024)
		printf ("%"PRDSNu" MB", d->capacity / (1024 / DISK_SECTOR_SIZE * 1024));
	else if (d->capacity > 1024 / DISK_SECTOR_SIZE)
		printf ("%"PRDSNu" kB", d->capacity / (1024 / DISK_SECTOR_SIZE));
	else
		printf ("%"PRDSNu" byte", d->capacity * DISK_SECTOR_SIZE);
	printf (") disk, model \"");
	print_ata_string ((char *) &id[27], 40);
	printf ("\", serial \"");
	print_ata_string ((char *) &id[10], 20);
	printf ("\"\n");
}

/* SIZE바이트로 이루어진 STRING을 특이한 형식으로 출력한다:
   각 바이트 쌍의 순서가 뒤집혀 있다.  뒤의 공백 및/또는 null은 출력하지 않는다. */
static void
print_ata_string (char *string, size_t size) {
	size_t i;

	/* 마지막 공백이 아닌, null이 아닌 문자를 찾는다. */
	for (; size > 0; size--) {
		int c = string[(size - 1) ^ 1];
		if (c != '\0' && !isspace (c))
			break;
	}

	/* 출력한다. */
	for (i = 0; i < size; i++)
		printf ("%c", string[i ^ 1]);
}

/* 장치 D를 선택하고, 준비될 때까지 기다린 다음,
   디스크의 섹터 선택 레지스터에 SEC_NO를 기록한다.  (우리는
   LBA mode를 사용한다.) */
static void
select_sector (struct disk *d, disk_sector_t sec_no) {
	struct channel *c = d->channel;

	ASSERT (sec_no < d->capacity);
	ASSERT (sec_no < (1UL << 28));

	select_device_wait (d);
	outb (reg_nsect (c), 1);
	outb (reg_lbal (c), sec_no);
	outb (reg_lbam (c), sec_no >> 8);
	outb (reg_lbah (c), (sec_no >> 16));
	outb (reg_device (c),
			DEV_MBS | DEV_LBA | (d->dev_no == 1 ? DEV_DEV : 0) | (sec_no >> 24));
}

/* COMMAND를 채널 C에 기록하고, 완료 인터럽트를 받을 준비를 한다. */
static void
issue_pio_command (struct channel *c, uint8_t command) {
	/* 인터럽트가 활성화되어 있어야 한다. 그렇지 않으면 완료 핸들러가
	   우리 세마포어를 up하지 못한다. */
	ASSERT (intr_get_level () == INTR_ON);

	c->expecting_interrupt = true;
	outb (reg_command (c), command);
}

/* PIO 모드에서 채널 C의 데이터 레지스터에서 섹터를 읽어
   DISK_SECTOR_SIZE바이트를 담을 수 있어야 하는 SECTOR에 넣는다. */
static void
input_sector (struct channel *c, void *sector) {
	insw (reg_data (c), sector, DISK_SECTOR_SIZE / 2);
}

/* PIO 모드에서 SECTOR를 채널 C의 데이터 레지스터에 기록한다.
   SECTOR에는 DISK_SECTOR_SIZE바이트가 들어 있어야 한다. */
static void
output_sector (struct channel *c, const void *sector) {
	outsw (reg_data (c), sector, DISK_SECTOR_SIZE / 2);
}

/* 저수준 ATA 기본 연산. */

/* 컨트롤러가 idle 상태가 될 때까지 최대 10초 동안 기다린다. 즉,
   status register에서 BSY와 DRQ 비트가 지워지기를 기다린다.
   부수 효과로, status register를 읽으면 대기 중인
   interrupt가 모두 지워진다. */
static void
wait_until_idle (const struct disk *d) {
	int i;

	for (i = 0; i < 1000; i++) {
		if ((inb (reg_status (d->channel)) & (STA_BSY | STA_DRQ)) == 0)
			return;
		timer_usleep (10);
	}

	printf ("%s: idle timeout\n", d->name);
}

/* 디스크 D가 BSY를 해제할 때까지 최대 30초 동안 기다린 뒤,
   DRQ 비트의 상태를 반환한다.
   ATA 표준에 따르면 디스크가 리셋을 완료하는 데 그 정도 시간이
   걸릴 수 있다. */
static bool
wait_while_busy (const struct disk *d) {
	struct channel *c = d->channel;
	int i;

	for (i = 0; i < 3000; i++) {
		if (i == 700)
			printf ("%s: busy, waiting...", d->name);
		if (!(inb (reg_alt_status (c)) & STA_BSY)) {
			if (i >= 700)
				printf ("ok\n");
			return (inb (reg_alt_status (c)) & STA_DRQ) != 0;
		}
		timer_msleep (10);
	}

	printf ("failed\n");
	return false;
}

/* D의 채널을 프로그래밍하여 D가 현재 선택된 디스크가 되도록 한다. */
static void
select_device (const struct disk *d) {
	struct channel *c = d->channel;
	uint8_t dev = DEV_MBS;
	if (d->dev_no == 1)
		dev |= DEV_DEV;
	outb (reg_device (c), dev);
	inb (reg_alt_status (c));
	timer_nsleep (400);
}

/* select_device()처럼 채널에서 디스크 D를 선택하되, 앞뒤로 채널이 유휴 상태가 되기를 기다린다. */
static void
select_device_wait (const struct disk *d) {
	wait_until_idle (d);
	select_device (d);
	wait_until_idle (d);
}

/* ATA 인터럽트 핸들러. */
static void
interrupt_handler (struct intr_frame *f) {
	struct channel *c;

	for (c = channels; c < channels + CHANNEL_CNT; c++)
		if (f->vec_no == c->irq) {
			if (c->expecting_interrupt) {
				inb (reg_status (c));               /* 인터럽트에 응답한다. */
				sema_up (&c->completion_wait);      /* 대기 중인 스레드를 깨운다. */
			} else
				printf ("%s: unexpected interrupt\n", c->name);
			return;
		}

	NOT_REACHED ();
}

static void
inspect_read_cnt (struct intr_frame *f) {
	struct disk * d = disk_get (f->R.rdx, f->R.rcx);
	f->R.rax = d->read_cnt;
}

static void
inspect_write_cnt (struct intr_frame *f) {
	struct disk * d = disk_get (f->R.rdx, f->R.rcx);
	f->R.rax = d->write_cnt;
}

/* 디스크 r/w cnt를 테스트하는 도구. 이 함수를 int 0x43과 int 0x44를 통해 호출한다.
 * 입력:
 * @RDX - 검사할 디스크의 chan_no
 * @RCX - 검사할 디스크의 dev_no
 * 출력:
 * @RAX - 디스크의 Read/Write count. */
void
register_disk_inspect_intr (void) {
	intr_register_int (0x43, 3, INTR_OFF, inspect_read_cnt, "Inspect Disk Read Count");
	intr_register_int (0x44, 3, INTR_OFF, inspect_write_cnt, "Inspect Disk Write Count");
}
