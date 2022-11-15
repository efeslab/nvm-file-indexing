#include "extents_test.hpp"

#include <map>
#include <iostream>
using namespace std;

bool operator== (const ext_meta_t& lhs, const ext_meta_t& rhs) {
    return lhs.et_direct_range == rhs.et_direct_range &&
           0 == strncmp((char*)lhs.et_direct_data, (char*)rhs.et_direct_data,
                        lhs.et_direct_range.pr_nbytes);
}

bool operator== (const idx_struct_t& lhs, const idx_struct_t& rhs) {
    EXTMETA(&lhs, lm);
    EXTMETA(&rhs, rm);
    return lhs.idx_callbacks   == rhs.idx_callbacks &&
           lhs.idx_mem_man     == rhs.idx_mem_man   &&
           lhs.idx_fns         == rhs.idx_fns       &&
           *lm == *rm;
}

INSTANTIATE_TEST_CASE_P(CacheState, 
                        ExtentTreeFixture,
                        ::testing::Values(false, true));


/*******************************************************************************
 * Section: Hashtable correctness tests.
 *
 * Here is where we make sure the hashtable functions properly.
 ******************************************************************************/
/*
 * HashTable initialization tests.
 */

TEST_P(ExtentTreeFixture, InitNew) {
    ASSERT_NE(nullptr, ext_idx.idx_metadata);
    ASSERT_EQ(0, init_err);

    EXTMETA(&ext_idx, ext_meta);
    ASSERT_NE(nullptr, ext_meta->et_direct_data);
}

TEST_P(ExtentTreeFixture, InitExists) {
    idx_struct_t ext_copy = ext_idx;
    int err = extent_tree_init(&idx_spec, &inode_space, &ext_copy);
    ASSERT_NE(0, err);
    ASSERT_EQ(ext_idx, ext_copy);
}

TEST_P(ExtentTreeFixture, InsertSingle) {
    inum_t inum   = 0;
    laddr_t lblk  = 0;
    paddr_t pblk  = 0;
    size_t npages = 1;

    ssize_t ret = extent_tree_create(&ext_idx, inum, lblk, npages, &pblk);
    ASSERT_EQ(npages, ret);
    ASSERT_GT(pblk, 0);
}

TEST_P(ExtentTreeFixture, InsertPersist) {
    inum_t inum   = 0;
    laddr_t lblk  = 0;
    paddr_t pblk  = 0;
    size_t npages = 1;

    ssize_t ret = extent_tree_create(&ext_idx, inum, lblk, npages, &pblk);
    ASSERT_EQ(npages, ret);
    ASSERT_GT(pblk, 0);

    idx_struct_t new_ext = {};
    int err = extent_tree_init(&idx_spec, &inode_space, &new_ext);

    ASSERT_EQ(ext_idx, new_ext);
}

TEST_P(ExtentTreeFixture, InsertMulti) {
    inum_t inum   = 0;
    laddr_t lblk  = 0;
    paddr_t pblk  = 0;
    size_t npages = 20;

    ssize_t ret = extent_tree_create(&ext_idx, inum, lblk, npages, &pblk);
    ASSERT_EQ(npages, ret);
    ASSERT_GT(pblk, 0);
}

TEST_P(ExtentTreeFixture, InsertFragmented) {
    inum_t inum   = 0;
    laddr_t lblk  = 0;
    paddr_t pblk1 = 0;
    paddr_t pblk2 = 0;
    size_t npages = 1;

    ssize_t ret = extent_tree_create(&ext_idx, inum, lblk, npages, &pblk1);
    ASSERT_EQ(npages, ret);
    ASSERT_GT(pblk1, 0);

    device.allocate(1);

    ret = extent_tree_create(&ext_idx, inum, lblk + 1, npages, &pblk2);
    ASSERT_EQ(npages, ret);
    ASSERT_GT(pblk2, 0);

    ASSERT_NE(pblk1, pblk2);
}

TEST_P(ExtentTreeFixture, InsertDeep) {
    inum_t inum   = 0;
    size_t npages = 1000;

    paddr_t prev_blk = -1;
    for(size_t i = 0; i < npages; ++i) {
        laddr_t lblk = (laddr_t)i;
        paddr_t pblk;
        ssize_t ret = extent_tree_create(&ext_idx, inum, lblk, 1, &pblk);
        ASSERT_EQ(1, ret);
        ASSERT_NE(prev_blk, pblk);

        prev_blk = pblk;
        device.allocate(1);
    }
}

