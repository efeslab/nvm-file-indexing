#include <iostream>
#include <libpmemobj.h>
#include <string>
#include <cassert>
#include "objghash.h"

using namespace std;

int main() {
    char* filename = "test_1";
    PMEMobjpool *pop = nvm_hash_table_new(nullptr, filename, 128);

    paddr_t nKey = 1;
    paddr_t nVal = 2;
    paddr_t nKey2 = 3;
    paddr_t nVal2 = 5;
    cout << "Inserting (1,2)..." << endl;
    cout << "Success: " << nvm_hash_table_insert(pop, nKey, nVal) << endl;
    cout << "Inserting (3,5)..." << endl;
    cout << "Success: " << nvm_hash_table_insert(pop, nKey2, nVal2) << endl;
    paddr_t *lookupVal = new paddr_t, *lookupSize = new paddr_t;

    cout << "Lookup 1..." << endl;
    nvm_hash_table_lookup(pop, nKey, lookupVal, lookupSize);
    assert(*lookupVal == 2);
    cout << "Lookup val: " << *lookupVal << endl;
    paddr_t badKey = 2;
    cout << "Lookup 2..." << endl;
    nvm_hash_table_lookup(pop, badKey, lookupVal, lookupSize);
    assert(*lookupVal == 0);
    cout << "Lookup val: " << *lookupVal << endl;

    cout << "Deleting (1,2)..." << endl;
    paddr_t *deletedVal = new paddr_t;
    size_t *rm1 = new size_t, *rm2 = new size_t;
    cout << "Success: " << nvm_hash_table_remove(pop, nKey, deletedVal, rm1, rm2) << endl;
    cout << "Deleted (1," << *deletedVal << ") extra: " << *rm1 << " " << *rm2 << endl;
    cout << "Lookup 1..." << endl;
    nvm_hash_table_lookup(pop, nKey, lookupVal, lookupSize);
    assert(*lookupVal == 0);
    cout << "Lookup val: " << *lookupVal << endl;
    cout << "Deleting (2,-)..." << endl;
    cout << "Success: " << nvm_hash_table_remove(pop, nKey, deletedVal, rm1, rm2) << endl;
    cout << "Deleted (2," << *deletedVal << ") extra: " << *rm1 << " " << *rm2 << endl;

    cout << "Lookup 3..." << endl;
    nvm_hash_table_lookup(pop, nKey2, lookupVal, lookupSize);
    assert(*lookupVal == 5);
    cout << "Lookup val: " << *lookupVal << endl;
    
    cout << "Inserting (1,2)..." << endl;
    cout << "Success: " << nvm_hash_table_insert(pop, nKey, nVal) << endl;

    delete deletedVal;
    delete rm1;
    delete rm2;
    delete lookupVal;
    delete lookupSize;
}