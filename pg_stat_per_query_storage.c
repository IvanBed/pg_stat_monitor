#include "pg_stat_per_query_storage.h"

PG_MODULE_MAGIC;

static int find_pos(pgsmPerQuerySharedStorage *shared_storage)
{
    int res_pos = STORAGE_FULL;
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
static void add_el_internal(pgsmPerQuerySharedStorage *shared_storage, dsa_area *dsa, pgsmPerQueryEntry *entry, size_t pos)
{
    if (!entry)
    {
        return;
    }

    char	   *query_buff;
    dsa_pointer dsa_query_pointer;
    
    size_t      query_len;
    char       *query_text;
    
    query_text = entry->query_text.query_pointer;
    query_len  = strlen(query_text); 

    LWLockAcquire(shared_storage->lock, LW_EXCLUSIVE);

    dsa_query_pointer = dsa_allocate_extended(dsa, query_len + 1,  DSA_ALLOC_ZERO);
    if (DsaPointerIsValid(dsa_query_pointer))
    {
        query_buff = dsa_get_address(dsa, dsa_query_pointer);
        memcpy(query_buff, query_text, query_len);
        query_buff[query_len] = 0;
        entry->query_text.query_pos = dsa_query_pointer;
    } 

    memcpy(shared_storage->store + pos, entry, sizeof(pgsmPerQueryEntry));       
    shared_storage->free_space_bitmap[pos] = ALLOCATED;

    LWLockRelease(shared_storage->lock);
}

PGDLLEXPORT bool pgsm_add_per_query_entry(pgsmPerQuerySharedStorage *shared_storage, dsa_area *dsa, pgsmPerQueryEntry *entry)
{
    if (!entry)
        return false;

    //elog(NOTICE, "add_el NEW!");    
    
    int pos = find_pos(shared_storage);
    //elog(NOTICE, "pos %ld", pos);
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

PGDLLEXPORT void cleanup_storage(pgsmPerQuerySharedStorage *shared_storage, dsa_area *dsa, int const *ret)
{
    dsa_pointer dsa_query_pointer;
    LWLockAcquire(shared_storage->lock, LW_EXCLUSIVE);
    
    for (size_t i = 0; i < shared_storage->store_capacity; i++)
    {
        // Удаляем строку из динамической разделяемой памяти и помечаем позиции в store как свободную.
        if (ret[i] == SPI_OK_INSERT)
        {
            dsa_query_pointer = (shared_storage->store + i)->query_text.query_pos;
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