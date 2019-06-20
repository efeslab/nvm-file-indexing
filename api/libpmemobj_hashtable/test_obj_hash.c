#include <libpmemobj.h>
#include "objghash.h"

int main() {
    char* filename = "test_1";
    PMEMobjpool *pop = nvm_hash_table_new(NULL, filename, 128);

    paddr_t nKey = 1;
    paddr_t nVal = 2;
    printf("Inserting (1,2)...\n");
    printf("Success: %u\n", nvm_hash_table_insert(pop, nKey, nVal));
    
    paddr_t nKey2 = 3;
    paddr_t nVal2 = 5;
    printf("Inserting (3,5)...\n");
    printf("Success: %u\n", nvm_hash_table_insert(pop, nKey2, nVal2));
    paddr_t *lookupVal = (paddr_t*)malloc(sizeof(paddr_t));
    paddr_t *lookupSize = (paddr_t*)malloc(sizeof(paddr_t));

    printf("Lookup 1...\n");
    nvm_hash_table_lookup(pop, nKey, lookupVal, lookupSize);
    printf("Lookup val: %u\n", *lookupVal);

    paddr_t badKey = 2;
    printf("Lookup 2...\n");
    nvm_hash_table_lookup(pop, badKey, lookupVal, lookupSize);
    printf("Lookup val: %u\n", *lookupVal);

    printf("Deleting (1,2)...");
    paddr_t *deletedVal = (paddr_t*)malloc(sizeof(paddr_t));
    size_t *rm1 = (size_t *)malloc(sizeof(size_t));
    size_t *rm2 = (size_t *)malloc(sizeof(size_t));
    printf("Success: %u\n", nvm_hash_table_remove(pop, nKey, deletedVal, rm1, rm2));
    printf("Deleted (1,%u)\n", *deletedVal);

    printf("Lookup 1...\n");
    nvm_hash_table_lookup(pop, nKey, lookupVal, lookupSize);
    printf("Lookup val: %u\n", *lookupVal);

    printf("Deleting (2,-)...\n");
    printf("Success: %u\n", nvm_hash_table_remove(pop, nKey, deletedVal, rm1, rm2));
    printf("Deleted (2,%u)\n", *deletedVal);

    printf("Lookup 3..." );
    nvm_hash_table_lookup(pop, nKey2, lookupVal, lookupSize);
    printf("Lookup val: %u\n", *lookupVal);
    
    printf("Inserting (1,2)..." );
    printf("Success: %u\n", nvm_hash_table_insert(pop, nKey, nVal));

    free(lookupVal);
    free(lookupSize);
    free(deletedVal);
    free(rm1);
    free(rm2);
}