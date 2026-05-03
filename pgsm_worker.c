#include "pgsm_worker.h"

PG_MODULE_MAGIC;

static pgsmPerQueryLocalStorage  pgsm_per_query_local_storage;
static Latch                    *latch         = NULL;

static bool get_shmem_latch(void);
static bool get_shmem_storage(void);
static void attach_shmem(void);
static dsa_area *get_dsa_area(void);
static pgsmPerQuerySharedStorage * get_per_query_shared_storage(void);
static Size pgsm_per_query_area_size(void);
static Size pgsm_get_per_query_shared_size(void);

static Size
pgsm_per_query_area_size(void)
{
	Size sz = DSA_STORE_MAX_SIZE;
	return MAXALIGN(sz);
}

static Size
pgsm_get_per_query_shared_size(void)
{
    Size sz = sizeof(pgsmPerQuerySharedStorage);
    sz += pgsm_per_query_area_size();  
    return sz;
}

static bool 
get_shmem_latch(void)
{
    bool found;

    LWLockAcquire(AddinShmemInitLock, LW_EXCLUSIVE);

    latch = ShmemInitStruct("Latch", sizeof(Latch), &found);

    LWLockRelease(AddinShmemInitLock);
    return found;
}

static bool 
get_shmem_storage(void)
{
    bool found;
    LWLockAcquire(AddinShmemInitLock, LW_EXCLUSIVE);

    pgsm_per_query_local_storage.shared_storage = ShmemInitStruct("PerQuerySharedStorage", pgsm_get_per_query_shared_size(), &found);

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
write_data_to_rel(void)
{
    pgsmPerQuerySharedStorage *shared_storage;
    size_t ret_arr_size;
    int *ret;
    MemoryContext oldcontext;

    //Statistics vars declaration
    dsa_area    *dsa;
    //dsa_pointer  dsa_text_pointer;
	
    char	    *query_text;
    char	    *per_node_plan_info;    
    char        *locks_info;

    shared_storage = get_per_query_shared_storage();
    dsa            = get_dsa_area();
    
    // Use TopMemoryContext to avoid mem leaks
    oldcontext = MemoryContextSwitchTo(TopMemoryContext);
    
    LWLockAcquire(shared_storage->lock, LW_SHARED);

    ret_arr_size   = sizeof(int) * shared_storage->store_capacity;
    ret            = (int*) palloc(ret_arr_size);

    memset(ret, 0, ret_arr_size);
    
    SetCurrentStatementStartTimestamp();
    StartTransactionCommand();
    SPI_connect();
    PushActiveSnapshot(GetTransactionSnapshot());


    for(size_t i = 0; i < shared_storage->store_capacity; i++)
    {
        if (shared_storage->free_space_bitmap[i] == ALLOCATED)
        {
            StringInfoData buf;
            initStringInfo(&buf);
            
            query_text         = dsa_get_address(dsa, shared_storage->store[i].query_text.query_pos);
            per_node_plan_info = dsa_get_address(dsa, shared_storage->store[i].plan_info_text.plan_info_pos);
            locks_info         = dsa_get_address(dsa, shared_storage->store[i].locks_info_text.locks_info_pos);
            // make a query to db
            appendStringInfo(&buf, "INSERT INTO %s (execution_id, query, exec_time, per_node_plan_info, lock_info) VALUES (%ld, $$%s$$, %f, '%s', '%s')", 
                    REL_NAME, shared_storage->store[i].execution_id, 
                            query_text, shared_storage->store[i].counters.time.total_time, per_node_plan_info, locks_info);
            
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
    char *db_name;
    long  timeout;

    // temp init for test
    db_name  = "postgres";
    timeout  = 10000;

    /*add error handling*/
    if (!get_shmem_latch())
    {
        //elog(FATAL, "Please use shared_preload_libraries");
        return;
    }

    if (!get_shmem_storage())
    {
        //elog(FATAL, "Please use shared_preload_libraries");
        return;
    }

    pqsignal(SIGHUP, SignalHandlerForConfigReload);
    pqsignal(SIGTERM, die);
    BackgroundWorkerUnblockSignals();

    BackgroundWorkerInitializeConnection(db_name, NULL, 0);

    // It gives ownership of a shared memory latch to the worker
    OwnLatch(latch);
    
    for (;;)
    {
        // wait the signal or timeout
        (void) WaitLatch(latch, WL_LATCH_SET | WL_TIMEOUT | WL_EXIT_ON_PM_DEATH, timeout, PG_WAIT_EXTENSION);
        ResetLatch(latch);

        CHECK_FOR_INTERRUPTS();

        if (ConfigReloadPending)
        {
            ConfigReloadPending = false;
            ProcessConfigFile(PGC_SIGHUP);
        }
        write_data_to_rel();
    }
}

void 
_PG_init()
{
    if(!process_shared_preload_libraries_in_progress)
        elog(FATAL, "Please use shared_preload_libraries");

}