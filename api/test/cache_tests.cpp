#include "cache_tests.hpp"

#include <map>

using namespace std;
#if 0
INSTANTIATE_TEST_CASE_P(OnlyLevelHashing, 
                        CachingTestFixture,
                        ::testing::Values(&levelhash_fns));

INSTANTIATE_TEST_CASE_P(OnlyGlobalHashTable, 
                        CachingTestFixture,
                        ::testing::Values(&hash_fns));
#endif

TEST_P(CachingTestFixture, InitNoError) {
    ASSERT_EQ(0, init_err);
}

TEST_P(CachingTestFixture, InsertCacheOnly) {
    inum_t inum = 17;
    size_t npages = 50;

    map<laddr_t, paddr_t> mapping;

    for (laddr_t lblk = 0; lblk < (laddr_t)npages; ++lblk) {
        paddr_t pblk;
        ssize_t ret = FN(&idx_struct, im_create,
                         &idx_struct, inum, lblk, 1, &pblk);

        mapping[lblk] = pblk;
        ASSERT_EQ(1, ret) << "Insert: " << lblk << ": " << pblk;

    }

    // The other idx struct should not see inserts, but the original should.
    for (laddr_t lblk = 0; lblk < (laddr_t)npages; ++lblk) {
        ASSERT_EQ(1, mapping.count(lblk)) << "This should never happen";

        paddr_t p;
        ssize_t ret = FN(&idx_other, im_lookup, &idx_other, inum, lblk, &p);

        ASSERT_LT(ret, 1) << "updates should only reside in cache!" << endl; 

        ret = FN(&idx_struct, im_lookup, &idx_struct, inum, lblk, &p);
        ASSERT_GE(ret, 1) << "cannot find cached updates!" << endl;
        ASSERT_TRUE(mapping[lblk] == p) << "cached update is wrong!";
    }

    device.deallocate();
}

TEST_P(CachingTestFixture, InsertCacheFlush) {
    inum_t inum = 17;
    size_t npages = 50;

    map<laddr_t, paddr_t> mapping;

    for (laddr_t lblk = 0; lblk < (laddr_t)npages; ++lblk) {
        paddr_t pblk;
        ssize_t ret = FN(&idx_struct, im_create,
                         &idx_struct, inum, lblk, 1, &pblk);

        mapping[lblk] = pblk;
        ASSERT_EQ(1, ret) << "Insert: " << lblk << ": " << pblk;

    }

    ASSERT_FALSE(FN(&idx_struct, im_persist, &idx_struct)) << 
        "Persisting failed!" << endl;

    // Both structures should now see the updates.
    for (laddr_t lblk = 0; lblk < (laddr_t)npages; ++lblk) {
        ASSERT_EQ(1, mapping.count(lblk)) << "This should never happen";

        paddr_t p;
        ssize_t ret = FN(&idx_other, im_lookup, &idx_other, inum, lblk, &p);

        ASSERT_GE(ret, 1) << "cannot find cached updates!" << endl;
        ASSERT_TRUE(mapping[lblk] == p) << "cached update is wrong!";

        ret = FN(&idx_struct, im_lookup, &idx_struct, inum, lblk, &p);
        ASSERT_GE(ret, 1) << "cannot find cached updates!" << endl;
        ASSERT_TRUE(mapping[lblk] == p) << "cached update is wrong!";
    }

    device.deallocate();
}

