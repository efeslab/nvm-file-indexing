#include "cuckoohash_test.hpp"

#include <map>

using namespace std;

/*******************************************************************************
 * Section: Hashtable correctness tests.
 *
 * Here is where we make sure the hashtable functions properly.
 ******************************************************************************/

/*
 * HashTable initialization tests.
 */

TEST_F(CuckooHashFixture, InitNew) {
    ASSERT_EQ(0, init_err);
    ASSERT_NE(cht.idx_metadata, nullptr);
    ASSERT_LT(metadata_loc, NBLK);
}

TEST_F(CuckooHashFixture, InitExists) {
    idx_struct_t hash_copy = cht;
    ASSERT_NE(hash_copy.idx_metadata, nullptr);
    int err = cuckoohash_initialize(&idx_spec, &hash_copy, &metadata_loc);
    ASSERT_TRUE(err);
    ASSERT_EQ(0, strncmp((char*)&hash_copy, (char*)&cht, sizeof(cht)));
}

TEST_F(CuckooHashFixture, InsertSingle) {
    inum_t inum   = 0;
    laddr_t lblk  = 0;
    paddr_t pblk  = 0;
    size_t npages = 1;

    ssize_t ret = cuckoohash_create(&cht, inum, lblk, npages, &pblk);
    ASSERT_EQ(npages, ret) << strerror(-ret);
    ASSERT_GT(pblk, 0);
}

TEST_F(CuckooHashFixture, InsertRepeat) {
    inum_t inum   = 0;
    laddr_t lblk  = 0;
    paddr_t pblk  = 0;
    size_t npages = 1;

    ssize_t ret = cuckoohash_create(&cht, inum, lblk, npages, &pblk);
    ASSERT_EQ(npages, ret);
    ASSERT_GT(pblk, 0);

    paddr_t lookup_paddr;
    ret = cuckoohash_create(&cht, inum, lblk, npages, &lookup_paddr);
    ASSERT_LT(ret, 0);
    ASSERT_EQ(0, lookup_paddr);
}

TEST_F(CuckooHashFixture, InsertMulti) {
    inum_t inum   = 0;
    laddr_t lblk  = 0;
    paddr_t pblk  = 0;
    size_t npages = 20;

    ssize_t ret = cuckoohash_create(&cht, inum, lblk, npages, &pblk);
    ASSERT_EQ(npages, ret);
    ASSERT_GT(pblk, 0);
}

TEST_F(CuckooHashFixture, LookupSingle) {
    inum_t inum   = 0;
    laddr_t lblk  = 0;
    paddr_t pblk  = 0;
    size_t npages = 1;

    ssize_t ret = cuckoohash_create(&cht, inum, lblk, npages, &pblk);
    ASSERT_EQ(npages, ret);
    ASSERT_GT(pblk, 0);

    paddr_t check_paddr;
    ssize_t check_size = cuckoohash_lookup(&cht, inum, lblk, npages, &check_paddr);
    ASSERT_EQ(pblk, check_paddr);
    ASSERT_EQ(npages, check_size);
}

TEST_F(CuckooHashFixture, LookupMulti) {
    inum_t inum   = 0;
    laddr_t lblk  = 0;
    paddr_t pblk  = 0;
    size_t npages = 20;

    ssize_t ret = cuckoohash_create(&cht, inum, lblk, npages, &pblk);
    ASSERT_EQ(npages, ret);
    ASSERT_GT(pblk, 0);

    for (size_t p = 0; p < npages; ++p) {
        paddr_t check_paddr;
        ssize_t check_size = cuckoohash_lookup(&cht, inum, lblk + p, npages - p, &check_paddr);
        ASSERT_EQ(pblk + p, check_paddr);
        ASSERT_EQ(npages - p, check_size);
    }
}

TEST_F(CuckooHashFixture, LookupMultiSIMD) {
    inum_t inum   = 0;
    laddr_t lblk  = 0;
    paddr_t pblk  = 0;
    size_t npages = 8;

    ssize_t ret = cuckoohash_create(&cht, inum, lblk, npages, &pblk);
    ASSERT_EQ(npages, ret);
    ASSERT_GT(pblk, 0);

    for (size_t p = 0; p < npages; p += 8) {
        paddr_t check_paddr[8];
        ssize_t check_size = cuckoohash_lookup_parallel(&cht, inum, lblk + p, 8, check_paddr);
        ASSERT_EQ(8, check_size);

        for (int i = 0; i < 8; ++i) {
            ASSERT_EQ(pblk + p + i, check_paddr[i]);
        }
    }
}

TEST_F(CuckooHashFixture, LookupMultiSIMDMany) {
    inum_t inum   = 0;
    laddr_t lblk  = 0;
    paddr_t pblk  = 0;
    size_t npages = 80;

    ssize_t ret = cuckoohash_create(&cht, inum, lblk, npages, &pblk);
    ASSERT_EQ(npages, ret);
    ASSERT_GT(pblk, 0);

    for (size_t p = 0; p < npages; p += 8) {
        paddr_t check_paddr[8];
        ssize_t check_size = cuckoohash_lookup_parallel(&cht, inum, lblk + p, 8, check_paddr);
        ASSERT_EQ(8, check_size);

        for (int i = 0; i < 8; ++i) {
            ASSERT_EQ(pblk + p + i, check_paddr[i]);
        }
    }
}

