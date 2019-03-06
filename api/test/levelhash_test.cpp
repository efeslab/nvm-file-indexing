#include "levelhash_test.hpp"

#include <iostream>
#include <map>
#include <set>

using namespace std;

TEST_F(LevelHashingFixture, LevelInit) {
    ASSERT_EQ(0, init_err);
}

TEST_F(LevelHashingFixture, LevelInsert) {
    laddr_t lblk  = 0;
    paddr_t pblk  = 0;
    size_t npages = 1;

    ssize_t ret = levelhash_create(&level_idx, 0, lblk, npages, &pblk);
    ASSERT_EQ(npages, ret);
    ASSERT_GT(pblk, 0);
}

TEST_F(LevelHashingFixture, LevelLookup) {
    laddr_t lblk  = 0;
    paddr_t pblk  = 0;
    size_t npages = 1;

    ssize_t ret = levelhash_create(&level_idx, 0, lblk, npages, &pblk);
    ASSERT_EQ(npages, ret);
    ASSERT_GT(pblk, 0);

    paddr_t lookup_paddr;
    ret = levelhash_lookup(&level_idx, 0, lblk, &lookup_paddr);
    ASSERT_EQ(npages, ret);
    ASSERT_EQ(pblk, lookup_paddr);
}

TEST_F(LevelHashingFixture, LevelRemove) {
    laddr_t lblk  = 0;
    paddr_t pblk  = 0;
    size_t npages = 1;

    ssize_t ret = levelhash_create(&level_idx, 0, lblk, npages, &pblk);
    ASSERT_EQ(npages, ret);
    ASSERT_GT(pblk, 0);

    ret = levelhash_remove(&level_idx, 0, lblk, npages);
    ASSERT_EQ(npages, ret);

    paddr_t lookup_paddr;
    ret = levelhash_lookup(&level_idx, 0, lblk, &lookup_paddr);
    ASSERT_GT(0, ret);
    ASSERT_EQ(0, lookup_paddr);
}

TEST_F(LevelHashingFixture, LevelPersist) {
    laddr_t lblk  = 0;
    paddr_t pblk  = 0;
    size_t npages = 1;

    ssize_t ret = levelhash_create(&level_idx, 0, lblk, npages, &pblk);
    ASSERT_EQ(npages, ret);
    ASSERT_GT(pblk, 0);

    idx_struct_t new_idx;
    int err = levelhash_init(&idx_spec, &inode_space, &new_idx);

    paddr_t lookup_paddr;
    ret = levelhash_lookup(&new_idx, 0, lblk, &lookup_paddr);
    ASSERT_EQ(npages, ret);
    ASSERT_EQ(pblk, lookup_paddr);
}

/*******************************************************************************
 * Section: Tests which cause the hash table to expand.
 ******************************************************************************/

TEST_F(LevelHashingFixture, LevelExpandIteratively) {
    laddr_t lblk  = 0;
    paddr_t pblk  = 0;
    size_t npages = 1000;
    size_t npage_inc = 1;

    set<paddr_t> prev_paddrs;

    for (size_t p = 0; p < npages; p += npage_inc) {
        ssize_t ret = levelhash_create(&level_idx, 0, lblk+p, npage_inc, &pblk);
        ASSERT_EQ(npage_inc, ret);

        ASSERT_EQ(prev_paddrs.end(), prev_paddrs.find(pblk));
        ASSERT_TRUE(prev_paddrs.insert(pblk).second);
    }
}

TEST_F(LevelHashingFixture, LevelExpandIterativelyAndLookup) {
    laddr_t lblk  = 0;
    paddr_t pblk  = 0;
    size_t npages = 1000;
    size_t npage_inc = 1;

    map<laddr_t, paddr_t> prev_paddrs;

    for (size_t p = 0; p < npages; p += npage_inc) {
        ssize_t ret = levelhash_create(&level_idx, 0, lblk+p, npage_inc, &pblk);
        ASSERT_EQ(npage_inc, ret);

        prev_paddrs[lblk + p] = pblk;

        for (size_t l = 0; l <= p; ++l) {
            paddr_t lookup_paddr;
            ssize_t ret = levelhash_lookup(&level_idx, 0, lblk + l,
                                           &lookup_paddr);
           
            ASSERT_EQ(npage_inc, ret) << lblk + l << " " << p << endl;
            ASSERT_EQ(prev_paddrs[lblk + l], lookup_paddr) << l << " " << p;
        }
    }
}

TEST_F(LevelHashingFixture, LevelExpandSlab) {
    paddr_t pblk  = 0;
    size_t npages = 1000;

    ssize_t ret = levelhash_create(&level_idx, 0, 0, npages, &pblk);
    ASSERT_EQ(npages, ret);
    ASSERT_GT(pblk, 0);
}

TEST_F(LevelHashingFixture, LevelLookupAfterExpand) {
    paddr_t pblk  = 0;
    size_t npages = 1000;

    ssize_t ret = levelhash_create(&level_idx, 0, 0, npages, &pblk);
    ASSERT_EQ(npages, ret);
    ASSERT_GT(pblk, 0);

    for (laddr_t lblk = 0; lblk < (laddr_t)npages; ++lblk) {
        paddr_t lookup_paddr;
        ret = levelhash_lookup(&level_idx, 0, lblk, &lookup_paddr);
        ASSERT_EQ(npages - (ssize_t)lblk, ret);
        ASSERT_EQ(pblk + (ssize_t)lblk, lookup_paddr);
    }
}

/*******************************************************************************
 * Section: Tests which cause the hash table to shrink.
 ******************************************************************************/

TEST_F(LevelHashingFixture, LevelShrinkSlab) {
    paddr_t pblk  = 0;
    size_t npages = 1000;

    ssize_t ret = levelhash_create(&level_idx, 0, 0, npages, &pblk);
    ASSERT_EQ(npages, ret);
    ASSERT_GT(pblk, 0);

    ret = levelhash_remove(&level_idx, 0, 0, npages - 1);
    ASSERT_EQ(npages - 1, ret);

    paddr_t lookup_paddr;
    ret = levelhash_lookup(&level_idx, 0, npages - 1, &lookup_paddr);
    ASSERT_GT(ret, 0);
    ASSERT_EQ(pblk + npages - 1, lookup_paddr);
}
