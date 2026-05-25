#include "pg_stat_per_query_storage.h"

PG_MODULE_MAGIC;

static int 
get_offset(pgsmPerQuerySharedStorage *shared_storage)
{
    int offset;
    offset = STORAGE_FULL;
    LWLockAcquire(shared_storage->lock, LW_SHARED);
    if (shared_storage->size < shared_storage->store_capacity)
        offset = shared_storage->size;
    
    LWLockRelease(shared_storage->lock);
    return offset;
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
add_el_internal(pgsmPerQuerySharedStorage *shared_storage, dsa_area *dsa, pgsmPerQueryEntry *entry, size_t offset)
{
    /*query vars*/   
    size_t      query_len;
    char       *query_text;
    /*plan info vars*/
    size_t      plan_len;
    char       *plan_text;
    /*plan info vars*/
    size_t      rels_len;
    char       *rels_text;        
    /*locks info vars*/
    size_t      locks_len;
    char       *locks_text;     

    if (!entry)
    {
        elog(NOTICE, "enrty is null!");
        return;
    }

    query_text = entry->query_text.query_pointer;
    if (!query_text) query_text = "";
    query_len  = strlen(query_text); 

    plan_text  = entry->plan_info_text.plan_info_pointer;
    if (!plan_text) plan_text = "";
    plan_len   = strlen(plan_text); 

    rels_text  = entry->rel_info_text.rel_info_pointer;
    if (!rels_text) rels_text = "";
    rels_len   = strlen(rels_text); 

    locks_text = entry->locks_info_text.locks_info_pointer;
    if (!locks_text) locks_text = "";
    locks_len  = strlen(locks_text); 

    LWLockAcquire(shared_storage->lock, LW_EXCLUSIVE);
    
    if (!dsa_store(dsa, query_text, query_len, &(entry->query_text.query_pos)))
        elog(NOTICE, "Could not add query text into the DSA");

    if (!dsa_store(dsa, plan_text, plan_len, &(entry->plan_info_text.plan_info_pos)))
        elog(NOTICE, "Could not add plan text into the DSA");

    if (!dsa_store(dsa, rels_text, rels_len, &(entry->rel_info_text.rel_info_pos)))
        elog(NOTICE, "Could not add rels text into the DSA");

    if (!dsa_store(dsa, locks_text, locks_len, &(entry->locks_info_text.locks_info_pos)))
        elog(NOTICE, "Could not add locks info text into the DSA");

    memcpy(shared_storage->store + offset, entry, sizeof(pgsmPerQueryEntry));       
    shared_storage->size++;
    LWLockRelease(shared_storage->lock);
}

PGDLLEXPORT bool 
pgsm_add_per_query_entry(pgsmPerQuerySharedStorage *shared_storage, dsa_area *dsa, pgsmPerQueryEntry *entry)
{
    int offset;

    if (!entry)
        return false;
    
    offset = get_offset(shared_storage);
    if (offset != STORAGE_FULL)
    {
        add_el_internal(shared_storage, dsa, entry, offset);
        return true;
    }
    else
    {
        return false;
    }
}

PGDLLEXPORT void 
pgsm_cleanup_storage(pgsmPerQuerySharedStorage *shared_storage, dsa_area *dsa)
{
    dsa_pointer dsa_query_pointer;
    LWLockAcquire(shared_storage->lock, LW_EXCLUSIVE);
    
    for (size_t i = 0; i < shared_storage->size; i++)
    {

        dsa_query_pointer = (shared_storage->store + i)->query_text.query_pos;
        if(DsaPointerIsValid(dsa))
            dsa_free(dsa, dsa_query_pointer);

        dsa_query_pointer = (shared_storage->store + i)->plan_info_text.plan_info_pos;
        if(DsaPointerIsValid(dsa))
            dsa_free(dsa, dsa_query_pointer);

        dsa_query_pointer = (shared_storage->store + i)->rel_info_text.rel_info_pos;
        if(DsaPointerIsValid(dsa))
            dsa_free(dsa, dsa_query_pointer);

        dsa_query_pointer = (shared_storage->store + i)->locks_info_text.locks_info_pos;
        if(DsaPointerIsValid(dsa))
            dsa_free(dsa, dsa_query_pointer);

    }
    shared_storage->size = 0;
    memset(shared_storage->store, 0, shared_storage->store_capacity * sizeof(pgsmPerQueryEntry));

    LWLockRelease(shared_storage->lock);
}
