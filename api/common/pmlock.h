#include <stdint.h>

typedef struct pmlock {
    uint8_t wrmutex;
    uint8_t has_writer;
    uint8_t rdmutex;
    uint8_t nreaders;
} pmlock_t;

static void pmlock_rd_lock(pmlock_t *l) {
    /*
    //printf("RD LOCK\n");
    //return;

    while (!__sync_bool_compare_and_swap(&l->rdmutex, 0, 1));

    while (__atomic_load_n(&l->has_writer, __ATOMIC_SEQ_CST));

    __atomic_add_fetch(&l->nreaders, 1, __ATOMIC_SEQ_CST);

    while (!__sync_bool_compare_and_swap(&l->rdmutex, 1, 0));
    */
}

static void pmlock_rd_unlock(pmlock_t *l) {
    /*
    //printf("RD UNLOCK\n");
    //return; 

    while (!__sync_bool_compare_and_swap(&l->rdmutex, 0, 1));

    __atomic_sub_fetch(&l->nreaders, 1, __ATOMIC_SEQ_CST);

    while (!__sync_bool_compare_and_swap(&l->rdmutex, 1, 0));
    */
}

static void pmlock_wr_lock(pmlock_t *l) {
    /*
    //printf("WR LOCK\n");
    //return; 

    // Probably a race condition
    while (__atomic_load_n(&l->has_writer, __ATOMIC_SEQ_CST));

    while (!__sync_bool_compare_and_swap(&l->wrmutex, 0, 1));

    // By doing this first, we can't get blocked by having so many readers
    __atomic_add_fetch(&l->has_writer, 1, __ATOMIC_SEQ_CST);

    while (__atomic_load_n(&l->nreaders, __ATOMIC_SEQ_CST) > 0);

    while (!__sync_bool_compare_and_swap(&l->wrmutex, 1, 0));
    */
}

static void pmlock_wr_unlock(pmlock_t *l) {
    /*
    //printf("WR UNLOCK\n");
    //return; 

    while (!__sync_bool_compare_and_swap(&l->rdmutex, 0, 1));

    __atomic_sub_fetch(&l->has_writer, 1, __ATOMIC_SEQ_CST);

    while (!__sync_bool_compare_and_swap(&l->rdmutex, 1, 0));
    */
}
