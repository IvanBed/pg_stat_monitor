#include "worker.h"

PG_MODULE_MAGIC;

static pgsmPerQueryLocalStorage  pgsm_per_query_local_storage;
static Latch                    *latch         = NULL;

static bool get_shmem_latch(void);
static bool get_shmem_storage(void);
static void attach_shmem(void);
static dsa_area *get_dsa_area(void);
static pgsmPerQuerySharedStorage * get_per_query_shared_storage(void);


static bool 
get_shmem_latch()
{
    bool found;

    LWLockAcquire(AddinShmemInitLock, LW_EXCLUSIVE);

    latch = ShmemInitStruct("Latch", sizeof(Latch), &found);

    LWLockRelease(AddinShmemInitLock);
    return found;
}

static bool 
get_shmem_storage()
{
    bool found;
    LWLockAcquire(AddinShmemInitLock, LW_EXCLUSIVE);

    pgsm_per_query_local_storage.shared_storage = ShmemInitStruct("PerQuerySharedStorage", sizeof(pgsmPerQuerySharedStorage), &found);

    LWLockRelease(AddinShmemInitLock);
    return found;
}

static void 
attach_shmem(void)
{
    MemoryContext oldcontext;

	if (pgsm_per_query_local_storage.dsa)
		return;
    
	oldcontext = MemoryContextSwitchTo(TopMemoryContext);

	pgsm_per_query_local_storage.dsa = dsa_attach_in_place(pgsm_per_query_local_storage.shared_storage->raw_dsa_area, NULL);
	dsa_pin_mapping(pgsm_per_query_local_storage.dsa);

	MemoryContextSwitchTo(oldcontext);
}

static dsa_area *
get_dsa_area(void)
{
	attach_shmem();
	return pgsm_per_query_local_storage.dsa;
}

static pgsmPerQuerySharedStorage *
get_per_query_shared_storage(void)
{
	return pgsm_per_query_local_storage.shared_storage;
}

static void 
write_data_to_rel()
{
    pgsmPerQuerySharedStorage *shared_storage;
    size_t ret_arr_size;
    int *ret;
    MemoryContext oldcontext;

    //Statistics vars declaration
    dsa_area    *dsa;
    dsa_pointer  dsa_text_pointer;
	
    char	     *query_text;
    char	     *plan_info_text;    
    
    shared_storage = get_per_query_shared_storage();
    dsa            = get_dsa_area();
    
    // Use TopMemoryContext to avoid mem leaks
    oldcontext = MemoryContextSwitchTo(TopMemoryContext);
    
    ret_arr_size   = sizeof(int) * shared_storage->store_capacity;
    ret            = (int*) palloc(ret_arr_size);

    if (!ret)
    {
        elog(WARNING, "Could not allocate memort for return codes array"); 
    }

    memset(ret, 0, ret_arr_size);
    
    SetCurrentStatementStartTimestamp();
    StartTransactionCommand();
    SPI_connect();
    PushActiveSnapshot(GetTransactionSnapshot());

    LWLockAcquire(shared_storage->lock, LW_SHARED);
	
    query_text    = NULL;
    plan_info_text = NULL;

    for(size_t i = 0; i < shared_storage->store_capacity; i++)
    {
        if (shared_storage->free_space_bitmap[i] == ALLOCATED)
        {
            StringInfoData buf;
            initStringInfo(&buf);
            
            query_text = dsa_get_address(dsa, shared_storage->store[i].query_text.query_pos);
            // make a query to db
            //appendStringInfo(&buf, "INSERT INTO %s (id, name) VALUES (%d, '%s')", TABLE_NAME, shared_storage->store[i].id, query_text);
            
            ret[i] = SPI_execute(buf.data, false, 0);
            pfree(buf.data);
        }
    }

    LWLockRelease(shared_storage->lock);

    PopActiveSnapshot();
    SPI_finish();
    CommitTransactionCommand();
    
    // function from pg_stat_per_query
    pgsm_cleanup_storage(shared_storage, dsa, ret);
    
    pfree(ret);

    MemoryContextSwitchTo(oldcontext);
}

PGDLLEXPORT void 
worker_main(Datum main_arg)
{
    // using args i can pass a db name
    
    pqsignal(SIGHUP, SignalHandlerForConfigReload);
    pqsignal(SIGTERM, die);
    BackgroundWorkerUnblockSignals();

    /*add error handling*/
    if (get_shmem_latch())
    {
        //elog(FATAL, "Please use shared_preload_libraries");
    }

    if (get_shmem_storage())
    {
        //elog(FATAL, "Please use shared_preload_libraries");
    }

    BackgroundWorkerInitializeConnection("postgres", NULL, 0);

    // It gives ownership of a shared memory latch to the worker
    OwnLatch(latch);
    
    for (;;)
    {
        // wait the signal or timeout
        (void) WaitLatch(latch, WL_LATCH_SET | WL_TIMEOUT | WL_EXIT_ON_PM_DEATH, 10000, PG_WAIT_EXTENSION);
        ResetLatch(latch);

        CHECK_FOR_INTERRUPTS();

        if (ConfigReloadPending)
        {
            ConfigReloadPending = false;
            ProcessConfigFile(PGC_SIGHUP);
        }
        //write_data_to_rel();

    }
}

void 
_PG_init()
{
    if(!process_shared_preload_libraries_in_progress)
        elog(FATAL, "Please use shared_preload_libraries");

}