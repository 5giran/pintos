#ifndef __LIB_ROUND_H
#define __LIB_ROUND_H

/* X를 STEP의 가장 가까운 배수로 올림한 값을 반환한다.
 * 단, X >= 0, STEP >= 1인 경우만 해당한다. */
#define ROUND_UP(X, STEP) (((X) + (STEP) - 1) / (STEP) * (STEP))

/* X / STEP를 올림한 값을 반환한다.
 * 단, X >= 0, STEP >= 1인 경우만 해당한다. */
#define DIV_ROUND_UP(X, STEP) (((X) + (STEP) - 1) / (STEP))

/* X를 STEP의 가장 가까운 배수로 내림한 값을 반환한다.
 * 단, X >= 0, STEP >= 1인 경우만 해당한다. */
#define ROUND_DOWN(X, STEP) ((X) / (STEP) * (STEP))

/* DIV_ROUND_DOWN은 없다. 그냥 X / STEP이면 된다. */

#endif /* lib/round.h */