TEST_P(CachingTestFixture, InsertBothCached) {
    inum_t inum = 17;
    size_t npages = 50;

    map<laddr_t, paddr_t> mapping;

    FN(&idx_other, im_set_caching, &idx_other, true);

    for (laddr_t lblk = 0; lblk < (laddr_t)npages; ++lblk) {
        // Do this to make the other have a "valid" cache.
        paddr_t p;
        ssize_t ret = FN(&idx_other, im_lookup, &idx_other, inum, lblk, &p);

        ASSERT_LT(ret, 1) << "entry shouldn't even exist yet!" << endl; 

        // Then do the actual insert.
        paddr_t pblk;
        ret = FN(&idx_struct, im_create, &idx_struct, inum, lblk, 1, &pblk);

        mapping[lblk] = pblk;
        ASSERT_EQ(1, ret) << "Insert: " << lblk << ": " << pblk;
    }

    ASSERT_FALSE(FN(&idx_struct, im_persist, &idx_struct)) << 
        "Persisting failed!" << endl;

    // The other idx struct should not see inserts, since it has not invalidated
    for (laddr_t lblk = 0; lblk < (laddr_t)npages; ++lblk) {
        ASSERT_EQ(1, mapping.count(lblk)) << "This should never happen";

        paddr_t p;
        ssize_t ret = FN(&idx_other, im_lookup, &idx_other, inum, lblk, &p);

        ASSERT_LT(ret, 1) << "updates should only reside in cache!" << endl; 

        ret = FN(&idx_struct, im_lookup, &idx_struct, inum, lblk, &p);
        ASSERT_GE(ret, 1) << "cannot find cached updates!" << endl;
        ASSERT_TRUE(mapping[lblk] == p) << "cached update is wrong!";
    }

    ASSERT_FALSE(FN(&idx_other, im_invalidate, &idx_other)) << 
        "Invalidation failed!" << endl;

    for (laddr_t lblk = 0; lblk < (laddr_t)npages; ++lblk) {

        paddr_t p;
        ssize_t ret = FN(&idx_other, im_lookup, &idx_other, inum, lblk, &p);

        ASSERT_GE(ret, 1) << "cannot find after invalidation!" << endl;
        ASSERT_TRUE(mapping[lblk] == p) << "did not reread properly!";
    }

    device.deallocate();
}

TEST_P(CachingTestFixture, MultiInsertBothCached) {
    inum_t inum = 17;
    size_t npages = 10000;

    map<laddr_t, paddr_t> mapping;

    FN(&idx_other, im_set_caching, &idx_other, true);

    // Do this to make the other have a "valid" cache.
    for (laddr_t lblk = 0; lblk < (laddr_t)npages; ++lblk) {
        paddr_t p;
        ssize_t ret = FN(&idx_other, im_lookup, &idx_other, inum, lblk, &p);

        ASSERT_LT(ret, 1) << "entry shouldn't even exist yet!" << endl; 
    }

    // Then do the actual insert.
    paddr_t pblk;
    ssize_t ret = FN(&idx_struct, im_create, &idx_struct, inum, 0, npages, &pblk);

    for (laddr_t lblk = 0; lblk < (laddr_t)npages; ++lblk) {
        mapping[lblk] = pblk + lblk;
    }

    ASSERT_EQ(npages, ret) << "Insert: 0" << ": " << pblk;

    ASSERT_FALSE(FN(&idx_struct, im_persist, &idx_struct)) << 
        "Persisting failed!" << endl;

    // The other idx struct should not see inserts, since it has not invalidated
    for (laddr_t lblk = 0; lblk < (laddr_t)npages; ++lblk) {
        ASSERT_EQ(1, mapping.count(lblk)) << "This should never happen";

        paddr_t p;
        ssize_t ret = FN(&idx_other, im_lookup, &idx_other, inum, lblk, &p);

        ASSERT_LT(ret, 1) << "updates should only reside in cache!" << endl 
            << "\tAt lookup of lblk " << lblk; 

        ret = FN(&idx_struct, im_lookup, &idx_struct, inum, lblk, &p);
        ASSERT_EQ(ret, npages - lblk) << "cannot find cached updates!" << endl;
        ASSERT_TRUE(mapping[lblk] == p) << "cached update is wrong!";
    }

    ASSERT_FALSE(FN(&idx_other, im_invalidate, &idx_other)) << 
        "Invalidation failed!" << endl;

    for (laddr_t lblk = 0; lblk < (laddr_t)npages; ++lblk) {

        paddr_t p;
        ssize_t ret = FN(&idx_other, im_lookup, &idx_other, inum, lblk, &p);

        ASSERT_EQ(ret, npages - lblk) << "cannot find after invalidation!" << endl
            << "\tFailed to find lblk " << lblk;
        ASSERT_TRUE(mapping[lblk] == p) << "did not reread properly!";
    }

    device.deallocate();
}
#if 0
TEST_P(GenericTestFixture, InsertPersistCheckEnd) {
    inum_t inum = 17;
    size_t npages = 1000;

    map<laddr_t, paddr_t> mapping;

    for (laddr_t lblk = 0; lblk < (laddr_t)npages; ++lblk) {
        paddr_t pblk;
        ssize_t ret = FN(&idx_struct, im_create,
                         &idx_struct, inum, lblk, 1, &pblk);

        mapping[lblk] = pblk;
        ASSERT_EQ(1, ret) << "Insert: " << lblk << ": " << pblk;

    }

    for (laddr_t l = 0; l < (laddr_t)npages; ++l) {
        ASSERT_EQ(1, mapping.count(l)) << "This should never happen";

        paddr_t p;
        ssize_t r = FN(&idx_other, im_lookup,
                       &idx_other, inum, l, &p);


        ASSERT_LE(1, r) << strerror(-r) << " on lblk " << l;
        ASSERT_TRUE(mapping[l] == p);
    }

    device.deallocate();
}

