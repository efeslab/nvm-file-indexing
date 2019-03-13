#include "generic_tests.hpp"

#include <map>

using namespace std;

TEST_P(GenericTestFixture, InitNoError) {
    ASSERT_EQ(0, init_err);
}

void printme(const map<laddr_t, paddr_t>& m) {
    cout << "Printing map, size " << m.size() << endl;
    for (const auto& i : m) {
        cout << i.first << " \u2192 " << i.second << endl;
    }
}

TEST_P(GenericTestFixture, InsertPersist) {
    inum_t inum = 17;
    size_t npages = 1000;

    map<laddr_t, paddr_t> mapping;

    for (laddr_t lblk = 0; lblk < (laddr_t)npages; ++lblk) {
        paddr_t pblk;
        ssize_t ret = FN(&idx_struct, im_create,
                         &idx_struct, inum, lblk, 1, &pblk);

        mapping[lblk] = pblk;
        ASSERT_EQ(1, ret) << "Insert: " << lblk << ": " << pblk;

        // Make sure no entries go missing during insert.
        for (laddr_t l = 0; l <= lblk; ++l) {
            ASSERT_EQ(1, mapping.count(l)) << "This should never happen";

            paddr_t p;
            ssize_t r = FN(&idx_other, im_lookup,
                           &idx_other, inum, l, &p);

            printme(mapping);

            ASSERT_LE(1, r) << strerror(-r) << " on lblk " << l 
                                            << " after insertion of " << lblk;
            ASSERT_LE(ret, npages - l);
            ASSERT_TRUE(mapping[l] == p);
        }
    }

    device.deallocate();
}

TEST_P(GenericTestFixture, InsertPersistThenRemoveAll) {
    inum_t inum = 17;
    size_t npages = 10000;

    map<laddr_t, paddr_t> mapping;

    for (laddr_t lblk = 0; lblk < (laddr_t)npages; ++lblk) {
        paddr_t pblk;
        ssize_t ret = FN(&idx_struct, im_create,
                         &idx_struct, inum, lblk, 1, &pblk);

        mapping[lblk] = pblk;
        ASSERT_EQ(1, ret) << "Insert: " << lblk << ": " << pblk;
    }

    for (laddr_t lblk = 0; lblk < (laddr_t)npages; ++lblk) {
        paddr_t pblk;
        ssize_t ret = FN(&idx_other, im_lookup,
                         &idx_other, inum, lblk, &pblk);

        ASSERT_LE(1, ret) << strerror(-ret) << " on lblk " << lblk;
        ASSERT_LE(ret, npages - lblk);
        ASSERT_EQ(mapping[lblk], pblk);
    }

    ssize_t ret = FN(&idx_struct, im_remove,
                     &idx_struct, inum, 0, npages);
    ASSERT_EQ(ret, npages);


    // Do another lookup, make sure they are all gone.
    for (laddr_t lblk = 0; lblk < (laddr_t)npages; ++lblk) {
        paddr_t pblk;
        ssize_t ret = FN(&idx_other, im_lookup,
                         &idx_other, inum, lblk, &pblk);

        ASSERT_LE(ret, 0);
        ASSERT_EQ(0, pblk);
    }

    device.deallocate();
}

TEST_P(GenericTestFixture, InsertPersistThenRemoveSome) {
    inum_t inum    = 17;
    size_t npages  = 10000;
    size_t nremove = 8000;
    size_t nremain = npages - nremove;
    laddr_t start  = 0;
    laddr_t remove = start + nremain;

    map<laddr_t, paddr_t> mapping;

    for (laddr_t lblk = start; lblk < (laddr_t)npages + start; ++lblk) {
        paddr_t pblk;
        ssize_t ret = FN(&idx_struct, im_create,
                         &idx_struct, inum, lblk, 1, &pblk);

        mapping[lblk] = pblk;
        ASSERT_EQ(1, ret) << "Insert: " << lblk << ": " << pblk;
    }

    for (laddr_t lblk = start; lblk < (laddr_t)npages + start; ++lblk) {
        paddr_t pblk;
        ssize_t ret = FN(&idx_other, im_lookup,
                         &idx_other, inum, lblk, &pblk);

        ASSERT_LE(1, ret) << strerror(-ret) << " on lblk " << lblk;
        ASSERT_LE(ret, npages - lblk);
        ASSERT_EQ(mapping[lblk], pblk);
    }

    ssize_t ret = FN(&idx_struct, im_remove,
                     &idx_struct, inum, remove, nremove);
    ASSERT_EQ(ret, nremove);


    // Do another lookup for the remainder.
    for (laddr_t lblk = start; lblk < (laddr_t)nremain + start; ++lblk) {
        paddr_t pblk;
        ssize_t ret = FN(&idx_other, im_lookup,
                         &idx_other, inum, lblk, &pblk);

        ASSERT_LE(1, ret) << strerror(-ret) << " on lblk " << lblk;
        ASSERT_LE(ret, npages - lblk);
        ASSERT_EQ(mapping[lblk], pblk);
    }

    device.deallocate();
}

