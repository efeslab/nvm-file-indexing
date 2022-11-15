#include "radixtree_test.hpp"

using namespace std;

/*******************************************************************************
 * Section: Radix tree correctness tests.
 *
 * Here is where we make sure the radix tree functions properly.
 ******************************************************************************/

/*
 * HashTable initialization tests.
 */

TEST_F(RadixTreeFixture, InitNew) {
    ASSERT_EQ(0, init_err);
    ASSERT_NE(radixtree.idx_metadata, nullptr);
}

TEST_F(RadixTreeFixture, InitExists) {
    idx_struct_t idx_copy;
    int err = radixtree_init(&idx_spec, &inode_space, &idx_copy);
    ASSERT_EQ(0, err);
    ASSERT_EQ(0, strncmp((char*)idx_copy.idx_metadata, (char*)radixtree.idx_metadata, 
                sizeof(radixtree_meta_t)));
}

TEST_F(RadixTreeFixture, InsertSingle) {
    inum_t inum   = 0;
    laddr_t lblk  = 0;
    paddr_t pblk  = 0;
    size_t npages = 1;

    ssize_t ret = radixtree_create(&radixtree, inum, lblk, npages, &pblk);
    ASSERT_EQ(npages, ret);
    ASSERT_GT(pblk, 0);
}

TEST_F(RadixTreeFixture, LookupSingle) {
    inum_t inum   = 0;
    laddr_t lblk  = 0;
    paddr_t pblk  = 0;
    size_t npages = 1;

    ssize_t ret = radixtree_create(&radixtree, inum, lblk, npages, &pblk);
    ASSERT_EQ(npages, ret);
    ASSERT_GT(pblk, 0);

    paddr_t check_paddr = UINT64_MAX;
    ssize_t check_size = radixtree_lookup(&radixtree, inum, lblk, npages, &check_paddr);
    ASSERT_EQ(pblk, check_paddr);
    ASSERT_EQ(npages, check_size);
}

TEST_F(RadixTreeFixture, RemoveSingle) {
    inum_t inum   = 0;
    laddr_t lblk  = 0;
    paddr_t pblk  = 0;
    size_t npages = 1;
    
    ssize_t nalloc_start = device.num_allocated();

    ssize_t ret = radixtree_create(&radixtree, inum, lblk, npages, &pblk);
    ASSERT_EQ(npages, ret);
    ASSERT_GT(pblk, 0);

    ssize_t check_size = radixtree_remove(&radixtree, inum, lblk, npages);
    ASSERT_EQ(npages, check_size);

    paddr_t check_paddr = 0;
    check_size = radixtree_lookup(&radixtree, inum, lblk, npages, &check_paddr);
    ASSERT_NE(pblk, check_paddr);
    ASSERT_NE(npages, check_size);
    ASSERT_LE(check_size, 0);
    ASSERT_EQ(nalloc_start, device.num_allocated());
}

TEST_F(RadixTreeFixture, InsertRepeat) {
    inum_t inum   = 0; 
    laddr_t lblk  = 0;
    paddr_t pblk  = 0;
    size_t npages = 1;

    ssize_t ret = radixtree_create(&radixtree, inum, lblk, npages, &pblk);
    ASSERT_EQ(npages, ret);
    ASSERT_GT(pblk, 0);

    paddr_t lookup_paddr;
    ret = radixtree_create(&radixtree, inum, lblk, npages, &lookup_paddr);
    ASSERT_EQ(npages, ret);
    ASSERT_EQ(pblk, lookup_paddr);
}

TEST_F(RadixTreeFixture, InsertMulti) {
    inum_t inum   = 0;
    laddr_t lblk  = 0;
    paddr_t pblk  = 0;
    size_t npages = 20;

    ssize_t ret = radixtree_create(&radixtree, inum, lblk, npages, &pblk);
    ASSERT_EQ(npages, ret);
    ASSERT_GT(pblk, 0);
}

