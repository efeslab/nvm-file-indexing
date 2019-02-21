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
    return device[blk].read(off, nbytes, buf);
}

ssize_t TestFixture::mock_write(paddr_t blk, off_t off, size_t nbytes, const char* buf) {
    return device[blk].write(off, nbytes, buf);
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


