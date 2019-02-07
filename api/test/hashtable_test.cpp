#include "hashtable_test.hpp"

using namespace std;

MockDevice HashTableFixture::device =
            MockDevice(HashTableFixture::NBLK, HashTableFixture::BLK_SZ);

/*******************************************************************************
 * Section: Fixture correctness tests.
 *
 * Sanity checks to make sure that the callback methods aren't buggy, etc.
 ******************************************************************************/

TEST_F(HashTableFixture, SetUpAndTearDown) {
    ASSERT_NE(idx_spec.idx_mem_man, nullptr)   << "Allocation failed";
    ASSERT_NE(idx_spec.idx_callbacks, nullptr) << "Allocation failed";
}

TEST_F(HashTableFixture, MockMemoryCallbacks) {
    void* mem = idx_spec.idx_mem_man->mm_malloc(1024);
    ASSERT_NE(mem, nullptr);
    idx_spec.idx_mem_man->mm_free(mem);
}

TEST_F(HashTableFixture, MockPersistentCallbacks) {
    void* mem = idx_spec.idx_mem_man->mm_malloc(1024);
    ASSERT_NE(mem, nullptr);
    idx_spec.idx_mem_man->mm_free(mem);
}


/*******************************************************************************
 * Section: Hashtable correctness tests.
 *
 * Here is where we make sure the hashtable functions properly.
 ******************************************************************************/

TEST_F(HashTableFixture, Init) {
    idx_struct_t hashtable = {0,};
    ASSERT_EQ(hashtable.idx_metadata, nullptr);
    init_hash(&idx_spec, &hashtable);
    ASSERT_NE(hashtable.idx_metadata, nullptr);
}

TEST_F(HashTableFixture, Insert) {
    idx_struct_t hashtable = {0,};
    init_hash(&idx_spec, &hashtable);

    inum_t inum   = 0;
    laddr_t lblk  = 0;
    paddr_t pblk  = 0;
    size_t npages = 1;
    size_t blk_sz = BLK_SZ;

    ssize_t err = mock_alloc_data(blk_sz, &pblk);
    ASSERT_EQ(err, blk_sz);

    int ret = insert_hash(&hashtable, inum, lblk, pblk, npages);
    ASSERT_TRUE(ret);
}

TEST_F(HashTableFixture, Lookup) {
    idx_struct_t hashtable = {0,};
    init_hash(&idx_spec, &hashtable);

    inum_t inum   = 0;
    laddr_t lblk  = 0;
    paddr_t pblk  = 0;
    size_t npages = 1;
    size_t blk_sz = BLK_SZ;

    ssize_t err = mock_alloc_data(blk_sz, &pblk);
    ASSERT_EQ(err, blk_sz);

    int ret = insert_hash(&hashtable, inum, lblk, pblk, npages);
    ASSERT_TRUE(ret);

    paddr_t check_paddr;
    size_t check_size;
    ret = lookup_hash(&hashtable, inum, lblk, &check_paddr, &check_size, false);
    ASSERT_TRUE(ret) << "lookup failed (not forced)!";
    ASSERT_EQ(pblk, check_paddr);
    ASSERT_EQ(npages, check_size);

    ret = lookup_hash(&hashtable, inum, lblk, &check_paddr, &check_size, true);
    ASSERT_TRUE(ret) << "lookup failed (forced)!";
    ASSERT_EQ(pblk, check_paddr);
    ASSERT_EQ(npages, check_size);
}
