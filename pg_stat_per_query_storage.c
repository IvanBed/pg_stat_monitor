#include "pg_stat_per_query_storage.h"

PG_MODULE_MAGIC;

static int 
find_pos(pgsmPerQuerySharedStorage *shared_storage)
{
    int res_pos;
    
    res_pos = STORAGE_FULL;
    LWLockAcquire(shared_storage->lock, LW_SHARED);
    for (size_t i = 0; i < shared_storage->store_capacity; i++)
    {
        if (shared_storage->free_space_bitmap[i] == FREE)
        {
            res_pos = (int) i;
            break;
        }
    }
    LWLockRelease(shared_storage->lock);
    return res_pos;
}

static bool 
dsa_store(dsa_area *dsa, char * text, size_t text_len, dsa_pointer * pos)
{
    char  *buf;
    dsa_pointer dsa_pointer_handle; 
    
    dsa_pointer_handle = dsa_allocate_extended(dsa, text_len + 1,  DSA_ALLOC_ZERO);

    if (DsaPointerIsValid(dsa_pointer_handle))
    {
        buf = dsa_get_address(dsa, dsa_pointer_handle);
        memcpy(buf, text, text_len);
        buf[text_len] = 0;
        *pos = dsa_pointer_handle;
        return true;
    } 
    else 
    {
        return false;
    }
}

static void 
add_el_internal(pgsmPerQuerySharedStorage *shared_storage, dsa_area *dsa, pgsmPerQueryEntry *entry, size_t pos)
{
    /*query vars*/   
    size_t      query_len;
    char       *query_text;
    /*plan info vars*/
    size_t      plan_len;
    char       *plan_text;      
    /*locks info vars*/
    size_t      locks_len;
    char       *locks_text;     

    elog(NOTICE, "add_el_internal!");
    if (!entry)
    {
        return;
    }

    query_text = entry->query_text.query_pointer;
    query_len  = strlen(query_text); 

    plan_text  = entry->plan_info_text.plan_info_pointer;
    plan_len   = strlen(plan_text); 

    locks_text = entry->locks_info_text.locks_info_pointer;
    locks_len  = strlen(locks_text); 

    LWLockAcquire(shared_storage->lock, LW_EXCLUSIVE);
    
    if (!dsa_store(dsa, query_text, query_len, &(entry->query_text.query_pos)))
        elog(NOTICE, "Could not add query text into the DSA");

    if (!dsa_store(dsa, plan_text, plan_len, &(entry->plan_info_text.plan_info_pos)))
        elog(NOTICE, "Could not add plan text into the DSA");
    
    if (!dsa_store(dsa, locks_text, locks_len, &(entry->locks_info_text.locks_info_pos)))
        elog(NOTICE, "Could not add locks info text into the DSA");

    memcpy(shared_storage->store + pos, entry, sizeof(pgsmPerQueryEntry));       
    shared_storage->free_space_bitmap[pos] = ALLOCATED;

    LWLockRelease(shared_storage->lock);
}

PGDLLEXPORT bool 
pgsm_add_per_query_entry(pgsmPerQuerySharedStorage *shared_storage, dsa_area *dsa, pgsmPerQueryEntry *entry)
{
    int pos;

    if (!entry)
        return false;

    elog(NOTICE, "add_el NEW!");     
    pos = find_pos(shared_storage);
    elog(NOTICE, "pos %d", pos);
    if (pos != STORAGE_FULL)
    {
        add_el_internal(shared_storage, dsa, entry, pos);
        return true;
    }
    else
    {
        return false;
    }
}

PGDLLEXPORT void 
pgsm_cleanup_storage(pgsmPerQuerySharedStorage *shared_storage, dsa_area *dsa, int const *ret)
{
    dsa_pointer dsa_query_pointer;
    LWLockAcquire(shared_storage->lock, LW_EXCLUSIVE);
    
    for (size_t i = 0; i < shared_storage->store_capacity; i++)
    {
        // Delete a tuple in the store and mark this position as FREE.
        if (ret[i] == SPI_OK_INSERT)
        {
            dsa_query_pointer = (shared_storage->store + i)->query_text.query_pos;
            if(DsaPointerIsValid(dsa))
                dsa_free(dsa, dsa_query_pointer);

            dsa_query_pointer = (shared_storage->store + i)->plan_info_text.plan_info_pos;
            if(DsaPointerIsValid(dsa))
                dsa_free(dsa, dsa_query_pointer);
            
            dsa_query_pointer = (shared_storage->store + i)->locks_info_text.locks_info_pos;
            if(DsaPointerIsValid(dsa))
                dsa_free(dsa, dsa_query_pointer);

            shared_storage->free_space_bitmap[i] = FREE;
        }
    }

    LWLockRelease(shared_storage->lock);
}
/*
static void
pgsm_lock_aquire(pgsmPerQuerySharedStorage *shared_storage, LWLockMode mode)
{
	LWLockAcquire(shared_storage->lock, mode);
	//disable_error_capture = true;
}

static void
pgsm_lock_release(pgsmPerQuerySharedStorage *shared_storage)
{
	//disable_error_capture = false;
	LWLockRelease(shared_storage->lock);
}
*/