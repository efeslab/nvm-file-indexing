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
    paddr_t *lookupVal = new paddr_t, *lookupSize = new paddr_t;

    cout << "Lookup 1..." << endl;
    nvm_hash_table_lookup(pop, nKey, lookupVal, lookupSize);
    cout << "Lookup val: " << *lookupVal << endl;
    paddr_t badKey = 2;
    cout << "Lookup 2..." << endl;
    nvm_hash_table_lookup(pop, badKey, lookupVal, lookupSize);
    cout << "Lookup val: " << *lookupVal << endl;

    cout << "Inserting (3,6)..." << endl;
    cout << "Success: " << nvm_hash_table_insert(pop, 3, 6) << endl;

    cout << "Inserting (4,6)..." << endl;
    cout << "Success: " << nvm_hash_table_insert(pop, 4, 6) << endl;

    cout << "Lookup 4..." << endl;
    nvm_hash_table_lookup(pop, 4, lookupVal, lookupSize);
    cout << "Lookup val: " << *lookupVal << endl;

    cout << "ht size: " << nvm_hash_table_size(pop) << endl;
 
    
    delete lookupVal;
    delete lookupSize;
}