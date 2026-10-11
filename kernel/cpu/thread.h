#ifndef SYSTEMZERO_THREAD_H
#define SYSTEMZERO_THREAD_H
#include "isr.h"
#include "../env_vars.h"
#define THREAD_LIMIT 16u
#define THREAD_STACK 65536u
typedef int (*thread_entry_t)(env_vars_t *, uint32_t);
void threads_init(env_vars_t *env);
registers_t *threads_schedule(registers_t *frame, uint32_t ticks, int force);
void thread_guard_enter(void);
void thread_guard_leave(void);
int thread_start(env_vars_t *env, thread_entry_t entry, uint32_t arg);
int thread_join(int id);
void thread_yield(void);
int thread_sleep(uint32_t milliseconds);
int thread_id(void);
int thread_count(void);
uint32_t thread_scope_begin(void);
void thread_scope_end(uint32_t previous);
int thread_mutex_create(void);
int thread_mutex_lock(int id);
int thread_mutex_unlock(int id);
#endif