TEST_F(RadixTreeFixture, LookupMulti) {
    inum_t inum   = 0;
    laddr_t lblk  = 0;
    paddr_t pblk  = 0;
    size_t npages = 20;

    ssize_t ret = radixtree_create(&radixtree, inum, lblk, npages, &pblk);
    ASSERT_EQ(npages, ret);
    ASSERT_GT(pblk, 0);

    for (size_t p = 0; p < npages; ++p) {
        paddr_t check_paddr;
        ssize_t check_size = radixtree_lookup(&radixtree, inum, lblk + p, 
                                              npages - p, &check_paddr);
        ASSERT_EQ(pblk + p, check_paddr);
        ASSERT_EQ(npages - p, check_size);
    }
}


TEST_F(RadixTreeFixture, EraseRecreate) {
    inum_t inum   = 0;
    laddr_t lblk  = 0;
    paddr_t pblk  = 0;
    size_t npages = 10;

    ssize_t ret = radixtree_create(&radixtree, inum, lblk, npages, &pblk);
    ASSERT_EQ(npages, ret);
    ASSERT_GT(pblk, 0);

    ssize_t check_size = radixtree_remove(&radixtree, inum, lblk, npages);
    ASSERT_EQ(npages, check_size);

    paddr_t check_paddr = 0;
    check_size = radixtree_lookup(&radixtree, inum, lblk, npages, &check_paddr);
    ASSERT_NE(pblk, check_paddr);
    ASSERT_NE(npages, check_size);
    ASSERT_LE(check_size, 0);

    ret = radixtree_create(&radixtree, inum, lblk, npages, &pblk);
    ASSERT_EQ(npages, ret);
    ASSERT_GT(pblk, 0);
}

TEST_F(RadixTreeFixture, EraseMulti) {
    inum_t inum   = 0;
    laddr_t lblk  = 0;
    paddr_t pblk  = 0;
    size_t npages = 20;

    ssize_t ret = radixtree_create(&radixtree, inum, lblk, npages, &pblk);
    ASSERT_EQ(npages, ret);
    ASSERT_GT(pblk, 0);

    ssize_t check_size = radixtree_remove(&radixtree, inum, lblk, npages);
    ASSERT_EQ(npages, check_size);

    paddr_t check_paddr = 0;
    check_size = radixtree_lookup(&radixtree, inum, lblk, npages, &check_paddr);
    ASSERT_NE(pblk, check_paddr);
    ASSERT_NE(npages, check_size);
    ASSERT_LE(check_size, 0);
}

TEST_F(RadixTreeFixture, ErasePartial) {
    inum_t inum    = 0;
    paddr_t pblk   = 0;
    size_t npages  = 20;
    size_t nremove = 7;

    ssize_t ret = radixtree_create(&radixtree, inum, 0, npages, &pblk);
    ASSERT_EQ(npages, ret);
    ASSERT_GT(pblk, 0);

    ssize_t check_size = radixtree_remove(&radixtree, inum, 
                                          (npages - nremove), nremove);
    ASSERT_EQ(nremove, check_size);

    for (laddr_t l = 0; l < (npages - nremove); ++l) {
        paddr_t check_paddr = 0;
        check_size = radixtree_lookup(&radixtree, inum, l, npages, &check_paddr);
        ASSERT_EQ(pblk + l, check_paddr);
        ASSERT_EQ(npages - nremove - l, check_size);
    }

    for (laddr_t l = (npages - nremove); l < npages; ++l) {
        paddr_t check_paddr = 0;
        check_size = radixtree_lookup(&radixtree, inum, l, npages, &check_paddr);
        ASSERT_LT(check_size, 0);
        ASSERT_EQ(0, check_paddr);
    }
}

TEST_F(RadixTreeFixture, EraseDeallocate) {
    inum_t inum   = 0;
    laddr_t lblk  = 0;
    paddr_t pblk  = 0;
    size_t npages = 20;

    size_t nalloced_meta_only = device.num_allocated();

    ssize_t ret = radixtree_create(&radixtree, inum, lblk, npages, &pblk);
    ASSERT_EQ(npages, ret);
    ASSERT_GT(pblk, 0);

    ssize_t check_size = radixtree_remove(&radixtree, inum, lblk, npages);
    ASSERT_EQ(npages, check_size);
    ASSERT_EQ(nalloced_meta_only, device.num_allocated());

    paddr_t check_paddr = 0;
    check_size = radixtree_lookup(&radixtree, inum, lblk, npages, &check_paddr);
    ASSERT_NE(pblk, check_paddr);
    ASSERT_NE(npages, check_size);
    ASSERT_LE(check_size, 0);
}

