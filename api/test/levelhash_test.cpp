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
    ret = levelhash_lookup(&level_idx, 0, lblk, npages, &lookup_paddr);
    ASSERT_EQ(npages, ret);
    ASSERT_EQ(pblk, lookup_paddr);
}
#if 0
TEST_F(LevelHashingFixture, LevelLookupRepeat) {
    laddr_t lblk  = 0;
    paddr_t pblk  = 0;
    size_t npages = 1;

    ssize_t ret = levelhash_create(&level_idx, 0, lblk, npages, &pblk);
    ASSERT_EQ(npages, ret);
    ASSERT_GT(pblk, 0);

    paddr_t lookup_paddr;
    ret = levelhash_lookup(&level_idx, 0, lblk, npages, &lookup_paddr);
    ASSERT_EQ(npages, ret);
    ASSERT_EQ(pblk, lookup_paddr);

    paddr_t repeat;
    ret = levelhash_create(&level_idx, 0, lblk, npages, &repeat);
    ASSERT_EQ(npages, ret);
    ASSERT_EQ(lookup_paddr, repeat);
}
#endif

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
    ret = levelhash_lookup(&level_idx, 0, lblk, npages, &lookup_paddr);
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
    ret = levelhash_lookup(&new_idx, 0, lblk, npages, &lookup_paddr);
    ASSERT_EQ(npages, ret);
    ASSERT_EQ(pblk, lookup_paddr);
}

/*******************************************************************************
 * Section: Tests which cause the hash table to expand.
 ******************************************************************************/

TEST_F(LevelHashingFixture, LevelExpandIteratively) {
    laddr_t lblk  = 0;
    paddr_t pblk  = 0;
    size_t npages = 100;
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
    size_t npages = 100;
    size_t npage_inc = 1;

    map<laddr_t, paddr_t> prev_paddrs;

    for (size_t p = 0; p < npages; p += npage_inc) {
        ssize_t ret = levelhash_create(&level_idx, 0, lblk+p, npage_inc, &pblk);
        ASSERT_EQ(npage_inc, ret);

        prev_paddrs[lblk + p] = pblk;

        for (size_t l = 0; l <= p; ++l) {
            paddr_t lookup_paddr;
            ssize_t ret = levelhash_lookup(&level_idx, 0, lblk + l, npage_inc,
                                           &lookup_paddr);
           
            ASSERT_EQ(npage_inc, ret) << lblk + l << " " << p << endl;
            ASSERT_EQ(prev_paddrs[lblk + l], lookup_paddr) << l << " " << p;
        }
    }
}

TEST_F(LevelHashingFixture, LevelExpandIterativelyAndLookupHuge) {
    paddr_t pblk  = 0;
    size_t npages = 100;
    size_t npage_inc = 1;

    map<laddr_t, paddr_t> prev_paddrs;

    for (size_t p = 0; p < npages; p += npage_inc) {
        ssize_t ret = levelhash_create(&level_idx, 0, p, npage_inc, &pblk);
        ASSERT_EQ(npage_inc, ret);

        prev_paddrs[p] = pblk;
    }

    for (size_t l = 0; l < npages; ++l) {
        paddr_t lookup_paddr;
        ssize_t ret = levelhash_lookup(&level_idx, 0, l, npage_inc, &lookup_paddr);
       
        ASSERT_EQ(npage_inc, ret) << l << endl;
        ASSERT_EQ(prev_paddrs[l], lookup_paddr) << l << endl;
    }
}

TEST_F(LevelHashingFixture, LevelExpandIterativelyAndLookupPersistent) {
    laddr_t lblk  = 0;
    paddr_t pblk  = 0;
    size_t npages = 100;
    size_t npage_inc = 1;

    map<laddr_t, paddr_t> prev_paddrs;

    for (size_t p = 0; p < npages; p += npage_inc) {
        ssize_t ret = levelhash_create(&level_idx, 0, lblk+p, npage_inc, &pblk);
        ASSERT_EQ(npage_inc, ret);

        prev_paddrs[lblk + p] = pblk;

        idx_struct_t temp_idx;
        int err = levelhash_init(&idx_spec, &inode_space, &temp_idx);
        ASSERT_FALSE(err);
        for (size_t l = 0; l <= p; ++l) {
            paddr_t lookup_paddr;
            ssize_t ret = levelhash_lookup(&temp_idx, 0, lblk + l, npage_inc,
                                           &lookup_paddr);
           
            ASSERT_EQ(npage_inc, ret) << lblk + l << " " << p << endl;
            ASSERT_EQ(prev_paddrs[lblk + l], lookup_paddr) << l << " " << p;
        }
    }
}

