#include "mock_device.hpp"

TEST(TestMockPage, Init) {
    size_t psize = 4096;
    MockMemPage mp(psize);
    ASSERT_EQ(mp.size(), psize);
}

TEST(TestMockPage, ReadWriteAligned) {
    size_t psize = 4096;
    char write_buf[psize];
    char read_buf[psize];
    memset(write_buf, 42, psize);
    memset(read_buf, 0, psize);

    MockMemPage mp(psize);
    ssize_t ret = mp.write(0, psize, write_buf);
    ASSERT_EQ(ret, psize);

    ret = mp.read(0, psize, read_buf);
    ASSERT_EQ(ret, psize);

    ASSERT_EQ(0, strncmp(read_buf, write_buf, psize));
}

TEST(TestMockPage, ReadWriteUnaligned) {
    off_t  offset = 37;
    size_t psize  = 4096;
    size_t iosize = psize - offset;
    char write_buf[psize];
    char read_buf[psize];
    memset(write_buf, 42, psize);
    memset(read_buf, 0, psize);

    MockMemPage mp(psize);
    ssize_t ret = mp.write(offset, iosize, write_buf);
    ASSERT_EQ(ret, iosize);

    ret = mp.read(offset, iosize, read_buf);
    ASSERT_EQ(ret, iosize);

    ASSERT_EQ(0, strncmp(read_buf, write_buf, iosize));
}

TEST(TestMockDevice, Init) {
    size_t nblocks = 10;
    size_t blksz   = 4096;
    MockDevice md(nblocks, blksz);
    ASSERT_EQ(nblocks, md.device.size());
    ASSERT_EQ(nblocks, md.allocated.size());

    char* prev_end = md.raw_device.data();
    for (const auto& mp : md.device) {
        ASSERT_EQ(blksz, mp.size());
        ASSERT_EQ(prev_end, mp.data());
        // Ensure they're also contiguous.
        prev_end = mp.data() + mp.size();
    }
}

TEST(TestMockDevice, Alloc) {
    size_t nblocks = 10;
    size_t blksz   = 4096;
    MockDevice md(nblocks, blksz);

    for (paddr_t i = 0; i < nblocks; ++i) {
        ASSERT_EQ(i, md.allocate(1));
        ASSERT_TRUE(md.allocated[i]);
    }
}

TEST(TestMockDevice, Dealloc) {
    size_t nblocks = 10;
    size_t blksz   = 4096;
    MockDevice md(nblocks, blksz);

    for (paddr_t i = 0; i < nblocks; ++i) {
        ASSERT_EQ(i, md.allocate(1));
        ASSERT_TRUE(md.allocated[i]);
    }

    for (paddr_t i = 0; i < nblocks; ++i) {
        md.deallocate(i, 1);
        ASSERT_FALSE(md.allocated[i]);
    }
}
