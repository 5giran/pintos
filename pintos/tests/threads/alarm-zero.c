/* 즉시 반환되어야 하는 timer_sleep(0)을 테스트합니다. */

#include <stdio.h>
#include "tests/threads/tests.h"
#include "threads/malloc.h"
#include "threads/synch.h"
#include "threads/thread.h"
#include "devices/timer.h"

void
test_alarm_zero (void) 
{
  timer_sleep (0);
  pass ();
}
