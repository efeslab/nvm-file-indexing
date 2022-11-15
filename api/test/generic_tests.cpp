#include "generic_tests.hpp"

#include <map>

using namespace std;

INSTANTIATE_TEST_CASE_P(AllStructures, 
                        GenericTestFixture,
                        ::testing::Values(&extent_tree_fns, 
                                          &hash_fns, 
                                          &levelhash_fns,
                                          &radixtree_fns,
                                          &cuckoohash_fns));

TEST_P(GenericTestFixture, InitNoError) {
    ASSERT_EQ(0, init_err);
}

void printme(const map<laddr_t, paddr_t>& m) {
    cout << "Printing map, size " << m.size() << endl;
    for (const auto& i : m) {
        cout << i.first << " \u2192 " << i.second << endl;
    }
}

TEST_P(GenericTestFixture, InsertPersistCheckEach) {
    inum_t inum = 17;
    size_t npages = 50;

    map<laddr_t, paddr_t> mapping;

    for (laddr_t lblk = 0; lblk < (laddr_t)npages; ++lblk) {
        paddr_t pblk;
        ssize_t ret = FN(&idx_struct, im_create,
                         &idx_struct, inum, lblk, 1, &pblk);

        mapping[lblk] = pblk;
        ASSERT_EQ(1, ret) << "Insert: " << lblk << ": " << pblk;

        if (idx_struct.idx_fns->im_clear_metadata) {
            FN(&idx_other, im_clear_metadata, &idx_other);
        }

        // Make sure no entries go missing during insert.
        for (laddr_t l = 0; l <= lblk; ++l) {
            ASSERT_EQ(1, mapping.count(l)) << "This should never happen";

            paddr_t p;
            ssize_t r = FN(&idx_other, im_lookup,
                           &idx_other, inum, l, npages - l, &p);


            ASSERT_LE(1, r) << "\"" << strerror(-r) << "\" on lblk " << l 
                                            << " after insertion of " << lblk;
            ASSERT_LE(ret, npages - l);
            ASSERT_TRUE(mapping[l] == p) << l << " != " << p << ", is " 
                << mapping[l] << " [lblk=" << lblk << "]";
        }
    }

    device.deallocate();
}

TEST_P(GenericTestFixture, InsertPersistCheckEnd) {
    inum_t inum = 17;
    size_t npages = 1000;

    map<laddr_t, paddr_t> mapping;

    for (laddr_t lblk = 0; lblk < (laddr_t)npages; ++lblk) {
        paddr_t pblk;
        ssize_t ret = FN(&idx_struct, im_create,
                         &idx_struct, inum, lblk, 1, &pblk);

        mapping[lblk] = pblk;
        ASSERT_EQ(1, ret) << "Insert: " << lblk << ": " << pblk;

    }

    for (laddr_t l = 0; l < (laddr_t)npages; ++l) {
        ASSERT_EQ(1, mapping.count(l)) << "This should never happen";

        paddr_t p;
        ssize_t r = FN(&idx_struct, im_lookup,
                       &idx_struct, inum, l, npages - l, &p);

        ASSERT_LE(1, r) << strerror(-r) << " on lblk " << l;
        ASSERT_TRUE(mapping[l] == p);
    }

    if (idx_struct.idx_fns->im_clear_metadata) {
        FN(&idx_other, im_clear_metadata, &idx_other);
    }

    for (laddr_t l = 0; l < (laddr_t)npages; ++l) {
        ASSERT_EQ(1, mapping.count(l)) << "This should never happen";

        paddr_t p;
        ssize_t r = FN(&idx_other, im_lookup,
                       &idx_other, inum, l, npages - l, &p);

        ASSERT_LE(1, r) << strerror(-r) << " on lblk " << l;
        ASSERT_TRUE(mapping[l] == p) << l << " != " << p;
    }

    device.deallocate();
}

