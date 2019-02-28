#include "levelhash_test.hpp"

TEST_F(LevelHashingFixture, LevelInit) {
    ASSERT_EQ(0, init_err);
}

TEST_F(LevelHashingFixture, LevelInsert) {
    inum_t inum   = 0;
    laddr_t lblk  = 0;
    paddr_t pblk  = 0;
    size_t npages = 1;

    ssize_t ret = levelhash_create(&level_idx, inum, lblk, npages, &pblk);
    ASSERT_EQ(npages, ret);
    ASSERT_GT(pblk, 0);
}
