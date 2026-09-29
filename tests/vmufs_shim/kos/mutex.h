#pragma once
typedef int mutex_t;
#define MUTEX_TYPE_NORMAL 0
static inline int mutex_init(mutex_t *m, int t) { *m = 0; (void)t; return 0; }
static inline int mutex_lock(mutex_t *m) { return (*m)++ ? -1 : 0; }
static inline int mutex_unlock(mutex_t *m) { *m = 0; return 0; }
static inline int mutex_destroy(mutex_t *m) { (void)m; return 0; }
