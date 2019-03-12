#include "generic_tests.hpp"

#include <map>

using namespace std;

TEST_P(GenericTestFixture, InitNoError) {
    ASSERT_EQ(0, init_err);
}

TEST_P(GenericTestFixture, InsertPersist) {
    inum_t inum = 17;
    size_t npages = 10000;

    map<laddr_t, paddr_t> mapping;

    for (laddr_t lblk = 0; lblk < (laddr_t)npages; ++lblk) {
        paddr_t pblk;
        ssize_t ret = FN(&idx_struct, im_create,
                         &idx_struct, inum, lblk, 1, &pblk);

        mapping[lblk] = pblk;
        ASSERT_EQ(1, ret);
    }

    for (laddr_t lblk = 0; lblk < (laddr_t)npages; ++lblk) {
        paddr_t pblk;
        ssize_t ret = FN(&idx_other, im_lookup,
                         &idx_other, inum, lblk, &pblk);

        ASSERT_LE(1, ret);
        ASSERT_LE(ret, npages - lblk);
        ASSERT_EQ(mapping[lblk], pblk);
    }

    device.deallocate();
}

INSTANTIATE_TEST_CASE_P(AllStructures, 
                        GenericTestFixture,
                        ::testing::Values(&extent_tree_fns, 
                                          &hash_fns, 
                                          &levelhash_fns));