TEST_P(ExtentTreeFixture, InsertDeepPersist) {
    inum_t inum   = 0;
    size_t npages = 1000;

    map<laddr_t, paddr_t> mapping;
    paddr_t prev_blk = -1;
    for(size_t i = 0; i < npages; ++i) {
        laddr_t lblk = (laddr_t)i;
        paddr_t pblk;
        ssize_t ret = extent_tree_create(&ext_idx, inum, lblk, 1, &pblk);
        ASSERT_EQ(1, ret);
        ASSERT_NE(prev_blk, pblk);
        mapping[lblk] = pblk;

        prev_blk = pblk;
        device.allocate(1);
    }

    idx_struct_t new_ext = {};
    int err = extent_tree_init(&idx_spec, &inode_space, &new_ext);
    for(size_t i = 0; i < npages; ++i) {
        laddr_t lblk = (laddr_t)i;
        paddr_t pblk;
        ssize_t ret = extent_tree_lookup(&new_ext, inum, lblk, 1, &pblk);
        ASSERT_EQ(1, ret);
        ASSERT_EQ(mapping[lblk], pblk);
    }
}

TEST_P(ExtentTreeFixture, InsertDeeperPersist) {
    inum_t inum   = 0;
    size_t npages = 100000;

    idx_struct_t new_ext = {};
    int err = extent_tree_init(&idx_spec, &inode_space, &new_ext);
    ASSERT_EQ(0, err);

    paddr_t prev_blk = -1;
    for(size_t i = 0; i < npages; ++i) {
        laddr_t lblk = (laddr_t)i;
        paddr_t pblk;
        ssize_t ret = extent_tree_create(&ext_idx, inum, lblk, 1, &pblk);
        ASSERT_EQ(1, ret);
        ASSERT_NE(prev_blk, pblk);

        prev_blk = pblk;
        device.allocate(1);
    }

    //Should caching be enabled.
    extent_tree_invalidate_caches(&new_ext);

    for(size_t i = 0; i < npages; ++i) {
        laddr_t lblk = (laddr_t)i;
        paddr_t pblk;
        ssize_t ret = extent_tree_lookup(&new_ext, inum, lblk, 1, &pblk);
        ASSERT_EQ(1, ret);
    }
}

TEST_P(ExtentTreeFixture, InsertDeepPersistStats) {
    inum_t inum   = 0;
    size_t npages = 1000;

    paddr_t prev_blk = -1;
    for(size_t i = 0; i < npages; ++i) {
        laddr_t lblk = (laddr_t)i;
        paddr_t pblk;
        ssize_t ret = extent_tree_create(&ext_idx, inum, lblk, 1, &pblk);
        ASSERT_EQ(1, ret);
        ASSERT_NE(prev_blk, pblk);

        prev_blk = pblk;
        device.allocate(1);
    }

    idx_struct_t new_ext = {};
    int err = extent_tree_init(&idx_spec, &inode_space, &new_ext);
    extent_tree_set_stats(&new_ext, true);
    for(size_t i = 0; i < npages; ++i) {
        laddr_t lblk = (laddr_t)i;
        paddr_t pblk;
        ssize_t ret = extent_tree_lookup(&new_ext, inum, lblk, 1, &pblk);
        ASSERT_EQ(1, ret);
    }
    extent_tree_print_stats(&new_ext);
}

TEST_P(ExtentTreeFixture, InsertRepeat) {
    inum_t inum   = 0;
    laddr_t lblk  = 0;
    paddr_t pblk  = 0;
    size_t npages = 1;

    ssize_t ret = extent_tree_create(&ext_idx, inum, lblk, npages, &pblk);
    ASSERT_EQ(npages, ret);
    ASSERT_GT(pblk, 0);

    size_t nalloced_total = device.num_allocated();

    ssize_t err = extent_tree_create(&ext_idx, inum, lblk, npages, &pblk);
    ASSERT_EQ(npages, err);
    ASSERT_GT(pblk, 0);
    ASSERT_EQ(nalloced_total, device.num_allocated()) <<
        "Error: new blocks mistakenly allocated for existing logical block.";
}

TEST_P(ExtentTreeFixture, LookupSingle) {
    inum_t inum   = 0;
    laddr_t lblk  = 0;
    paddr_t pblk  = 0;
    size_t npages = 1;

    ssize_t ret = extent_tree_create(&ext_idx, inum, lblk, npages, &pblk);
    ASSERT_EQ(npages, ret);
    ASSERT_GT(pblk, 0);

    paddr_t check_paddr;
    ssize_t check_size = extent_tree_lookup(&ext_idx, inum, lblk, npages, &check_paddr);
    ASSERT_EQ(pblk, check_paddr);
    ASSERT_EQ(npages, check_size);
}

