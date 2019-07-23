#include "hashtable_test.hpp"

using namespace std;

/*******************************************************************************
 * Section: Hashtable correctness tests.
 *
 * Here is where we make sure the hashtable functions properly.
 ******************************************************************************/

/*
 * HashTable initialization tests.
 */

TEST_F(HashTableFixture, InitNew) {
    ASSERT_NE(hashtable.idx_metadata, nullptr);
    ASSERT_LT(metadata_loc, NBLK);
    ASSERT_EQ(0, init_err);
}

TEST_F(HashTableFixture, InitExists) {
    idx_struct_t hash_copy = hashtable;
    ASSERT_NE(hash_copy.idx_metadata, nullptr);
    int err = hashtable_initialize(&idx_spec, &hash_copy, &metadata_loc);
    ASSERT_TRUE(err);
    ASSERT_EQ(0, strncmp((char*)&hash_copy, (char*)&hashtable, sizeof(hashtable)));
}

TEST_F(HashTableFixture, InsertSingle) {
    inum_t inum   = 0;
    laddr_t lblk  = 0;
    paddr_t pblk  = 0;
    size_t npages = 1;

    ssize_t ret = hashtable_create(&hashtable, inum, lblk, npages, &pblk);
    ASSERT_EQ(npages, ret);
    ASSERT_GT(pblk, 0);
}

TEST_F(HashTableFixture, InsertRepeat) {
    inum_t inum   = 0;
    laddr_t lblk  = 0;
    paddr_t pblk  = 0;
    size_t npages = 1;

    ssize_t ret = hashtable_create(&hashtable, inum, lblk, npages, &pblk);
    ASSERT_EQ(npages, ret);
    ASSERT_GT(pblk, 0);

    paddr_t lookup_paddr;
    ret = hashtable_create(&hashtable, inum, lblk, npages, &lookup_paddr);
    ASSERT_LT(ret, 0);
    ASSERT_EQ(0, lookup_paddr);
}

TEST_F(HashTableFixture, InsertMulti) {
    inum_t inum   = 0;
    laddr_t lblk  = 0;
    paddr_t pblk  = 0;
    size_t npages = 20;

    ssize_t ret = hashtable_create(&hashtable, inum, lblk, npages, &pblk);
    ASSERT_EQ(npages, ret);
    ASSERT_GT(pblk, 0);
}

TEST_F(HashTableFixture, LookupSingle) {
    inum_t inum   = 0;
    laddr_t lblk  = 0;
    paddr_t pblk  = 0;
    size_t npages = 1;

    ssize_t ret = hashtable_create(&hashtable, inum, lblk, npages, &pblk);
    ASSERT_EQ(npages, ret);
    ASSERT_GT(pblk, 0);

    paddr_t check_paddr;
    ssize_t check_size = hashtable_lookup(&hashtable, inum, lblk, npages, &check_paddr);
    ASSERT_EQ(pblk, check_paddr);
    ASSERT_EQ(npages, check_size);
}

TEST_F(HashTableFixture, LookupMulti) {
    inum_t inum   = 0;
    laddr_t lblk  = 0;
    paddr_t pblk  = 0;
    size_t npages = 20;

    ssize_t ret = hashtable_create(&hashtable, inum, lblk, npages, &pblk);
    ASSERT_EQ(npages, ret);
    ASSERT_GT(pblk, 0);

    for (size_t p = 0; p < npages; ++p) {
        paddr_t check_paddr;
        ssize_t check_size = hashtable_lookup(&hashtable, inum, lblk + p, npages - p, &check_paddr);
        ASSERT_EQ(pblk + p, check_paddr);
        ASSERT_EQ(npages - p, check_size);
    }
}

TEST_F(HashTableFixture, EraseSingle) {
    inum_t inum   = 0;
    laddr_t lblk  = 0;
    paddr_t pblk  = 0;
    size_t npages = 1;

    ssize_t ret = hashtable_create(&hashtable, inum, lblk, npages, &pblk);
    ASSERT_EQ(npages, ret);
    ASSERT_GT(pblk, 0);

    ssize_t check_size = hashtable_remove(&hashtable, inum, lblk, npages);
    ASSERT_EQ(npages, check_size);

    paddr_t check_paddr = 0;
    check_size = hashtable_lookup(&hashtable, inum, lblk, npages, &check_paddr);
    ASSERT_NE(pblk, check_paddr);
    ASSERT_NE(npages, check_size);
    ASSERT_LE(check_size, 0);
}