TEST_F(CuckooHashFixture, EraseSingle) {
    inum_t inum   = 0;
    laddr_t lblk  = 0;
    paddr_t pblk  = 0;
    size_t npages = 1;

    ssize_t ret = cuckoohash_create(&cht, inum, lblk, npages, &pblk);
    ASSERT_EQ(npages, ret);
    ASSERT_GT(pblk, 0);

    ssize_t check_size = cuckoohash_remove(&cht, inum, lblk, npages);
    ASSERT_EQ(npages, check_size);

    paddr_t check_paddr = 0;
    check_size = cuckoohash_lookup(&cht, inum, lblk, npages, &check_paddr);
    ASSERT_NE(pblk, check_paddr);
    ASSERT_NE(npages, check_size);
    ASSERT_LE(check_size, 0);
}

TEST_F(CuckooHashFixture, EraseRecreate) {
    inum_t inum   = 0;
    laddr_t lblk  = 0;
    paddr_t pblk  = 0;
    size_t npages = 10;

    ssize_t ret = cuckoohash_create(&cht, inum, lblk, npages, &pblk);
    ASSERT_EQ(npages, ret);
    ASSERT_GT(pblk, 0);

    ssize_t check_size = cuckoohash_remove(&cht, inum, lblk, npages);
    ASSERT_EQ(npages, check_size);

    paddr_t check_paddr = 0;
    check_size = cuckoohash_lookup(&cht, inum, lblk, npages, &check_paddr);
    ASSERT_NE(pblk, check_paddr);
    ASSERT_NE(npages, check_size);
    ASSERT_LE(check_size, 0);

    ret = cuckoohash_create(&cht, inum, lblk, npages, &pblk);
    ASSERT_EQ(npages, ret) << strerror(ret);
    ASSERT_GT(pblk, 0);
}

TEST_F(CuckooHashFixture, EraseMulti) {
    inum_t inum   = 0;
    laddr_t lblk  = 0;
    paddr_t pblk  = 0;
    size_t npages = 20;

    ssize_t ret = cuckoohash_create(&cht, inum, lblk, npages, &pblk);
    ASSERT_EQ(npages, ret);
    ASSERT_GT(pblk, 0);

    ssize_t check_size = cuckoohash_remove(&cht, inum, lblk, npages);
    ASSERT_EQ(npages, check_size);

    paddr_t check_paddr = 0;
    check_size = cuckoohash_lookup(&cht, inum, lblk, npages, &check_paddr);
    ASSERT_NE(pblk, check_paddr);
    ASSERT_NE(npages, check_size);
    ASSERT_LE(check_size, 0);
}

TEST_F(CuckooHashFixture, ErasePartial) {
    inum_t inum    = 0;
    paddr_t pblk   = 0;
    size_t npages  = 20;
    size_t nremove = 7;

    ssize_t ret = cuckoohash_create(&cht, inum, 0, npages, &pblk);
    ASSERT_EQ(npages, ret);
    ASSERT_GT(pblk, 0);

    ssize_t check_size = cuckoohash_remove(&cht, inum, 
                                          (npages - nremove), nremove);
    ASSERT_EQ(nremove, check_size);

    for (laddr_t l = 0; l < (npages - nremove); ++l) {
        paddr_t check_paddr = 0;
        check_size = cuckoohash_lookup(&cht, inum, l, 
                                      npages - nremove - l, &check_paddr);
        ASSERT_EQ(pblk + l, check_paddr);
        ASSERT_EQ(npages - nremove - l, check_size);
    }

    for (laddr_t l = (npages - nremove); l < npages; ++l) {
        paddr_t check_paddr = 0;
        check_size = cuckoohash_lookup(&cht, inum, l, 1, &check_paddr);
        ASSERT_LT(check_size, 0);
        ASSERT_EQ(0, check_paddr);
    }
}

TEST_F(CuckooHashFixture, EraseDeallocate) {
    inum_t inum   = 0;
    laddr_t lblk  = 0;
    paddr_t pblk  = 0;
    size_t npages = 20;

    size_t nalloced_meta_only = device.num_allocated();

    ssize_t ret = cuckoohash_create(&cht, inum, lblk, npages, &pblk);
    ASSERT_EQ(npages, ret);
    ASSERT_GT(pblk, 0);

    ssize_t check_size = cuckoohash_remove(&cht, inum, lblk, npages);
    ASSERT_EQ(npages, check_size);
    ASSERT_EQ(nalloced_meta_only, device.num_allocated());

    paddr_t check_paddr = 0;
    check_size = cuckoohash_lookup(&cht, inum, lblk, npages, &check_paddr);
    ASSERT_NE(pblk, check_paddr);
    ASSERT_NE(npages, check_size);
    ASSERT_LE(check_size, 0);
}