/*******************************************************************************
 * Section: Tests on "slab" inserts (many blocks contiguous).
 ******************************************************************************/

TEST_P(GenericTestFixture, Slab_InsertPersistThenRemoveAll) {
    inum_t inum = 17;
    size_t npages = 10000;
    paddr_t lblk = 0;
    paddr_t pblk;

    ssize_t ret = FN(&idx_struct, im_create,
                     &idx_struct, inum, lblk, npages, &pblk);

    ASSERT_EQ(npages, ret) << "Insert: " << lblk << ": " << pblk;
    ASSERT_NE(0, pblk);

    for (laddr_t l = lblk; l < (laddr_t)npages + lblk; ++l) {
        paddr_t lookup_pblk;
        ssize_t ret = FN(&idx_other, im_lookup,
                         &idx_other, inum, l, &lookup_pblk);

        ASSERT_EQ(ret, npages - l) << "lookup " << l;
        ASSERT_EQ(pblk + l, lookup_pblk);
    }

    ret = FN(&idx_struct, im_remove, &idx_struct, inum, lblk, npages);
    ASSERT_EQ(ret, npages);

    // Do another lookup, make sure they are all gone.
    for (laddr_t l = 0; l < (laddr_t)npages + lblk; ++l) {
        paddr_t lookup_pblk;
        ssize_t ret = FN(&idx_other, im_lookup,
                         &idx_other, inum, l, &lookup_pblk);

        ASSERT_LE(ret, 0);
        ASSERT_EQ(0, lookup_pblk);
    }

    device.deallocate();
}
/*
TEST_P(GenericTestFixture, Slab_InsertPersistThenRemoveSome) {
    inum_t inum    = 17;
    size_t npages  = 10000;
    size_t nremove = 8000;
    size_t nremain = npages - nremove;
    laddr_t start  = 0;
    laddr_t remove = start + nremain;

    map<laddr_t, paddr_t> mapping;

    for (laddr_t lblk = start; lblk < (laddr_t)npages + start; ++lblk) {
        paddr_t pblk;
        ssize_t ret = FN(&idx_struct, im_create,
                         &idx_struct, inum, lblk, 1, &pblk);

        mapping[lblk] = pblk;
        ASSERT_EQ(1, ret) << "Insert: " << lblk << ": " << pblk;
    }

    for (laddr_t lblk = start; lblk < (laddr_t)npages + start; ++lblk) {
        paddr_t pblk;
        ssize_t ret = FN(&idx_other, im_lookup,
                         &idx_other, inum, lblk, &pblk);

        ASSERT_LE(1, ret) << strerror(-ret) << " on lblk " << lblk;
        ASSERT_LE(ret, npages - lblk);
        ASSERT_EQ(mapping[lblk], pblk);
    }

    ssize_t ret = FN(&idx_struct, im_remove,
                     &idx_struct, inum, remove, nremove);
    ASSERT_EQ(ret, nremove);


    // Do another lookup for the remainder.
    for (laddr_t lblk = start; lblk < (laddr_t)nremain + start; ++lblk) {
        paddr_t pblk;
        ssize_t ret = FN(&idx_other, im_lookup,
                         &idx_other, inum, lblk, &pblk);

        ASSERT_LE(1, ret) << strerror(-ret) << " on lblk " << lblk;
        ASSERT_LE(ret, npages - lblk);
        ASSERT_EQ(mapping[lblk], pblk);
    }

    device.deallocate();
}
*/

INSTANTIATE_TEST_CASE_P(AllStructures, 
                        GenericTestFixture,
                        ::testing::Values(&extent_tree_fns, 
                                          &hash_fns, 
                                          &levelhash_fns));