TEST_P(ExtentTreeFixture, LookupMulti) {
    inum_t inum   = 0;
    laddr_t lblk  = 0;
    paddr_t pblk  = 0;
    size_t npages = 20;

    ssize_t ret = extent_tree_create(&ext_idx, inum, lblk, npages, &pblk);
    ASSERT_EQ(npages, ret);
    ASSERT_GT(pblk, 0);

    for (size_t p = 0; p < npages; ++p) {
        paddr_t check_paddr;
        ssize_t check_size = extent_tree_lookup(&ext_idx, inum, lblk + p, npages - p, &check_paddr);
        ASSERT_EQ(pblk + p, check_paddr);
        ASSERT_EQ(npages - p, check_size);
    }
}

TEST_P(ExtentTreeFixture, RemoveSingle) {
    inum_t inum   = 0;
    laddr_t lblk  = 0;
    paddr_t pblk  = 0;
    size_t npages = 1;

    ssize_t ret = extent_tree_create(&ext_idx, inum, lblk, npages, &pblk);
    ASSERT_EQ(npages, ret);
    ASSERT_GT(pblk, 0);

    ssize_t check_size = extent_tree_remove(&ext_idx, inum, lblk, npages);
    ASSERT_EQ(npages, check_size);

    paddr_t check_paddr = 0;
    check_size = extent_tree_lookup(&ext_idx, inum, lblk, npages, &check_paddr);
    ASSERT_NE(pblk, check_paddr);
    ASSERT_NE(npages, check_size);
    ASSERT_LE(check_size, 0);
}

TEST_P(ExtentTreeFixture, RemoveSingleReinsert) {
    inum_t inum   = 0;
    laddr_t lblk  = 0;
    paddr_t pblk  = 0;
    size_t npages = 1;

    ssize_t ret = extent_tree_create(&ext_idx, inum, lblk, npages, &pblk);
    ASSERT_EQ(npages, ret);
    ASSERT_GT(pblk, 0);

    ssize_t check_size = extent_tree_remove(&ext_idx, inum, lblk, npages);
    ASSERT_EQ(npages, check_size);

    paddr_t check_paddr = 0;
    check_size = extent_tree_lookup(&ext_idx, inum, lblk, npages, &check_paddr);
    ASSERT_NE(pblk, check_paddr);
    ASSERT_NE(npages, check_size);
    ASSERT_LE(check_size, 0);

    ret = extent_tree_create(&ext_idx, inum, lblk, npages, &pblk);
    ASSERT_EQ(npages, ret);
    ASSERT_GT(pblk, 0);
}

TEST_P(ExtentTreeFixture, EraseMulti) {
    inum_t inum   = 0;
    laddr_t lblk  = 0;
    paddr_t pblk  = 0;
    size_t npages = 20;

    ssize_t ret = extent_tree_create(&ext_idx, inum, lblk, npages, &pblk);
    ASSERT_EQ(npages, ret);
    ASSERT_GT(pblk, 0);

    ssize_t check_size = extent_tree_remove(&ext_idx, inum, lblk, npages);
    ASSERT_EQ(npages, check_size);

    paddr_t check_paddr = 0;
    check_size = extent_tree_lookup(&ext_idx, inum, lblk, npages, &check_paddr);
    ASSERT_NE(pblk, check_paddr);
    ASSERT_NE(npages, check_size);
    ASSERT_LE(check_size, 0);
}

TEST_P(ExtentTreeFixture, EraseDeep) {
    inum_t inum   = 0;
    size_t npages = 1000;

    paddr_t prev_blk = -1;
    for(size_t i = 0; i < npages; ++i) {
        laddr_t lblk = (laddr_t)i;
        paddr_t pblk;
        ssize_t ret = extent_tree_create(&ext_idx, inum, lblk, 1, &pblk);
        ASSERT_EQ(1, ret);
        ASSERT_NE(prev_blk, pblk);

        prev_blk = pblk;
        device.allocate(1);
    }

    ssize_t check_size = extent_tree_remove(&ext_idx, inum, 0, npages);
    ASSERT_EQ(npages, check_size);
}

TEST_P(ExtentTreeFixture, EraseDeallocate) {
    inum_t inum   = 0;
    laddr_t lblk  = 0;
    paddr_t pblk  = 0;
    size_t npages = 20;

    size_t nalloced_meta_only = device.num_allocated();

    ssize_t ret = extent_tree_create(&ext_idx, inum, lblk, npages, &pblk);
    ASSERT_EQ(npages, ret);
    ASSERT_GT(pblk, 0);

    ssize_t check_size = extent_tree_remove(&ext_idx, inum, lblk, npages);
    ASSERT_EQ(npages, check_size);
    ASSERT_EQ(nalloced_meta_only, device.num_allocated());

    paddr_t check_paddr = 0;
    check_size = extent_tree_lookup(&ext_idx, inum, lblk, npages, &check_paddr);
    ASSERT_NE(pblk, check_paddr);
    ASSERT_NE(npages, check_size);
    ASSERT_LE(check_size, 0);
}
