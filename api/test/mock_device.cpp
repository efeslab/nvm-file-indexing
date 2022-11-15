#include "mock_device.hpp"

TEST(TestMockDevice, Init) {
    size_t nblocks = 10;
    size_t blksz   = 4096;
    MockDevice md(nblocks, blksz);
    ASSERT_EQ(nblocks, md.allocated_.size());
}

TEST(TestMockDevice, Alloc) {
    size_t nblocks = 10;
    size_t blksz   = 4096;
    MockDevice md(nblocks, blksz);

    for (paddr_t i = 0; i < nblocks; ++i) {
        ASSERT_EQ(i, md.allocate(1));
        ASSERT_TRUE(md.allocated_[i]);
    }

    ASSERT_EQ(nblocks, md.num_allocated());
}

TEST(TestMockDevice, Dealloc) {
    size_t nblocks = 10;
    size_t blksz   = 4096;
    MockDevice md(nblocks, blksz);

    for (paddr_t i = 0; i < nblocks; ++i) {
        ASSERT_EQ(i, md.allocate(1));
        ASSERT_TRUE(md.allocated_[i]);
    }

    for (paddr_t i = 0; i < nblocks; ++i) {
        md.deallocate(i, 1);
        ASSERT_FALSE(md.allocated_[i]);
    }

    ASSERT_EQ(0, md.num_allocated());
}
