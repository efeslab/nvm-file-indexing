#include "p-hot.h"

idx_fns_t p_hot_fns = {
    .im_init               = NULL,
    .im_init_prealloc      = p_hot_init,
    .im_lookup             = p_hot_lookup,
    .im_create             = p_hot_create,
    .im_remove             = p_hot_remove,

    .im_set_caching        = p_hot_set_caching,
    .im_set_locking        = p_hot_set_locking,
    .im_persist            = p_hot_persist_updates,
    .im_invalidate         = p_hot_invalidate_caches,
};
