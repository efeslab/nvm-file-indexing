#include "levelhash_test.hpp"

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