TEST_F(LevelHashingFixture, LevelExpandIterativelyUnfavorableAlloc) {
    paddr_t pblk  = 0;
    size_t npages = 64 * 256;
    size_t npage_inc = 256;

    map<laddr_t, paddr_t> prev_paddrs;
    map<laddr_t, size_t> prev_sizes;

    set_restricted_data_alloc(true);

    for (size_t laddr = 0; laddr < npages; ) {
        ssize_t ret = levelhash_create(&level_idx, 0, laddr, npage_inc, &pblk);
        ASSERT_GT(ret, 0);

        for (laddr_t k = 0; k < ret; ++k) {
            prev_paddrs[laddr + k] = pblk + k;
            prev_sizes[laddr + k] = ret - k;
        }

        idx_struct_t temp_idx;
        int err = levelhash_init(&idx_spec, &inode_space, &temp_idx);
        ASSERT_FALSE(err);
        for (size_t l = 0; l < laddr + ret; ++l) {
            paddr_t lookup_paddr;
            ssize_t ret = levelhash_lookup(&temp_idx, 0, l, prev_sizes[l],
                                           &lookup_paddr);
           
            ASSERT_EQ(prev_sizes[l], ret) << l << " " << laddr << endl;
            ASSERT_EQ(prev_paddrs[l], lookup_paddr) << l << " " << laddr << endl;
        }

        laddr += ret;
    }
}


TEST_F(LevelHashingFixture, LevelExpandSlab) {
    paddr_t pblk  = 0;
    size_t npages = 100;

    ssize_t ret = levelhash_create(&level_idx, 0, 0, npages, &pblk);
    ASSERT_EQ(npages, ret);
    ASSERT_GT(pblk, 0);
}

TEST_F(LevelHashingFixture, LevelLookupAfterExpand) {
    paddr_t pblk  = 0;
    size_t npages = 100;

    ssize_t ret = levelhash_create(&level_idx, 0, 0, npages, &pblk);
    ASSERT_EQ(npages, ret);
    ASSERT_GT(pblk, 0);

    for (laddr_t lblk = 0; lblk < (laddr_t)npages; ++lblk) {
        paddr_t lookup_paddr;
        ret = levelhash_lookup(&level_idx, 0, lblk, npages - lblk, &lookup_paddr);
        ASSERT_EQ(npages - (ssize_t)lblk, ret);
        ASSERT_EQ(pblk + (ssize_t)lblk, lookup_paddr);
    }
}

TEST_F(LevelHashingFixture, LevelLookupAfterExpandAcrossIndices) {
    paddr_t pblk  = 0;
    size_t npages = 100;

    idx_struct_t test_idx;
    int init_again = levelhash_init(&idx_spec, &inode_space, &test_idx);
    ASSERT_FALSE(init_again);

    ssize_t ret = levelhash_create(&level_idx, 0, 0, npages, &pblk);
    ASSERT_EQ(npages, ret);
    ASSERT_GT(pblk, 0);

    levelhash_clear_metadata(&test_idx);

    for (laddr_t lblk = 0; lblk < (laddr_t)npages; ++lblk) {
        paddr_t lookup_paddr;
        ret = levelhash_lookup(&test_idx, 0, lblk, npages - lblk, &lookup_paddr);
        ASSERT_EQ(npages - (ssize_t)lblk, ret);
        ASSERT_EQ(pblk + (ssize_t)lblk, lookup_paddr);
    }
}

/*******************************************************************************
 * Section: Tests which cause the hash table to shrink.
 ******************************************************************************/

TEST_F(LevelHashingFixture, LevelShrinkSlab) {
    paddr_t pblk  = 0;
    size_t npages = 100;

    ssize_t ret = levelhash_create(&level_idx, 0, 0, npages, &pblk);
    ASSERT_EQ(npages, ret);
    ASSERT_GT(pblk, 0);

    ret = levelhash_remove(&level_idx, 0, 0, npages - 1);
    ASSERT_EQ(npages - 1, ret);

    paddr_t lookup_paddr;
    ret = levelhash_lookup(&level_idx, 0, npages - 1, npages, &lookup_paddr);
    ASSERT_GT(ret, 0);
    ASSERT_EQ(pblk + npages - 1, lookup_paddr);
}
