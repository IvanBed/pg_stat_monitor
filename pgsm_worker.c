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
static Oid get_rel_oid(char const *, char const *);

static Oid 
get_rel_oid(char const *schema, char const *rel_name)
{        
    Oid schema_oid;
    Oid rel_oid;

    schema_oid = get_namespace_oid(schema, false);
    rel_oid    = get_relname_relid(rel_name, schema_oid);
    return rel_oid;
}

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
write_data_to_rel(pgsmPerQuerySharedStorage *shared_storage, dsa_area *dsa)
{	
    char	    *query_text;
    char	    *per_node_plan_info; 
    char	    *rels_info;    
    char        *locks_info;

    LWLockAcquire(shared_storage->lock, LW_SHARED);

    SetCurrentStatementStartTimestamp();
    StartTransactionCommand();
    SPI_connect();
    PushActiveSnapshot(GetTransactionSnapshot());

    for(size_t i = 0; i < shared_storage->size; i++)
    {
        StringInfoData buf;
        initStringInfo(&buf);

        query_text         = dsa_get_address(dsa, shared_storage->store[i].query_text.query_pos);
        per_node_plan_info = dsa_get_address(dsa, shared_storage->store[i].plan_info_text.plan_info_pos);
        rels_info          = dsa_get_address(dsa, shared_storage->store[i].rel_info_text.rel_info_pos);
        locks_info         = dsa_get_address(dsa, shared_storage->store[i].locks_info_text.locks_info_pos);
            // make a query to db
        appendStringInfo(&buf, "INSERT INTO %s (execution_id, client_ip, transaction_id, execution_time, application_name, query, comments, exec_time, per_node_plan_info, rels_info, lock_info, cpu_user_time, cpu_sys_time, wal_records, wal_fpi, shared_blks_read, shared_blks_written, shared_blk_read_time, shared_blk_write_time) VALUES (%ld, %d, %d, %ld, $$%s$$, $$%s$$, $$%s$$, %f, '%s', '%s', '%s', %f, %f, %ld, %ld, %ld, %ld, %f, %f)", 
                    REL_NAME, shared_storage->store[i].execution_id, shared_storage->store[i].client_ip,
                            shared_storage->store[i].transaction_id, shared_storage->store[i].execution_time,
                            shared_storage->store[i].counters.info.application_name,                            
                            query_text, shared_storage->store[i].counters.info.comments, shared_storage->store[i].counters.time.total_time, per_node_plan_info, rels_info, locks_info, 
                            shared_storage->store[i].counters.sysinfo.stime, shared_storage->store[i].counters.sysinfo.utime,
                            shared_storage->store[i].counters.walusage.wal_records, shared_storage->store[i].counters.walusage.wal_fpi,
                            shared_storage->store[i].counters.blocks.shared_blks_read, shared_storage->store[i].counters.blocks.shared_blks_written,
                            shared_storage->store[i].counters.blocks.shared_blk_read_time, shared_storage->store[i].counters.blocks.shared_blk_write_time
                            );
            
        SPI_execute(buf.data, false, 0);
        pfree(buf.data);
    }

    LWLockRelease(shared_storage->lock);
    PopActiveSnapshot();
    SPI_finish();
    CommitTransactionCommand();
}

static void 
write_data_to_rel_direct(Oid tbl_oid, pgsmPerQuerySharedStorage *shared_storage, dsa_area *dsa)
{	
    
    Relation   rel;
    HeapTuple  tup;
    Datum      values[1];
    bool       nulls[1];
    
    char	  *query_text;
    char	  *per_node_plan_info; 
    char	  *rels_info;    
    char      *locks_info;

    memset(nulls, false, sizeof(nulls));

    SetCurrentStatementStartTimestamp();
    StartTransactionCommand();
    PushActiveSnapshot(GetTransactionSnapshot());
    rel = try_table_open(tbl_oid, RowExclusiveLock);
    LWLockAcquire(shared_storage->lock, LW_SHARED);
    //for(size_t i = 0; i < shared_storage->size; i++)
    //{
    values[0] = CStringGetTextDatum("testTESTTEST");
    nulls[0] = 0;
    TupleDesc  desc  = rel->rd_att; 
    tup = heap_form_tuple(desc, values, nulls);
    CatalogTupleInsert(rel, tup);
    heap_freetuple(tup);
    //}
    if (rel)
        table_close(rel, RowExclusiveLock);
    LWLockRelease(shared_storage->lock);
    PopActiveSnapshot();
    CommitTransactionCommand();
    
    
}

PGDLLEXPORT void 
worker_main(Datum main_arg)
{
    // using args i can pass a db name
    char                      *db_name;
    Oid                        rel_oid;
    long                       timeout;
    pgsmPerQuerySharedStorage *shared_storage;
    dsa_area                  *dsa;
  

    // to windows should be DatumGetInt32
    timeout  = DatumGetInt64(main_arg);
    db_name  = "postgres";
    //rel_oid  = (Oid)get_rel_oid("public", "pg_stat_per_query"); 
    rel_oid = 57792;
    //timeout  = 10000;
    
    /*add error handling*/
    if (!get_shmem_latch())
    {
        elog(NOTICE, "Unable to find latch structure in shared memory.");
        return;
    }

    if (!get_shmem_storage())
    {
        elog(NOTICE, "Unable to find shared storage in shared memory.");
        return;
    }

    shared_storage = get_per_query_shared_storage();
    dsa            = get_dsa_area();

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
        //write_data_to_rel(shared_storage, dsa);
        write_data_to_rel_direct(rel_oid, shared_storage, dsa);
        pgsm_cleanup_storage(shared_storage, dsa);
    }
}

void 
_PG_init()
{
    if(!process_shared_preload_libraries_in_progress)
        elog(FATAL, "Please use shared_preload_libraries");

}