TEST_P(GenericTestFixture, InsertPersistFragmentedCheckEnd) {
    inum_t inum = 17;
    size_t npages = 4096;

    map<laddr_t, paddr_t> mapping;

    for (laddr_t lblk = 0; lblk < (laddr_t)npages; ++lblk) {
        paddr_t pblk;
        ssize_t ret = FN(&idx_struct, im_create,
                         &idx_struct, inum, lblk, 1, &pblk);

        mapping[lblk] = pblk;
        ASSERT_EQ(1, ret) << "Insert: " << lblk << ": " << pblk;

        device.allocate(1);

    }

    for (laddr_t l = 0; l < (laddr_t)npages; ++l) {
        ASSERT_EQ(1, mapping.count(l)) << "This should never happen";

        paddr_t p;
        ssize_t r = FN(&idx_struct, im_lookup,
                       &idx_struct, inum, l, npages - l, &p);

        ASSERT_LE(1, r) << strerror(-r) << " on lblk " << l;
        ASSERT_TRUE(mapping[l] == p);
    }

    if (idx_struct.idx_fns->im_clear_metadata) {
        FN(&idx_other, im_clear_metadata, &idx_other);
    }

    for (laddr_t l = 0; l < (laddr_t)npages; ++l) {
        ASSERT_EQ(1, mapping.count(l)) << "This should never happen";

        paddr_t p;
        ssize_t r = FN(&idx_other, im_lookup,
                       &idx_other, inum, l, npages - l, &p);

        ASSERT_LE(1, r) << strerror(-r) << " on lblk " << l;
        ASSERT_TRUE(mapping[l] == p) << l << " != " << p;
    }

    device.deallocate();
}

TEST_P(GenericTestFixture, InsertPersistThenRemoveAll) {
    inum_t inum = 17;
    size_t npages = 100;

    map<laddr_t, paddr_t> mapping;

    for (laddr_t lblk = 0; lblk < (laddr_t)npages; ++lblk) {
        paddr_t pblk;
        ssize_t ret = FN(&idx_struct, im_create,
                         &idx_struct, inum, lblk, 1, &pblk);

        mapping[lblk] = pblk;
        ASSERT_EQ(1, ret) << "Insert: " << lblk << ": " << pblk;
    }

    if (idx_struct.idx_fns->im_clear_metadata) {
        FN(&idx_other, im_clear_metadata, &idx_other);
    }

    for (laddr_t lblk = 0; lblk < (laddr_t)npages; ++lblk) {
        paddr_t pblk;
        ssize_t ret = FN(&idx_other, im_lookup,
                         &idx_other, inum, lblk, npages - lblk, &pblk);

        ASSERT_LE(1, ret) << strerror(-ret) << " on lblk " << lblk;
        ASSERT_LE(ret, npages - lblk);
        ASSERT_EQ(mapping[lblk], pblk);
    }

    ssize_t ret = FN(&idx_struct, im_remove,
                     &idx_struct, inum, 0, npages);
    ASSERT_EQ(ret, npages);

    if (idx_struct.idx_fns->im_clear_metadata) {
        FN(&idx_other, im_clear_metadata, &idx_other);
    }

    // Do another lookup, make sure they are all gone.
    for (laddr_t lblk = 0; lblk < (laddr_t)npages; ++lblk) {
        paddr_t pblk;
        ssize_t ret = FN(&idx_other, im_lookup,
                         &idx_other, inum, lblk, 1, &pblk);

        ASSERT_LE(ret, 0) << "lblk == " << lblk;
        ASSERT_EQ(0, pblk);
    }

    device.deallocate();
}

TEST_P(GenericTestFixture, InsertPersistThenRemoveAllRepeat) {
    inum_t inum = 17;
    size_t npages = 100;

    map<laddr_t, paddr_t> mapping;

    for (int rep = 0; rep < 5; ++rep) {
        for (laddr_t lblk = 0; lblk < (laddr_t)npages; ++lblk) {
            paddr_t pblk;
            ssize_t ret = FN(&idx_struct, im_create,
                             &idx_struct, inum, lblk, 1, &pblk);

            mapping[lblk] = pblk;
            ASSERT_EQ(1, ret) << "Insert: " << lblk << "->" << pblk << 
                " (rep==" << rep << ")";
        }

        if (idx_struct.idx_fns->im_clear_metadata) {
            FN(&idx_other, im_clear_metadata, &idx_other);
        }

        for (laddr_t lblk = 0; lblk < (laddr_t)npages; ++lblk) {
            paddr_t pblk;
            ssize_t ret = FN(&idx_other, im_lookup,
                             &idx_other, inum, lblk, npages - lblk, &pblk);

            ASSERT_LE(1, ret) << strerror(-ret) << " on lblk " << lblk;
            ASSERT_LE(ret, npages - lblk);
            ASSERT_EQ(mapping[lblk], pblk);
        }

        ssize_t ret = FN(&idx_struct, im_remove,
                         &idx_struct, inum, 0, npages);
        ASSERT_EQ(ret, npages);

        if (idx_struct.idx_fns->im_clear_metadata) {
            FN(&idx_other, im_clear_metadata, &idx_other);
        }

        // Do another lookup, make sure they are all gone.
        for (laddr_t lblk = 0; lblk < (laddr_t)npages; ++lblk) {
            paddr_t pblk;
            ssize_t ret = FN(&idx_other, im_lookup,
                             &idx_other, inum, lblk, 1, &pblk);

            ASSERT_LE(ret, 0) << "lblk == " << lblk << ", rep == " << rep;
            ASSERT_EQ(0, pblk);
        }
    }

    device.deallocate();
}

