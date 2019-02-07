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

TEST_F(HashTableFixture, HashTableInit) {
    idx_struct_t hashtable = {0,};
    ASSERT_EQ(hashtable.idx_metadata, nullptr);
    init_hash(&idx_spec, &hashtable);
    ASSERT_NE(hashtable.idx_metadata, nullptr);
}
