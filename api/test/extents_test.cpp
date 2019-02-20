#include "extents_test.hpp"


/*******************************************************************************
 * Section: Hashtable correctness tests.
 *
 * Here is where we make sure the hashtable functions properly.
 ******************************************************************************/

/*
 * HashTable initialization tests.
 */

TEST_F(ExtentTreeFixture, InitNew) {
    ASSERT_NE(nullptr, ext_idx.idx_metadata);
    ASSERT_EQ(0, init_err);

    EXTMETA(&ext_idx, ext_meta);
    ASSERT_NE(nullptr, ext_meta->et_direct_data);
}

TEST_F(ExtentTreeFixture, InitExists) {
    idx_struct_t ext_copy = ext_idx;
    int err = extent_tree_init(&idx_spec, &inode_space, &ext_copy);
    ASSERT_NE(0, err);
    ASSERT_EQ(0, strncmp((char*)&ext_copy, (char*)&ext_idx, sizeof(ext_idx)));
}

TEST_F(ExtentTreeFixture, InsertSingle) {
    inum_t inum   = 0;
    laddr_t lblk  = 0;
    paddr_t pblk  = 0;
    size_t npages = 1;
    size_t blk_sz = BLK_SZ;

    ssize_t ret = extent_tree_create(&ext_idx, inum, lblk, npages, &pblk);
    ASSERT_EQ(npages, ret);
}

TEST_F(ExtentTreeFixture, InsertMulti) {
    inum_t inum   = 0;
    laddr_t lblk  = 0;
    paddr_t pblk  = 0;
    size_t npages = 20;
    size_t blk_sz = BLK_SZ;

    ssize_t ret = extent_tree_create(&ext_idx, inum, lblk, npages, &pblk);
    ASSERT_EQ(npages, ret);
}

TEST_F(ExtentTreeFixture, LookupSingle) {
    inum_t inum   = 0;
    laddr_t lblk  = 0;
    paddr_t pblk  = 0;
    size_t npages = 1;
    size_t blk_sz = BLK_SZ;

    ssize_t ret = extent_tree_create(&ext_idx, inum, lblk, npages, &pblk);
    ASSERT_EQ(npages, ret);

    paddr_t check_paddr;
    ssize_t check_size = extent_tree_lookup(&ext_idx, inum, lblk, &check_paddr);
    ASSERT_EQ(pblk, check_paddr);
    ASSERT_EQ(npages, check_size);
}

TEST_F(ExtentTreeFixture, LookupMulti) {
    inum_t inum   = 0;
    laddr_t lblk  = 0;
    paddr_t pblk  = 0;
    size_t npages = 20;
    size_t blk_sz = BLK_SZ;

    ssize_t ret = extent_tree_create(&ext_idx, inum, lblk, npages, &pblk);
    ASSERT_EQ(npages, ret);

    for (size_t p = 0; p < npages; ++p) {
        paddr_t check_paddr;
        ssize_t check_size = extent_tree_lookup(&ext_idx, inum, lblk + p, &check_paddr);
        ASSERT_EQ(pblk + p, check_paddr);
        ASSERT_EQ(npages - p, check_size);
    }
}

TEST_F(ExtentTreeFixture, RemoveSingle) {
    inum_t inum   = 0;
    laddr_t lblk  = 0;
    paddr_t pblk  = 0;
    size_t npages = 1;
    size_t blk_sz = BLK_SZ;

    ssize_t ret = extent_tree_create(&ext_idx, inum, lblk, npages, &pblk);
    ASSERT_EQ(npages, ret);

    ssize_t check_size = extent_tree_remove(&ext_idx, inum, lblk, npages);
    ASSERT_EQ(npages, check_size);

    paddr_t check_paddr = 0;
    check_size = extent_tree_lookup(&ext_idx, inum, lblk, &check_paddr);
    ASSERT_NE(pblk, check_paddr);
    ASSERT_NE(npages, check_size);
    ASSERT_LE(check_size, 0);
}

TEST_F(ExtentTreeFixture, EraseMulti) {
    inum_t inum   = 0;
    laddr_t lblk  = 0;
    paddr_t pblk  = 0;
    size_t npages = 20;
    size_t blk_sz = BLK_SZ;

    ssize_t ret = extent_tree_create(&ext_idx, inum, lblk, npages, &pblk);
    ASSERT_EQ(npages, ret);

    ssize_t check_size = extent_tree_remove(&ext_idx, inum, lblk, npages);
    ASSERT_EQ(npages, check_size);

    paddr_t check_paddr = 0;
    check_size = extent_tree_lookup(&ext_idx, inum, lblk, &check_paddr);
    ASSERT_NE(pblk, check_paddr);
    ASSERT_NE(npages, check_size);
    ASSERT_LE(check_size, 0);
}

TEST_F(ExtentTreeFixture, EraseDeallocate) {
    inum_t inum   = 0;
    laddr_t lblk  = 0;
    paddr_t pblk  = 0;
    size_t npages = 20;
    size_t blk_sz = BLK_SZ;

    size_t nalloced_meta_only = device.num_allocated();

    ssize_t ret = extent_tree_create(&ext_idx, inum, lblk, npages, &pblk);
    ASSERT_EQ(npages, ret);

    ssize_t check_size = extent_tree_remove(&ext_idx, inum, lblk, npages);
    ASSERT_EQ(npages, check_size);
    ASSERT_EQ(nalloced_meta_only, device.num_allocated());

    paddr_t check_paddr = 0;
    check_size = extent_tree_lookup(&ext_idx, inum, lblk, &check_paddr);
    ASSERT_NE(pblk, check_paddr);
    ASSERT_NE(npages, check_size);
    ASSERT_LE(check_size, 0);
}
