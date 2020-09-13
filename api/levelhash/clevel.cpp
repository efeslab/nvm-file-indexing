#include "clevel.hpp"


#include <iterator>
#include <thread>
#include <vector>
#include <cstdio>

#include "Clevel-Hashing/include/libpmemobj++/make_persistent.hpp"
#include "Clevel-Hashing/include/libpmemobj++/p.hpp"
#include "Clevel-Hashing/include/libpmemobj++/persistent_ptr.hpp"
#include "Clevel-Hashing/include/libpmemobj++/pool.hpp"
#include "Clevel-Hashing/include/libpmemobj++/experimental/clevel_hash.hpp"

int clevel_init(void **clevel) {
    return 0;
}

ssize_t clevel_lookup(void *clevel, laddr_t blk, paddr_t *pblk) {
    return 0;
}

ssize_t clevel_create(void *clevel, laddr_t blk, paddr_t *pblk, size_t n) {
    return 0;
}

ssize_t clevel_remove(void *clevel, laddr_t blk, size_t n) {
    return 0;
}