TEST_P(GenericTestFixture, InsertPersistThenRemoveAllRepeatDeep) {
    inum_t inum = 17;
    size_t npages = 1000;

    map<laddr_t, paddr_t> mapping;

    for (int rep = 0; rep < 5; ++rep) {
        for (laddr_t lblk = 0; lblk < (laddr_t)npages; ++lblk) {
            paddr_t pblk;
            ssize_t ret = FN(&idx_struct, im_create,
                             &idx_struct, inum, lblk, 1, &pblk);

            mapping[lblk] = pblk;
            ASSERT_EQ(1, ret) << "Insert: " << lblk << "->" << pblk << 
                " (rep==" << rep << ")";
        }

        if (idx_struct.idx_fns->im_clear_metadata) {
            FN(&idx_other, im_clear_metadata, &idx_other);
        }

        for (laddr_t lblk = 0; lblk < (laddr_t)npages; ++lblk) {
            paddr_t pblk;
            ssize_t ret = FN(&idx_other, im_lookup,
                             &idx_other, inum, lblk, npages - lblk, &pblk);

            ASSERT_LE(1, ret) << strerror(-ret) << " on lblk " << lblk <<
                ", rep == " << rep;
            ASSERT_LE(ret, npages - lblk);
            ASSERT_EQ(mapping[lblk], pblk);
        }

        ssize_t ret = FN(&idx_struct, im_remove,
                         &idx_struct, inum, 0, npages);
        ASSERT_EQ(ret, npages);

        if (idx_struct.idx_fns->im_clear_metadata) {
            FN(&idx_other, im_clear_metadata, &idx_other);
        }

        // Do another lookup, make sure they are all gone.
        for (laddr_t lblk = 0; lblk < (laddr_t)npages; ++lblk) {
            paddr_t pblk;
            ssize_t ret = FN(&idx_other, im_lookup,
                             &idx_other, inum, lblk, 1, &pblk);

            ASSERT_LE(ret, 0) << "lblk == " << lblk << ", rep == " << rep;
            ASSERT_EQ(0, pblk);
        }
    }

    device.deallocate();
}

