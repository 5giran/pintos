#ifndef __LIB_DEBUG_LOG_H
#define __LIB_DEBUG_LOG_H

#include <stdio.h>

#ifdef DEBUG_LOG
#define DBG(...) printf (__VA_ARGS__)
#else
#define DBG(...) ((void) 0)
#endif

#endif /* lib/debug_log.h */