TEST_P(GenericTestFixture, InsertPersistThenRemoveAll) {
    inum_t inum = 17;
    size_t npages = 100;

    map<laddr_t, paddr_t> mapping;

    for (laddr_t lblk = 0; lblk < (laddr_t)npages; ++lblk) {
        paddr_t pblk;
        ssize_t ret = FN(&idx_struct, im_create,
                         &idx_struct, inum, lblk, 1, &pblk);

        mapping[lblk] = pblk;
        ASSERT_EQ(1, ret) << "Insert: " << lblk << ": " << pblk;
    }

    for (laddr_t lblk = 0; lblk < (laddr_t)npages; ++lblk) {
        paddr_t pblk;
        ssize_t ret = FN(&idx_other, im_lookup,
                         &idx_other, inum, lblk, &pblk);

        ASSERT_LE(1, ret) << strerror(-ret) << " on lblk " << lblk;
        ASSERT_LE(ret, npages - lblk);
        ASSERT_EQ(mapping[lblk], pblk);
    }

    ssize_t ret = FN(&idx_struct, im_remove,
                     &idx_struct, inum, 0, npages);
    ASSERT_EQ(ret, npages);


    // Do another lookup, make sure they are all gone.
    for (laddr_t lblk = 0; lblk < (laddr_t)npages; ++lblk) {
        paddr_t pblk;
        ssize_t ret = FN(&idx_other, im_lookup,
                         &idx_other, inum, lblk, &pblk);

        ASSERT_LE(ret, 0);
        ASSERT_EQ(0, pblk);
    }

    device.deallocate();
}

TEST_P(GenericTestFixture, InsertPersistThenRemoveSome) {
    inum_t inum    = 17;
    size_t npages  = 100;
    size_t nremove = 80;
    size_t nremain = npages - nremove;
    laddr_t start  = 0;
    laddr_t remove = start + nremain;

    map<laddr_t, paddr_t> mapping;

    for (laddr_t lblk = start; lblk < (laddr_t)npages + start; ++lblk) {
        paddr_t pblk;
        ssize_t ret = FN(&idx_struct, im_create,
                         &idx_struct, inum, lblk, 1, &pblk);

        mapping[lblk] = pblk;
        ASSERT_EQ(1, ret) << "Insert: " << lblk << ": " << pblk;
    }

    for (laddr_t lblk = start; lblk < (laddr_t)npages + start; ++lblk) {
        paddr_t pblk;
        ssize_t ret = FN(&idx_other, im_lookup,
                         &idx_other, inum, lblk, &pblk);

        ASSERT_LE(1, ret) << strerror(-ret) << " on lblk " << lblk;
        ASSERT_LE(ret, npages - lblk);
        ASSERT_EQ(mapping[lblk], pblk);
    }

    ssize_t ret = FN(&idx_struct, im_remove,
                     &idx_struct, inum, remove, nremove);
    ASSERT_EQ(ret, nremove);


    // Do another lookup for the remainder.
    for (laddr_t lblk = start; lblk < (laddr_t)nremain + start; ++lblk) {
        paddr_t pblk;
        ssize_t ret = FN(&idx_other, im_lookup,
                         &idx_other, inum, lblk, &pblk);

        ASSERT_LE(1, ret) << strerror(-ret) << " on lblk " << lblk;
        ASSERT_LE(ret, npages - lblk);
        ASSERT_EQ(mapping[lblk], pblk);
    }

    device.deallocate();
}

/*******************************************************************************
 * Section: Tests on "slab" inserts (many blocks contiguous).
 ******************************************************************************/

