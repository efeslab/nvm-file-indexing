#include "test_fixture.hpp"

const size_t TestFixture::NBLK   = 1024 * 1024;
const size_t TestFixture::BLK_SZ = 4096;
MockDevice TestFixture::device = MockDevice(TestFixture::NBLK,
                                            TestFixture::BLK_SZ);

int TestFixture::mock_get_dev_info(device_info_t* di) {
    di->di_size_blocks = NBLK;
    di->di_block_size  = BLK_SZ;
    return 0;
}

ssize_t TestFixture::mock_alloc_metadata(size_t nblocks, paddr_t *blk) {
    *blk = device.allocate(nblocks);
    return (ssize_t)(nblocks);
}

ssize_t TestFixture::mock_dealloc_metadata(size_t nblocks, paddr_t blk) {
    device.deallocate(blk, nblocks);
    return (ssize_t)(nblocks);
}

ssize_t TestFixture::mock_alloc_data(size_t nblocks, paddr_t *blk) {
    return mock_alloc_metadata(nblocks, blk);
}

ssize_t TestFixture::mock_dealloc_data(size_t nblocks, paddr_t blk) {
    return mock_dealloc_metadata(nblocks, blk);
}

ssize_t TestFixture::mock_read(paddr_t blk, off_t off, size_t nbytes, char* buf) {
    paddr_t end_blk = blk + ((off + nbytes) / BLK_SZ);
    size_t total = 0;
    ssize_t cret = 0;
    for (paddr_t p = blk; p <= end_blk; ++p) {
        off_t  o = (p == blk) ? off : 0;
        if (o > BLK_SZ) {
            p += (o / BLK_SZ);
            o %= BLK_SZ;
        }
        size_t b = (nbytes - total) > (BLK_SZ - o) ? (BLK_SZ - o) : (nbytes - total);
        ssize_t ret = device[p].read(o, b, buf + total);
        total += b; 
        if (ret < 0) return ret;
        cret += ret;
    }

    return cret;
}

ssize_t TestFixture::mock_write(paddr_t blk, off_t off, size_t nbytes, const char* buf) {
    paddr_t end_blk = blk + ((off + nbytes) / BLK_SZ);
    size_t total = 0;
    ssize_t cret = 0;
    for (paddr_t p = blk; p <= end_blk; ++p) {
        off_t  o = (p == blk) ? off : 0;
        if (o > BLK_SZ) {
            p += (o / BLK_SZ);
            o %= BLK_SZ;
        }
        size_t b = (nbytes - total) > (BLK_SZ - o) ? (BLK_SZ - o) : (nbytes - total);
        ssize_t ret = device[p].write(o, b, buf + total);
        total += b; 
        if (ret < 0) return ret;
        cret += ret;
    }

    return cret;
}

void TestFixture::SetUp() {
   idx_spec.idx_mem_man            = new mem_man_fns_t;
   idx_spec.idx_mem_man->mm_malloc = malloc;
   idx_spec.idx_mem_man->mm_free   = free;

   idx_spec.idx_callbacks                      = new callback_fns_t;
   idx_spec.idx_callbacks->cb_write            = mock_write;
   idx_spec.idx_callbacks->cb_read             = mock_read;
   idx_spec.idx_callbacks->cb_alloc_metadata   = mock_alloc_metadata;
   idx_spec.idx_callbacks->cb_alloc_data       = mock_alloc_data;
   idx_spec.idx_callbacks->cb_dealloc_metadata = mock_dealloc_metadata;
   idx_spec.idx_callbacks->cb_dealloc_data     = mock_dealloc_data;
   idx_spec.idx_callbacks->cb_get_dev_info     = mock_get_dev_info;
}

void TestFixture::TearDown() {
   delete idx_spec.idx_mem_man;
   delete idx_spec.idx_callbacks;
}

/*******************************************************************************
 * Section: Fixture correctness tests.
 *
 * Sanity checks to make sure that the callback methods aren't buggy, etc.
 ******************************************************************************/

TEST_F(TestFixture, SetUpAndTearDown) {
    ASSERT_NE(idx_spec.idx_mem_man, nullptr)   << "Allocation failed";
    ASSERT_NE(idx_spec.idx_callbacks, nullptr) << "Allocation failed";
}

TEST_F(TestFixture, MockMemoryCallbacks) {
    void* mem = idx_spec.idx_mem_man->mm_malloc(1024);
    ASSERT_NE(mem, nullptr);
    idx_spec.idx_mem_man->mm_free(mem);
}

TEST_F(TestFixture, MockPersistentCallbacks) {
    void* mem = idx_spec.idx_mem_man->mm_malloc(1024);
    ASSERT_NE(mem, nullptr);
    idx_spec.idx_mem_man->mm_free(mem);
}

TEST_F(TestFixture, MockWrite) {
    paddr_t pblk;
    size_t  npages = 2;
    size_t  nbytes = BLK_SZ;
    off_t   offset = BLK_SZ / 2;
    ssize_t nalloc = mock_alloc_data(npages, &pblk);
    ASSERT_EQ(npages, nalloc);

    char* mem = (char*)idx_spec.idx_mem_man->mm_malloc(nbytes);
    ASSERT_NE(mem, nullptr);

    memset(mem, 1, nbytes);
    ssize_t ret = mock_write(pblk, offset, nbytes, mem);
    ASSERT_EQ(nbytes, ret);
}

TEST_F(TestFixture, MockWriteBigOffset) {
    paddr_t pblk;
    size_t  npages = 3;
    size_t  nbytes = BLK_SZ;
    off_t   offset = (BLK_SZ * 3 ) / 2;
    ssize_t nalloc = mock_alloc_data(npages, &pblk);
    ASSERT_EQ(npages, nalloc);

    char* mem = (char*)idx_spec.idx_mem_man->mm_malloc(nbytes);
    ASSERT_NE(mem, nullptr);

    memset(mem, 1, nbytes);
    ssize_t ret = mock_write(pblk, offset, nbytes, mem);
    ASSERT_EQ(nbytes, ret);

    char* buf = (char*)idx_spec.idx_mem_man->mm_malloc(nbytes);
    ASSERT_NE(buf, nullptr);

    memset(buf, 0, nbytes);
    ret = mock_read(pblk, offset, nbytes, buf);
    ASSERT_EQ(nbytes, ret);

    ASSERT_EQ(0, strncmp(mem, buf, nbytes));
}

TEST_F(TestFixture, MockRead) {
    paddr_t pblk;
    size_t  npages = 2;
    size_t  nbytes = BLK_SZ;
    off_t   offset = BLK_SZ / 2;
    ssize_t nalloc = mock_alloc_data(npages, &pblk);
    ASSERT_EQ(npages, nalloc);

    char* mem = (char*)idx_spec.idx_mem_man->mm_malloc(nbytes);
    ASSERT_NE(mem, nullptr);
    char* buf = (char*)idx_spec.idx_mem_man->mm_malloc(nbytes);
    ASSERT_NE(buf, nullptr);

    memset(mem, 1, nbytes);
    ssize_t ret = mock_write(pblk, offset, nbytes, mem);
    ASSERT_EQ(nbytes, ret);

    memset(buf, 0, nbytes);
    ret = mock_read(pblk, offset, nbytes, buf);
    ASSERT_EQ(nbytes, ret);

    ASSERT_EQ(0, strncmp(mem, buf, nbytes));
}
