
#include <libpmemobj.h>
#include "objghash.h"


int main() {
    char* filename = "test_1";
    PMEMobjpool *pop = nvm_hash_table_new(NULL, filename, 128);

    paddr_t nKey = 1;
    paddr_t nVal = 2;
    paddr_t nKey2 = 3;
    paddr_t nVal2 = 5;
    paddr_t *lookupVal = (paddr_t*)malloc(sizeof(paddr_t));
    paddr_t *lookupSize = (paddr_t*)malloc(sizeof(paddr_t));

    printf("Lookup 1...\n");
    nvm_hash_table_lookup(pop, nKey, lookupVal, lookupSize);
    printf("Lookup val: %u\n", *lookupVal);
    paddr_t badKey = 2;
    printf("Lookup 2...\n");
    nvm_hash_table_lookup(pop, badKey, lookupVal, lookupSize);
    printf("Lookup val: %u\n", *lookupVal);

    printf("Inserting (3,6)...\n");
    printf("Success: %u\n", nvm_hash_table_insert(pop, 3, 6));

    printf("Inserting (4,6)...\n");
    printf("Success: %u\n", nvm_hash_table_insert(pop, 4, 6));

    printf("Lookup 4...\n");
    nvm_hash_table_lookup(pop, 4, lookupVal, lookupSize);
    printf("Lookup val: %u\n", *lookupVal);

    printf("ht size: %u\n", nvm_hash_table_size(pop));
 
    
    free(lookupVal);
    free(lookupSize);
}