/*
    Test grow/shrink capabilities.
 */


TEST_F(RadixTreeFixture, LookupGrow) {
    inum_t inum   = 0;
    laddr_t lblk  = 0;
    paddr_t pblk  = 0;
    size_t npages = 1024;

    ssize_t ret = radixtree_create(&radixtree, inum, lblk, npages, &pblk);
    ASSERT_EQ(npages, ret);
    ASSERT_GT(pblk, 0);

    GET_RADIX(&radixtree);
    ASSERT_EQ(radix->ondev->nlevels, 2);

    paddr_t check_paddr;
    ssize_t check_size = radixtree_lookup(&radixtree, inum, lblk, 
                                        npages, &check_paddr);
    ASSERT_EQ(npages, check_size);
    ASSERT_EQ(pblk, check_paddr);
}

TEST_F(RadixTreeFixture, LookupFirstThenGrow) {
    inum_t inum   = 0;
    laddr_t lblk  = 0;
    paddr_t pblk  = 0;
    size_t npages = 1024;

    paddr_t check_paddr;
    ssize_t check_size = radixtree_lookup(&radixtree, inum, lblk, 
                                          npages, &check_paddr);
    ASSERT_EQ(0, check_size);
    ASSERT_EQ(0, check_paddr);

    ssize_t ret = radixtree_create(&radixtree, inum, lblk, npages, &pblk);
    ASSERT_EQ(npages, ret);
    ASSERT_GT(pblk, 0);

    GET_RADIX(&radixtree);
    ASSERT_EQ(radix->ondev->nlevels, 2);

    check_size = radixtree_lookup(&radixtree, inum, lblk, 
                                        npages, &check_paddr);
    ASSERT_EQ(npages, check_size);
    ASSERT_EQ(pblk, check_paddr);
}

TEST_F(RadixTreeFixture, GrowDirectBoundary) {
    inum_t inum   = 0;
    laddr_t lblk  = 0;
    paddr_t pblk  = 0;
    size_t npages = 1024;

    ssize_t ret = radixtree_create(&radixtree, inum, lblk, RADIX_NDIRECT, &pblk);
    ASSERT_EQ(RADIX_NDIRECT, ret);
    ASSERT_GT(pblk, 0);

    ret = radixtree_create(&radixtree, inum, lblk + RADIX_NDIRECT, 
                            npages - RADIX_NDIRECT, &pblk);
    ASSERT_EQ(npages - RADIX_NDIRECT, ret);
    ASSERT_GT(pblk, 0);

    GET_RADIX(&radixtree);
    ASSERT_EQ(radix->ondev->nlevels, 2);
}

TEST_F(RadixTreeFixture, GrowBoundary) {
    inum_t inum   = 0;
    laddr_t lblk  = 0;
    paddr_t pblk  = 0;
    size_t npages = 1024;

    ssize_t ret = radixtree_create(&radixtree, inum, lblk, 512, &pblk);
    ASSERT_EQ(512, ret);
    ASSERT_GT(pblk, 0);

    ret = radixtree_create(&radixtree, inum, lblk + 512, npages - 512, &pblk);
    ASSERT_EQ(npages - 512, ret);
    ASSERT_GT(pblk, 0);

    GET_RADIX(&radixtree);
    ASSERT_EQ(radix->ondev->nlevels, 2);
}