TEST_F(HashTableFixture, EraseRecreate) {
    inum_t inum   = 0;
    laddr_t lblk  = 0;
    paddr_t pblk  = 0;
    size_t npages = 10;

    ssize_t ret = hashtable_create(&hashtable, inum, lblk, npages, &pblk);
    ASSERT_EQ(npages, ret);
    ASSERT_GT(pblk, 0);

    ssize_t check_size = hashtable_remove(&hashtable, inum, lblk, npages);
    ASSERT_EQ(npages, check_size);

    paddr_t check_paddr = 0;
    check_size = hashtable_lookup(&hashtable, inum, lblk, npages, &check_paddr);
    ASSERT_NE(pblk, check_paddr);
    ASSERT_NE(npages, check_size);
    ASSERT_LE(check_size, 0);

    ret = hashtable_create(&hashtable, inum, lblk, npages, &pblk);
    ASSERT_EQ(npages, ret);
    ASSERT_GT(pblk, 0);
}

TEST_F(HashTableFixture, EraseMulti) {
    inum_t inum   = 0;
    laddr_t lblk  = 0;
    paddr_t pblk  = 0;
    size_t npages = 20;

    ssize_t ret = hashtable_create(&hashtable, inum, lblk, npages, &pblk);
    ASSERT_EQ(npages, ret);
    ASSERT_GT(pblk, 0);

    ssize_t check_size = hashtable_remove(&hashtable, inum, lblk, npages);
    ASSERT_EQ(npages, check_size);

    paddr_t check_paddr = 0;
    check_size = hashtable_lookup(&hashtable, inum, lblk, npages, &check_paddr);
    ASSERT_NE(pblk, check_paddr);
    ASSERT_NE(npages, check_size);
    ASSERT_LE(check_size, 0);
}

TEST_F(HashTableFixture, ErasePartial) {
    inum_t inum    = 0;
    paddr_t pblk   = 0;
    size_t npages  = 20;
    size_t nremove = 7;

    ssize_t ret = hashtable_create(&hashtable, inum, 0, npages, &pblk);
    ASSERT_EQ(npages, ret);
    ASSERT_GT(pblk, 0);

    ssize_t check_size = hashtable_remove(&hashtable, inum, 
                                          (npages - nremove), nremove);
    ASSERT_EQ(nremove, check_size);

    for (laddr_t l = 0; l < (npages - nremove); ++l) {
        paddr_t check_paddr = 0;
        check_size = hashtable_lookup(&hashtable, inum, l, 
                                      npages - nremove - l, &check_paddr);
        ASSERT_EQ(pblk + l, check_paddr);
        ASSERT_EQ(npages - nremove - l, check_size);
    }

    for (laddr_t l = (npages - nremove); l < npages; ++l) {
        paddr_t check_paddr = 0;
        check_size = hashtable_lookup(&hashtable, inum, l, 1, &check_paddr);
        ASSERT_LT(check_size, 0);
        ASSERT_EQ(0, check_paddr);
    }
}

TEST_F(HashTableFixture, EraseDeallocate) {
    inum_t inum   = 0;
    laddr_t lblk  = 0;
    paddr_t pblk  = 0;
    size_t npages = 20;

    size_t nalloced_meta_only = device.num_allocated();

    ssize_t ret = hashtable_create(&hashtable, inum, lblk, npages, &pblk);
    ASSERT_EQ(npages, ret);
    ASSERT_GT(pblk, 0);

    ssize_t check_size = hashtable_remove(&hashtable, inum, lblk, npages);
    ASSERT_EQ(npages, check_size);
    ASSERT_EQ(nalloced_meta_only, device.num_allocated());

    paddr_t check_paddr = 0;
    check_size = hashtable_lookup(&hashtable, inum, lblk, npages, &check_paddr);
    ASSERT_NE(pblk, check_paddr);
    ASSERT_NE(npages, check_size);
    ASSERT_LE(check_size, 0);
}
