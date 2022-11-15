#include <stdint.h>

typedef struct pmlock {
    uint8_t has_writer;
    uint8_t mutex;
    uint8_t nreaders;
} pmlock_t;
#if 0
static void pmlock_rd_lock(pmlock_t *l) {
    printf("RD LOCK\n");
    //return;

    while (!__sync_bool_compare_and_swap(&l->mutex, 0, 1));

    while (__atomic_load_n(&l->has_writer, __ATOMIC_SEQ_CST));

    __atomic_add_fetch(&l->nreaders, 1, __ATOMIC_SEQ_CST);

    while (!__sync_bool_compare_and_swap(&l->mutex, 1, 0));
    printf("--- RD LOCK\n");
}

static void pmlock_rd_unlock(pmlock_t *l) {
    printf("RD UNLOCK\n");
    //return; 

    //while (!__sync_bool_compare_and_swap(&l->mutex, 0, 1));

    __atomic_sub_fetch(&l->nreaders, 1, __ATOMIC_SEQ_CST);

    //while (!__sync_bool_compare_and_swap(&l->mutex, 1, 0));
    printf("--- RD UNLOCK\n");
}

static void pmlock_wr_lock(pmlock_t *l) {
    printf("WR LOCK\n");
    //return; 

    while (!__sync_bool_compare_and_swap(&l->mutex, 0, 1));

    // By doing this first, we can't get blocked by having so many readers
    while (!__sync_bool_compare_and_swap(&l->has_writer, 0, 1));

    while (__atomic_load_n(&l->nreaders, __ATOMIC_SEQ_CST) > 0);

    while (!__sync_bool_compare_and_swap(&l->mutex, 1, 0));

    printf("--- WR LOCK\n");
}

static void pmlock_wr_unlock(pmlock_t *l) {
    printf("WR UNLOCK\n");
    //return; 

    //while (!__sync_bool_compare_and_swap(&l->mutex, 0, 1));

    __sync_bool_compare_and_swap(&l->has_writer, 1, 0);
    //if_then_panic(!__sync_bool_compare_and_swap(&l->has_writer, 1, 0), "WRITE RACE");

    //while (!__sync_bool_compare_and_swap(&l->mutex, 1, 0));
    printf("--- WR UNLOCK\n");
}
#elif 0
static void pmlock_rd_lock(pmlock_t *l) {
    printf("RD LOCK\n");
    while (!__sync_bool_compare_and_swap(&l->mutex, 0, 1));
    printf("--- RD LOCK\n");
}

static void pmlock_rd_unlock(pmlock_t *l) {
    printf("RD UNLOCK\n");
    while (!__sync_bool_compare_and_swap(&l->mutex, 1, 0));
    printf("--- RD UNLOCK\n");
}

static void pmlock_wr_lock(pmlock_t *l) {
    printf("WR LOCK\n");
    while (!__sync_bool_compare_and_swap(&l->mutex, 0, 1));
    printf("--- WR LOCK\n");
}

static void pmlock_wr_unlock(pmlock_t *l) {
    printf("WR UNLOCK\n");
    while (!__sync_bool_compare_and_swap(&l->mutex, 1, 0));
    printf("--- WR UNLOCK\n");
}
#else

#define pmlock_rd_lock(l) 0
#define pmlock_rd_unlock(l) 0
#define pmlock_wr_lock(l) 0
#define pmlock_wr_unlock(l) 0

#endif
