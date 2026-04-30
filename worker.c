#include "worker.h"

PG_MODULE_MAGIC;

static pgsmPerQueryLocalStorage  pgsm_per_query_local_storage;
static Latch                    *latch         = NULL;

bool get_shmem_latch()
{
    bool found;

    LWLockAcquire(AddinShmemInitLock, LW_EXCLUSIVE);

    latch = ShmemInitStruct("Latch", sizeof(Latch), &found);

    LWLockRelease(AddinShmemInitLock);
    return found;
}

bool get_shmem_storage()
{
    bool found;
    LWLockAcquire(AddinShmemInitLock, LW_EXCLUSIVE);

    pgsm_per_query_local_storage.shared_storage = ShmemInitStruct("PerQuerySharedStorage", sizeof(pgsmPerQuerySharedStorage), &found);

    LWLockRelease(AddinShmemInitLock);
    return found;
}

void attach_shmem(void)
{
    MemoryContext oldcontext;

	if (pgsm_per_query_local_storage.dsa)
		return;
    
	oldcontext = MemoryContextSwitchTo(TopMemoryContext);

	pgsm_per_query_local_storage.dsa = dsa_attach_in_place(pgsm_per_query_local_storage.shared_storage->raw_dsa_area, NULL);
	dsa_pin_mapping(pgsm_per_query_local_storage.dsa);

	MemoryContextSwitchTo(oldcontext);
}

dsa_area *get_dsa_area_for_text(void)
{
	attach_shmem();
	return pgsm_per_query_local_storage.dsa;
}

void write_data_to_rel()
{
    pgsmPerQuerySharedStorage *storage;

    //Statistics vars declaration
    dsa_area    *dsa;
    dsa_pointer  dsa_text_pointer;
	
    char	     *query_text;
    char	     *plan_info_text;    
    
    
    
    storage = pgsm_per_query_local_storage.shared_storage;
    
    MemoryContext oldcontext;
    size_t ret_arr_size = sizeof(int) * storage->store_capacity;
    
    oldcontext = MemoryContextSwitchTo(TopMemoryContext);
    int *ret            = (int*) palloc(ret_arr_size);

    if (!ret)
    {
        elog(WARNING, "Could not allocate memort for return codes array"); 
    }
    memset(ret, 0, ret_arr_size);
    
    SetCurrentStatementStartTimestamp();
    
    StartTransactionCommand();
    SPI_connect();
    PushActiveSnapshot(GetTransactionSnapshot());

    LWLockAcquire(storage->lock, LW_SHARED);
    

    dsa = get_dsa_area_for_text();
	
    query_text    = NULL;
    lan_info_text = NULL;

    for(size_t i = 0; i < storage->store_capacity; i++)
    {
        if (storage->free_space_bitmap[i] == ALLOCATED)
        {
            StringInfoData buf;
            initStringInfo(&buf);
            
            text = dsa_get_address(dsa, storage->store[i].test_text.text_pos);
            // make a query to db
            //appendStringInfo(&buf, "INSERT INTO %s (id, name) VALUES (%d, '%s')", TABLE_NAME, storage->store[i].id, text);
            
            ret[i] = SPI_execute(buf.data, false, 0);
            pfree(buf.data);
        }
    }

    LWLockRelease(storage->lock);

    PopActiveSnapshot();
    SPI_finish();
    CommitTransactionCommand();
    
    // function from pg_stat_per_query
    cleanup_storage(local_storage, ret);
    
    pfree(ret);
    MemoryContextSwitchTo(oldcontext);
}

PGDLLEXPORT void worker_main(Datum main_arg)
{
    pqsignal(SIGHUP, SignalHandlerForConfigReload);
    pqsignal(SIGTERM, die);
    BackgroundWorkerUnblockSignals();

    /*add error handling*/
    if (get_shmem_latch())
    {

    }

    if (get_shmem_storage())
    {

    }

    // Подумать как прокинуть OID db динамически
    BackgroundWorkerInitializeConnection("postgres", NULL, 0);

    // Передает право владения лэтчем из разделяемой памяти воркеру
    OwnLatch(latch);
    
    for (;;)
    {
        // Ожидание будет до сигнала от основного процесса
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

void _PG_init()
{
    if(!process_shared_preload_libraries_in_progress)
        elog(FATAL, "Please use shared_preload_libraries");

}