TEST_P(GenericTestFixture, InsertPersistThenRemoveSome) {
    inum_t inum    = 17;
    size_t npages  = 100;
    size_t nremove = 80;
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

    if (idx_struct.idx_fns->im_clear_metadata) {
        FN(&idx_other, im_clear_metadata, &idx_other);
    }

    for (laddr_t lblk = start; lblk < (laddr_t)npages + start; ++lblk) {
        paddr_t pblk;
        ssize_t ret = FN(&idx_other, im_lookup,
                         &idx_other, inum, lblk, npages - (lblk - start), &pblk);

        ASSERT_LE(1, ret) << strerror(-ret) << " on lblk " << lblk;
        ASSERT_LE(ret, npages - lblk);
        ASSERT_EQ(mapping[lblk], pblk);
    }

    ssize_t ret = FN(&idx_struct, im_remove,
                     &idx_struct, inum, remove, nremove);
    ASSERT_EQ(ret, nremove);

    if (idx_struct.idx_fns->im_clear_metadata) {
        FN(&idx_other, im_clear_metadata, &idx_other);
    }

    // Do another lookup for the remainder.
    for (laddr_t lblk = start; lblk < (laddr_t)nremain + start; ++lblk) {
        paddr_t pblk;
        ssize_t ret = FN(&idx_other, im_lookup,
                         &idx_other, inum, lblk, npages - lblk, &pblk);

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
    size_t npages = 100;
    paddr_t lblk = 0;
    paddr_t pblk;

    ssize_t ret = FN(&idx_struct, im_create,
                     &idx_struct, inum, lblk, npages, &pblk);

    ASSERT_EQ(npages, ret) << "Insert: " << lblk << ": " << pblk;
    ASSERT_NE(0, pblk);

    if (idx_struct.idx_fns->im_clear_metadata) {
        FN(&idx_other, im_clear_metadata, &idx_other);
    }

    for (laddr_t l = lblk; l < (laddr_t)npages + lblk; ++l) {
        paddr_t lookup_pblk;
        ssize_t ret = FN(&idx_other, im_lookup,
                         &idx_other, inum, l, npages-l, &lookup_pblk);

        ASSERT_EQ(ret, npages - l) << "lookup " << l;
        ASSERT_EQ(pblk + l, lookup_pblk);
    }

    ret = FN(&idx_struct, im_remove, &idx_struct, inum, lblk, npages);
    ASSERT_EQ(ret, npages) << "Remove error: " << strerror(-ret);

    if (idx_struct.idx_fns->im_clear_metadata) {
        FN(&idx_other, im_clear_metadata, &idx_other);
    }

    // Do another lookup, make sure they are all gone.
    for (laddr_t l = 0; l < (laddr_t)npages + lblk; ++l) {
        paddr_t lookup_pblk;
        ssize_t ret = FN(&idx_other, im_lookup,
                         &idx_other, inum, l, 1, &lookup_pblk);

        ASSERT_LE(ret, 0);
        ASSERT_EQ(0, lookup_pblk);
    }

    device.deallocate();
}

TEST_P(GenericTestFixture, Slab_InsertPersistThenRemoveSome) {
    inum_t inum    = 17;
    size_t npages  = 100;
    size_t nremove = 80;
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

    if (idx_struct.idx_fns->im_clear_metadata) {
        FN(&idx_other, im_clear_metadata, &idx_other);
    }

    for (laddr_t lblk = start; lblk < (laddr_t)npages + start; ++lblk) {
        paddr_t pblk;
        ssize_t ret = FN(&idx_other, im_lookup,
                         &idx_other, inum, lblk, npages - lblk, &pblk);

        ASSERT_LE(1, ret) << strerror(-ret) << " on lblk " << lblk;
        ASSERT_LE(ret, npages - lblk);
        ASSERT_EQ(mapping[lblk], pblk);
    }

    ssize_t ret = FN(&idx_struct, im_remove,
                     &idx_struct, inum, remove, nremove);
    ASSERT_EQ(ret, nremove);

    if (idx_struct.idx_fns->im_clear_metadata) {
        FN(&idx_other, im_clear_metadata, &idx_other);
    }

    // Do another lookup for the remainder.
    for (laddr_t lblk = start; lblk < (laddr_t)nremain + start; ++lblk) {
        paddr_t pblk;
        ssize_t ret = FN(&idx_other, im_lookup,
                         &idx_other, inum, lblk, npages - lblk, &pblk);

        ASSERT_LE(1, ret) << strerror(-ret) << " on lblk " << lblk;
        ASSERT_LE(ret, npages - lblk);
        ASSERT_EQ(mapping[lblk], pblk);
    }

    device.deallocate();
}

/*******************************************************************************
 * Section: Tests on small "slab" inserts (some blocks contiguous).
 ******************************************************************************/

TEST_P(GenericTestFixture, SmallSlab_InsertPersistThenRemoveAll) {
    inum_t inum = 17;
    size_t npages = 100;
    size_t inc = 10;
    paddr_t lblk = 0;

    for (laddr_t l = lblk; l < (laddr_t)npages + lblk; l += inc) {
        paddr_t pblk;
        ssize_t ret = FN(&idx_struct, im_create,
                         &idx_struct, inum, l, inc, &pblk);

        ASSERT_EQ(inc, ret) << "Insert: " << l << ": " << pblk;
        ASSERT_NE(0, pblk);
        device.allocate(1); // Ensure the ranges are not contiguous.
    }

    if (idx_struct.idx_fns->im_clear_metadata) {
        FN(&idx_other, im_clear_metadata, &idx_other);
    }

    for (laddr_t l = lblk; l < (laddr_t)npages + lblk; ++l) {
        paddr_t lookup_pblk;
        ssize_t ret = FN(&idx_other, im_lookup,
                         &idx_other, inum, l, inc - (l % inc), &lookup_pblk);

        ASSERT_EQ(ret, inc - (l % inc) ) << "lookup " << l;
    }

    ssize_t ret = FN(&idx_struct, im_remove, &idx_struct, inum, lblk, npages);
    ASSERT_EQ(ret, npages) << "Remove error: " << strerror(-ret);

    if (idx_struct.idx_fns->im_clear_metadata) {
        FN(&idx_other, im_clear_metadata, &idx_other);
    }

    // Do another lookup, make sure they are all gone.
    for (laddr_t l = 0; l < (laddr_t)npages + lblk; ++l) {
        paddr_t lookup_pblk;
        ssize_t ret = FN(&idx_other, im_lookup,
                         &idx_other, inum, l, 1, &lookup_pblk);

        ASSERT_LE(ret, 0);
        ASSERT_EQ(0, lookup_pblk);
    }

    device.deallocate();
}