TEST_P(GenericTestFixture, Slab_InsertPersistThenRemoveAll) {
    inum_t inum = 17;
    size_t npages = 100;
    paddr_t lblk = 0;
    paddr_t pblk;

    ssize_t ret = FN(&idx_struct, im_create,
                     &idx_struct, inum, lblk, npages, &pblk);

    ASSERT_EQ(npages, ret) << "Insert: " << lblk << ": " << pblk;
    ASSERT_NE(0, pblk);

    for (laddr_t l = lblk; l < (laddr_t)npages + lblk; ++l) {
        paddr_t lookup_pblk;
        ssize_t ret = FN(&idx_other, im_lookup,
                         &idx_other, inum, l, &lookup_pblk);

        ASSERT_EQ(ret, npages - l) << "lookup " << l;
        ASSERT_EQ(pblk + l, lookup_pblk);
    }

    ret = FN(&idx_struct, im_remove, &idx_struct, inum, lblk, npages);
    ASSERT_EQ(ret, npages) << "Remove error: " << strerror(-ret);

    // Do another lookup, make sure they are all gone.
    for (laddr_t l = 0; l < (laddr_t)npages + lblk; ++l) {
        paddr_t lookup_pblk;
        ssize_t ret = FN(&idx_other, im_lookup,
                         &idx_other, inum, l, &lookup_pblk);

        ASSERT_LE(ret, 0);
        ASSERT_EQ(0, lookup_pblk);
    }

    device.deallocate();
}

TEST_P(GenericTestFixture, Slab_InsertPersistThenRemoveSome) {
    inum_t inum    = 17;
    size_t npages  = 100;
    size_t nremove = 80;
    size_t nremain = npages - nremove;
    laddr_t start  = 0;
    laddr_t remove = start + nremain;

    map<laddr_t, paddr_t> mapping;

    for (laddr_t lblk = start; lblk < (laddr_t)npages + start; ++lblk) {
        paddr_t pblk;
        ssize_t ret = FN(&idx_struct, im_create,
                         &idx_struct, inum, lblk, 1, &pblk);

        mapping[lblk] = pblk;
        ASSERT_EQ(1, ret) << "Insert: " << lblk << ": " << pblk;
    }

    for (laddr_t lblk = start; lblk < (laddr_t)npages + start; ++lblk) {
        paddr_t pblk;
        ssize_t ret = FN(&idx_other, im_lookup,
                         &idx_other, inum, lblk, &pblk);

        ASSERT_LE(1, ret) << strerror(-ret) << " on lblk " << lblk;
        ASSERT_LE(ret, npages - lblk);
        ASSERT_EQ(mapping[lblk], pblk);
    }

    ssize_t ret = FN(&idx_struct, im_remove,
                     &idx_struct, inum, remove, nremove);
    ASSERT_EQ(ret, nremove);


    // Do another lookup for the remainder.
    for (laddr_t lblk = start; lblk < (laddr_t)nremain + start; ++lblk) {
        paddr_t pblk;
        ssize_t ret = FN(&idx_other, im_lookup,
                         &idx_other, inum, lblk, &pblk);

        ASSERT_LE(1, ret) << strerror(-ret) << " on lblk " << lblk;
        ASSERT_LE(ret, npages - lblk);
        ASSERT_EQ(mapping[lblk], pblk);
    }

    device.deallocate();
}

/*******************************************************************************
 * Section: Tests on small "slab" inserts (some blocks contiguous).
 ******************************************************************************/

TEST_P(GenericTestFixture, SmallSlab_InsertPersistThenRemoveAll) {
    inum_t inum = 17;
    size_t npages = 100;
    size_t inc = 10;
    paddr_t lblk = 0;

    for (laddr_t l = lblk; l < (laddr_t)npages + lblk; l += inc) {
        paddr_t pblk;
        ssize_t ret = FN(&idx_struct, im_create,
                         &idx_struct, inum, l, inc, &pblk);

        ASSERT_EQ(inc, ret) << "Insert: " << l << ": " << pblk;
        ASSERT_NE(0, pblk);
        device.allocate(1); // Ensure the ranges are not contiguous.
    }


    for (laddr_t l = lblk; l < (laddr_t)npages + lblk; ++l) {
        paddr_t lookup_pblk;
        ssize_t ret = FN(&idx_other, im_lookup,
                         &idx_other, inum, l, &lookup_pblk);

        ASSERT_EQ(ret, inc - (l % inc) ) << "lookup " << l;
    }

    ssize_t ret = FN(&idx_struct, im_remove, &idx_struct, inum, lblk, npages);
    ASSERT_EQ(ret, npages) << "Remove error: " << strerror(-ret);

    // Do another lookup, make sure they are all gone.
    for (laddr_t l = 0; l < (laddr_t)npages + lblk; ++l) {
        paddr_t lookup_pblk;
        ssize_t ret = FN(&idx_other, im_lookup,
                         &idx_other, inum, l, &lookup_pblk);

        ASSERT_LE(ret, 0);
        ASSERT_EQ(0, lookup_pblk);
    }

    device.deallocate();
}
#endif
