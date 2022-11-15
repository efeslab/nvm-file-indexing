#ifndef __NVM_IDX_COMMON_PERSISTENCE_H__
#define __NVM_IDX_COMMON_PERSISTENCE_H__

#include <libpmem.h>

#define nvm_persist_struct(s) pmem_persist((void*)&(s), sizeof(s))
#define nvm_persist_struct_ptr(p) pmem_persist((void*)(p), sizeof(*p))

#endif