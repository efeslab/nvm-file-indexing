#include "hashtable_test.hpp"

using namespace std;

MockDevice HashTableFixture::device =
            MockDevice(HashTableFixture::NBLK, HashTableFixture::BLK_SZ);
const size_t HashTableFixture::NBLK   = 1024 * 1024;
const size_t HashTableFixture::BLK_SZ = 4096;

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

/*
 * HashTable initialization tests.
 */

TEST_F(HashTableFixture, InitNew) {
    idx_struct_t hashtable = {0,};
    paddr_t metadata_loc = 0;
    ASSERT_EQ(hashtable.idx_metadata, nullptr);
    init_hash(&idx_spec, &hashtable, &metadata_loc);
    ASSERT_NE(hashtable.idx_metadata, nullptr);
    ASSERT_LT(metadata_loc, NBLK);
}

TEST_F(HashTableFixture, InitExists) {
    idx_struct_t hashtable = {0,};
    paddr_t metadata_loc = 0;
    ASSERT_EQ(hashtable.idx_metadata, nullptr);
    int ret = init_hash(&idx_spec, &hashtable, &metadata_loc);
    ASSERT_NE(hashtable.idx_metadata, nullptr);
    ASSERT_FALSE(ret);

    idx_struct_t hash_copy = hashtable;
    ASSERT_NE(hash_copy.idx_metadata, nullptr);
    int err = init_hash(&idx_spec, &hash_copy, &metadata_loc);
    ASSERT_TRUE(err);
    ASSERT_EQ(0, strncmp((char*)&hash_copy, (char*)&hashtable, sizeof(hashtable)));
}

TEST_F(HashTableFixture, InsertSingle) {
    idx_struct_t hashtable = {0,};
    paddr_t _unused = 0;
    init_hash(&idx_spec, &hashtable, &_unused);

    inum_t inum   = 0;
    laddr_t lblk  = 0;
    paddr_t pblk  = 0;
    size_t npages = 1;
    size_t blk_sz = BLK_SZ;

    ssize_t ret = insert_hash(&hashtable, inum, lblk, npages, &pblk);
    ASSERT_EQ(npages, ret);
    ASSERT_GT(pblk, 0);
}

TEST_F(HashTableFixture, InsertMulti) {
    idx_struct_t hashtable = {0,};
    paddr_t _unused = 0;
    init_hash(&idx_spec, &hashtable, &_unused);

    inum_t inum   = 0;
    laddr_t lblk  = 0;
    paddr_t pblk  = 0;
    size_t npages = 20;
    size_t blk_sz = BLK_SZ;

    ssize_t ret = insert_hash(&hashtable, inum, lblk, npages, &pblk);
    ASSERT_EQ(npages, ret);
    ASSERT_GT(pblk, 0);
}

TEST_F(HashTableFixture, LookupSingle) {
    idx_struct_t hashtable = {0,};
    paddr_t _unused = 0;
    init_hash(&idx_spec, &hashtable, &_unused);

    inum_t inum   = 0;
    laddr_t lblk  = 0;
    paddr_t pblk  = 0;
    size_t npages = 1;
    size_t blk_sz = BLK_SZ;

    ssize_t ret = insert_hash(&hashtable, inum, lblk, npages, &pblk);
    ASSERT_EQ(npages, ret);
    ASSERT_GT(pblk, 0);

    paddr_t check_paddr;
    ssize_t check_size = lookup_hash(&hashtable, inum, lblk, &check_paddr);
    ASSERT_EQ(pblk, check_paddr);
    ASSERT_EQ(npages, check_size);
}

TEST_F(HashTableFixture, LookupMulti) {
    idx_struct_t hashtable = {0,};
    paddr_t _unused = 0;
    init_hash(&idx_spec, &hashtable, &_unused);

    inum_t inum   = 0;
    laddr_t lblk  = 0;
    paddr_t pblk  = 0;
    size_t npages = 20;
    size_t blk_sz = BLK_SZ;

    ssize_t ret = insert_hash(&hashtable, inum, lblk, npages, &pblk);
    ASSERT_EQ(npages, ret);
    ASSERT_GT(pblk, 0);

    for (size_t p = 0; p < npages; ++p) {
        paddr_t check_paddr;
        ssize_t check_size = lookup_hash(&hashtable, inum, lblk + p, &check_paddr);
        ASSERT_EQ(pblk + p, check_paddr);
        ASSERT_EQ(npages - p, check_size);
    }
}

TEST_F(HashTableFixture, EraseSingle) {
    idx_struct_t hashtable = {0,};
    paddr_t _unused = 0;
    init_hash(&idx_spec, &hashtable, &_unused);

    inum_t inum   = 0;
    laddr_t lblk  = 0;
    paddr_t pblk  = 0;
    size_t npages = 1;
    size_t blk_sz = BLK_SZ;

    ssize_t ret = insert_hash(&hashtable, inum, lblk, npages, &pblk);
    ASSERT_EQ(npages, ret);
    ASSERT_GT(pblk, 0);

    ssize_t check_size = erase_hash(&hashtable, inum, lblk, npages);
    ASSERT_EQ(npages, check_size);

    paddr_t check_paddr = 0;
    check_size = lookup_hash(&hashtable, inum, lblk, &check_paddr);
    ASSERT_NE(pblk, check_paddr);
    ASSERT_NE(npages, check_size);
    ASSERT_LE(check_size, 0);
}

TEST_F(HashTableFixture, EraseMulti) {
    idx_struct_t hashtable = {0,};
    paddr_t _unused = 0;
    init_hash(&idx_spec, &hashtable, &_unused);

    inum_t inum   = 0;
    laddr_t lblk  = 0;
    paddr_t pblk  = 0;
    size_t npages = 20;
    size_t blk_sz = BLK_SZ;

    ssize_t ret = insert_hash(&hashtable, inum, lblk, npages, &pblk);
    ASSERT_EQ(npages, ret);
    ASSERT_GT(pblk, 0);

    ssize_t check_size = erase_hash(&hashtable, inum, lblk, npages);
    ASSERT_EQ(npages, check_size);

    paddr_t check_paddr = 0;
    check_size = lookup_hash(&hashtable, inum, lblk, &check_paddr);
    ASSERT_NE(pblk, check_paddr);
    ASSERT_NE(npages, check_size);
    ASSERT_LE(check_size, 0);
}

