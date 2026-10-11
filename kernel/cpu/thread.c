#include "thread.h"
#include "timer.h"
#include "../drivers/utils/mem.h"
#include "../desktop.h"
_Static_assert(sizeof(registers_t) == 56, "i386 interrupt stack layout");
enum { FREE, RUNNABLE, SLEEPING, DONE };
typedef struct {
    registers_t *frame;
    env_vars_t *env;
    thread_entry_t entry;
    uint32_t argument, owner, deadline, guard;
    int id, state, result;
} thread_t;
static thread_t threads[THREAD_LIMIT];
static uint8_t stacks[THREAD_LIMIT][THREAD_STACK] __attribute__((aligned(16)));
static struct { uint32_t owner; int holder, id; } mutexes[32];
static uint32_t current, initialized, next_owner = 1;
static int next_id = 1, next_mutex = 1;
static uint32_t irq_save(void) {
    uint32_t flags; __asm__ volatile("pushfl; popl %0; cli" : "=r"(flags) : : "memory"); return flags;
}
static void irq_restore(uint32_t flags) { __asm__ volatile("pushl %0; popfl" : : "r"(flags) : "memory"); }
void thread_guard_enter(void) { if (initialized) threads[current].guard++; }
void thread_guard_leave(void) { if (initialized && threads[current].guard) threads[current].guard--; }
void thread_yield(void) {
    if (!initialized) return;
    uint32_t slot = current, guard = threads[slot].guard;
    threads[slot].guard = 0;
    __asm__ volatile("int $48" : : : "memory");
    threads[slot].guard = guard;
}
int thread_id(void) { return initialized ? threads[current].id : 0; }
int thread_count(void) {
    int n = 0; for (uint32_t i = 3; i < THREAD_LIMIT; i++)
        if (threads[i].state == RUNNABLE || threads[i].state == SLEEPING) n++;
    return n;
}
static void thread_trampoline(void) {
    thread_t *t = &threads[current];
    t->result = t->entry(t->env, t->argument);
    uint32_t flags = irq_save();
    for (uint32_t i = 0; i < 32; i++) if (mutexes[i].holder == t->id) mutexes[i].holder = -1;
    t->state = DONE; irq_restore(flags);
    thread_yield(); for (;;) __asm__ volatile("hlt");
}
static void prepare(uint32_t slot, env_vars_t *env, thread_entry_t entry, uint32_t arg, uint32_t owner) {
    thread_t *t = &threads[slot];
    uint32_t top = (uint32_t)(uintptr_t)(stacks[slot] + THREAD_STACK) - 4;
    registers_t *frame = (void *)(top - sizeof(registers_t));
    memset(frame, 0, sizeof *frame);
    frame->ds = 0x10; frame->cs = 8; frame->eflags = 0x202;
    frame->eip = (uint32_t)(uintptr_t)thread_trampoline;
    *t = (thread_t){.frame = frame, .env = env, .entry = entry, .argument = arg,
        .owner = owner, .id = next_id++, .state = RUNNABLE};
}
static int idle(env_vars_t *env, uint32_t arg) { for (;;) __asm__ volatile("hlt"); return 0; }
static int desktop_worker(env_vars_t *env, uint32_t arg) {
    for (;;) { desktop_refresh(); thread_sleep(20); }
    return 0;
}
void threads_init(env_vars_t *env) {
    threads[0] = (thread_t){.id = 0, .env = env, .state = RUNNABLE};
    for (uint32_t i = 0; i < 32; i++) mutexes[i].holder = -1;
    prepare(1, env, idle, 0, 0); prepare(2, env, desktop_worker, 0, 0);
    initialized = 1;
}
registers_t *threads_schedule(registers_t *frame, uint32_t ticks, int force) {
    if (!initialized) return frame;
    threads[current].frame = frame;
    for (uint32_t i = 1; i < THREAD_LIMIT; i++)
        if (threads[i].state == SLEEPING && (int32_t)(ticks - threads[i].deadline) >= 0)
            threads[i].state = RUNNABLE;
    if (threads[0].state == SLEEPING && (int32_t)(ticks - threads[0].deadline) >= 0)
        threads[0].state = RUNNABLE;
    if (!force && (ticks % 5 || (current == 2 && threads[current].guard))) return frame;
    uint32_t holder = THREAD_LIMIT;
    for (uint32_t i = 0; i < THREAD_LIMIT; i++) if (i != 1 && i != 2 &&
        threads[i].state == RUNNABLE && threads[i].guard) { holder = i; break; }
    for (uint32_t step = 1; step <= THREAD_LIMIT; step++) {
        uint32_t candidate = (current + step) % THREAD_LIMIT;
        if (candidate != 1 && threads[candidate].state == RUNNABLE &&
            (candidate == 2 || holder == THREAD_LIMIT || candidate == holder)) {
            current = candidate; return threads[current].frame;
        }
    }
    current = 1; return threads[1].frame;
}
int thread_start(env_vars_t *env, thread_entry_t entry, uint32_t arg) {
    if (!initialized || !entry || !threads[current].owner) return -1;
    uint32_t flags = irq_save(); int id = -1;
    for (uint32_t i = 3; i < THREAD_LIMIT; i++) if (threads[i].state == FREE) {
        prepare(i, env, entry, arg, threads[current].owner); id = threads[i].id; break;
    }
    irq_restore(flags); return id;
}
static thread_t *find(int id) {
    for (uint32_t i = 3; i < THREAD_LIMIT; i++)
        if (threads[i].state != FREE && threads[i].id == id && threads[i].owner == threads[current].owner)
            return &threads[i];
    return 0;
}
int thread_join(int id) {
    thread_t *t = find(id);
    if (!t || id == thread_id()) return -1;
    while (t->state != DONE) {
        thread_yield();
        if (t->id != id || t->state == FREE) return -1;
    }
    uint32_t flags = irq_save();
    if (t->state != DONE || t->id != id) { irq_restore(flags); return -1; }
    int result = t->result; t->state = FREE; irq_restore(flags); return result;
}
int thread_sleep(uint32_t milliseconds) {
    if (milliseconds > 60000) return -1;
    if (!initialized) return 0;
    if (!milliseconds) { thread_yield(); return 0; }
    uint32_t flags = irq_save();
    threads[current].deadline = (uint32_t)get_tick() + milliseconds;
    threads[current].state = SLEEPING; irq_restore(flags); thread_yield(); return 0;
}
uint32_t thread_scope_begin(void) {
    uint32_t previous = threads[current].owner;
    threads[current].owner = next_owner++; return previous;
}
void thread_scope_end(uint32_t previous) {
    uint32_t owner = threads[current].owner;
    for (;;) {
        uint32_t flags = irq_save();
        int active = 0;
        for (uint32_t i = 3; i < THREAD_LIMIT; i++) if (threads[i].owner == owner && threads[i].state != FREE) {
            if (threads[i].state == DONE) threads[i].state = FREE; else active = 1;
        }
        irq_restore(flags);
        if (!active) break;
        thread_yield();
    }
    for (uint32_t i = 0; i < 32; i++) if (mutexes[i].owner == owner) mutexes[i].id = 0;
    threads[current].owner = previous;
}
int thread_mutex_create(void) {
    uint32_t flags = irq_save(); int id = -1;
    if (threads[current].owner) for (uint32_t i = 0; i < 32; i++) if (!mutexes[i].id) {
        mutexes[i].id = id = next_mutex++; mutexes[i].owner = threads[current].owner; mutexes[i].holder = -1; break;
    }
    irq_restore(flags); return id;
}
int thread_mutex_lock(int id) {
    if (id <= 0) return -1;
    for (;;) {
        uint32_t flags = irq_save();
        for (uint32_t i = 0; i < 32; i++) if (mutexes[i].id == id && mutexes[i].owner == threads[current].owner) {
            if (mutexes[i].holder == thread_id()) { irq_restore(flags); return -1; }
            if (mutexes[i].holder < 0) { mutexes[i].holder = thread_id(); irq_restore(flags); return 0; }
            irq_restore(flags); thread_yield(); goto retry;
        }
        irq_restore(flags); return -1;
retry:;
    }
}
int thread_mutex_unlock(int id) {
    if (id <= 0) return -1;
    uint32_t flags = irq_save(); int result = -1;
    for (uint32_t i = 0; i < 32; i++) if (mutexes[i].id == id && mutexes[i].holder == thread_id()) {
        mutexes[i].holder = -1; result = 0; break;
    }
    irq_restore(flags); return result;
}
