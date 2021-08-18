#include "clevel.hpp"

#include <unistd.h>
#include <iterator>
#include <thread>
#include <vector>
#include <cstdio>
#include <fstream>
#include <utility>
#include <sys/file.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>

#include "Clevel-Hashing/include/libpmemobj++/make_persistent.hpp"
#include "Clevel-Hashing/include/libpmemobj++/p.hpp"
#include "Clevel-Hashing/include/libpmemobj++/persistent_ptr.hpp"
#include "Clevel-Hashing/include/libpmemobj++/pool.hpp"
#include "Clevel-Hashing/include/libpmemobj++/experimental/clevel_hash.hpp"

#define LAYOUT "clevel_hash"

namespace nvobj = pmem::obj;


typedef nvobj::experimental::clevel_hash<nvobj::p<uint64_t>, 
                                         nvobj::p<uint64_t>>
	persistent_map_type;

struct root {
	nvobj::persistent_ptr<persistent_map_type> cons;
};


// Static stuff
static bool isSetUp = false;
static nvobj::pool<root> pop;

static bool path_exists(const char *p) {
    std::ifstream f(p);
    return f.good();
}

int clevel_init(void) {
    if (isSetUp) return 0;

    const char path[] = "/mnt/pmem/clevel.pool";

    if (!path_exists(path)) {
        pop = nvobj::pool<root>::create(
            path, LAYOUT, PMEMOBJ_MIN_POOL * 20, S_IWUSR | S_IRUSR);
        auto proot = pop.root();

        nvobj::transaction::manual tx(pop);

        proot->cons = nvobj::make_persistent<persistent_map_type>();
        proot->cons->set_thread_num(1);

        nvobj::transaction::commit();
    } else {
		pop = nvobj::pool<root>::open(path, LAYOUT);
    }

    isSetUp = true;

    std::string cmd;
    cmd += "flock -u ";
    cmd += path;

#if 0
    int ret = system(cmd.c_str());
    if (ret != 0) {
        perror("could not unlock pool!");
        exit(-1);
    }
#elif 0
    int fd = open(path, O_RDWR);
    if (fd < 0) {
        perror("could not open pool!");
        exit(-1);
    }
    printf("Unlocking %s (fd=%d)\n", path, fd);
    int ret = flock(fd, LOCK_UN);
    if (ret != 0) {
        perror("could not unlock pool!");
        exit(-1);
    }
    (void)close(fd);
#endif
    return 0;
}

ssize_t clevel_lookup(inum_t inum, laddr_t blk, paddr_t *pblk) {
	auto map = pop.root()->cons;
    uint64_t key = ((uint64_t)inum << 32) | (uint64_t)blk;
    auto r = map->search(key);
    if (r.found) {
        auto level_ptr = map->meta(map->my_pool_uuid)->last_level;
        if (r.level_idx > 0) {
            level_ptr = map->meta(map->my_pool_uuid)->first_level;
        }
        auto &kv = map->get_entry(level_ptr, r.bucket_idx, r.slot_idx);
        /*
        auto &kv = map->get_entry(r.level_idx, r.bucket_idx, r.slot_idx);
        auto *ptr = kv(map->my_pool_uuid);
        assert(ptr);
        */
        *pblk = kv(map->my_pool_uuid)->second;
        return 1;
    }
    return 0;
}

ssize_t clevel_create(inum_t inum, laddr_t blk, paddr_t pblk) {
	auto map = pop.root()->cons;
    uint64_t key = ((uint64_t)inum << 32) | (uint64_t)blk;
	auto r = map->insert(persistent_map_type::value_type(key, pblk), 
                1, static_cast<size_t>(key));
    return !r.found;
}

ssize_t clevel_remove(inum_t inum, laddr_t blk) {
	auto map = pop.root()->cons;
    uint64_t key = ((uint64_t)inum << 32) | (uint64_t)blk;
    auto r = map->erase(key, 1);
    return r.found;
}
