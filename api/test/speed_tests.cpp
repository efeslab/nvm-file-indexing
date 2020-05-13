#include "speed_tests.hpp"

//INSTANTIATE_TEST_CASE_P(AllStructures, 
//                        SpeedTestFixture,
//                        ::testing::Values(&extent_tree_fns, 
//                                          &hash_fns, 
//                                          &levelhash_fns,
//                                          &radixtree_fns));
//
//INSTANTIATE_TEST_CASE_P(AllStructures, 
//                        SpeedTestFixturePrecreate,
//                        ::testing::Values(&extent_tree_fns, 
//                                          &hash_fns, 
//                                          &levelhash_fns,
//                                          &radixtree_fns));

TEST_P(SpeedTestFixture, CreateSingle) {
    for (laddr_t lblk = 0; lblk < (laddr_t)npages; ++lblk) {
        paddr_t pblk;
        ssize_t ret = FN(&idx_struct, im_create,
                         &idx_struct, inum, lblk, 1, &pblk);
    }
}

TEST_P(SpeedTestFixturePrecreate, LookupSequentialSingle) {
    for (laddr_t lblk = 0; lblk < (laddr_t)npages; ++lblk) {
        paddr_t pblk;
        ssize_t ret = FN(&idx_struct, im_lookup,
                         &idx_struct, inum, lblk, 1, &pblk);
    }
}