TEST_F(RadixTreeFixture, GrowAndShrink) {
    inum_t inum   = 0;
    laddr_t lblk  = 0;
    paddr_t pblk  = 0;
    size_t npages = 1024;

    ssize_t ret = radixtree_create(&radixtree, inum, lblk, npages, &pblk);
    ASSERT_EQ(npages, ret);
    ASSERT_GT(pblk, 0);

    GET_RADIX(&radixtree);
    ASSERT_EQ(radix->ondev->nlevels, 2);
    ASSERT_EQ(radix->ondev->nentries, npages);

    paddr_t check_paddr;
    ssize_t check_size = radixtree_lookup(&radixtree, inum, lblk, 
                                        npages, &check_paddr);
    ASSERT_EQ(npages, check_size);
    ASSERT_EQ(pblk, check_paddr);

    check_size = radixtree_remove(&radixtree, inum, lblk + (npages / 2), 
                                  npages / 2);
    ASSERT_EQ(npages / 2, check_size);
    ASSERT_EQ(radix->ondev->nlevels, 1);
    ASSERT_EQ(radix->ondev->nentries, npages / 2);

    check_size = radixtree_lookup(&radixtree, inum, lblk, npages, &check_paddr);
    ASSERT_EQ(npages / 2, check_size);
    ASSERT_EQ(pblk, check_paddr);
}

TEST_F(RadixTreeFixture, GrowAndShrinkFully) {
    inum_t inum   = 0;
    laddr_t lblk  = 0;
    paddr_t pblk  = 0;
    size_t npages = 1024;

    ssize_t ret = radixtree_create(&radixtree, inum, lblk, npages, &pblk);
    ASSERT_EQ(npages, ret);
    ASSERT_GT(pblk, 0);

    GET_RADIX(&radixtree);
    ASSERT_EQ(radix->ondev->nlevels, 2);
    ASSERT_EQ(radix->ondev->nentries, npages);

    paddr_t check_paddr;
    ssize_t check_size = radixtree_lookup(&radixtree, inum, lblk, 
                                        npages, &check_paddr);
    ASSERT_EQ(npages, check_size);
    ASSERT_EQ(pblk, check_paddr);

    check_size = radixtree_remove(&radixtree, inum, lblk, npages);
    ASSERT_EQ(npages, check_size);
    ASSERT_EQ(radix->ondev->nlevels, 0);
    ASSERT_EQ(radix->ondev->nentries, 0);

    check_size = radixtree_lookup(&radixtree, inum, lblk, npages, &check_paddr);
    ASSERT_EQ(0, check_size);
    ASSERT_EQ(0, check_paddr);
}

TEST_F(RadixTreeFixture, GrowAndShrinkFullyLarge) {
    inum_t inum   = 0;
    laddr_t lblk  = 0;
    paddr_t pblk  = 0;
    size_t npages = (1024 * 256) + 1;

    ssize_t ret = radixtree_create(&radixtree, inum, lblk, npages, &pblk);
    ASSERT_EQ(npages, ret);
    ASSERT_GT(pblk, 0);

    GET_RADIX(&radixtree);
    ASSERT_EQ(radix->ondev->nlevels, 3);
    ASSERT_EQ(radix->ondev->nentries, npages);

    paddr_t check_paddr;
    ssize_t check_size = radixtree_lookup(&radixtree, inum, lblk, 
                                        npages, &check_paddr);
    ASSERT_EQ(npages, check_size);
    ASSERT_EQ(pblk, check_paddr);

    check_size = radixtree_remove(&radixtree, inum, lblk, npages);
    ASSERT_EQ(npages, check_size);
    ASSERT_EQ(radix->ondev->nlevels, 0);
    ASSERT_EQ(radix->ondev->nentries, 0);

    check_size = radixtree_lookup(&radixtree, inum, lblk, npages, &check_paddr);
    ASSERT_EQ(0, check_size);
    ASSERT_EQ(0, check_paddr);
}

TEST_F(RadixTreeFixture, LookupGrowMulti) {
    inum_t inum   = 0;
    laddr_t lblk  = 0;
    paddr_t pblk  = 0;
    size_t npages = 300000;

    ssize_t ret = radixtree_create(&radixtree, inum, lblk, npages, &pblk);
    ASSERT_EQ(npages, ret);
    ASSERT_GT(pblk, 0);

    GET_RADIX(&radixtree);
    ASSERT_EQ(radix->ondev->nlevels, 3);


    paddr_t check_paddr;
    ssize_t check_size = radixtree_lookup(&radixtree, inum, lblk, 
                                        npages, &check_paddr);
    ASSERT_EQ(npages, check_size);
    ASSERT_EQ(pblk, check_paddr);
}
