#include "cache_tests.hpp"

#include <map>

using namespace std;
#if 0
INSTANTIATE_TEST_CASE_P(ExtentTree, 
                        CachingTestFixture,
                        ::testing::Values(&extent_tree_fns));
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
        ssize_t ret = FN(&idx_other, im_lookup, &idx_other, inum, lblk, 1, &p);

        ASSERT_LT(ret, 1) << "updates should only reside in cache!" << endl; 

        ret = FN(&idx_struct, im_lookup, &idx_struct, inum, lblk, npages, &p);
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
        ssize_t ret = FN(&idx_other, im_lookup, &idx_other, inum, lblk, npages, &p);

        ASSERT_GE(ret, 1) << "cannot find cached updates!" << endl;
        ASSERT_TRUE(mapping[lblk] == p) << "cached update is wrong!";

        ret = FN(&idx_struct, im_lookup, &idx_struct, inum, lblk, npages, &p);
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
        ssize_t ret = FN(&idx_other, im_lookup, &idx_other, inum, lblk, npages, &p);

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
        ssize_t ret = FN(&idx_other, im_lookup, &idx_other, inum, lblk, 1, &p);

        ASSERT_LT(ret, 1) << "updates should only reside in cache!" << endl; 

        ret = FN(&idx_struct, im_lookup, &idx_struct, inum, lblk, 1, &p);
        ASSERT_GE(ret, 1) << "cannot find cached updates!" << endl;
        ASSERT_TRUE(mapping[lblk] == p) << "cached update is wrong!";
    }

    ASSERT_FALSE(FN(&idx_other, im_invalidate, &idx_other)) << 
        "Invalidation failed!" << endl;

    for (laddr_t lblk = 0; lblk < (laddr_t)npages; ++lblk) {

        paddr_t p;
        ssize_t ret = FN(&idx_other, im_lookup, &idx_other, inum, lblk, 1, &p);

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
        ssize_t ret = FN(&idx_other, im_lookup, &idx_other, inum, lblk, 1, &p);

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
        ssize_t ret = FN(&idx_other, im_lookup, &idx_other, inum, lblk, 1, &p);

        ASSERT_LT(ret, 1) << "updates should only reside in cache!" << endl 
            << "\tAt lookup of lblk " << lblk; 

        ret = FN(&idx_struct, im_lookup, &idx_struct, inum, lblk, npages - lblk, &p);
        ASSERT_EQ(ret, npages - lblk) << "cannot find cached updates!" << endl;
        ASSERT_TRUE(mapping[lblk] == p) << "cached update is wrong!";
    }

    ASSERT_FALSE(FN(&idx_other, im_invalidate, &idx_other)) << 
        "Invalidation failed!" << endl;

    for (laddr_t lblk = 0; lblk < (laddr_t)npages; ++lblk) {

        paddr_t p;
        ssize_t ret = FN(&idx_other, im_lookup, &idx_other, inum, lblk, npages - lblk, &p);

        ASSERT_EQ(ret, npages - lblk) << "cannot find after invalidation!" << endl
            << "\tFailed to find lblk " << lblk;
        ASSERT_TRUE(mapping[lblk] == p) << "did not reread properly!";
    }

    device.deallocate();
}
