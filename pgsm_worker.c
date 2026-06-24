#include "pgsm_worker.h"

PG_MODULE_MAGIC;

static pgsmPerQueryLocalStorage  pgsm_per_query_local_storage;
static Latch                    *latch         = NULL;
static WorkerArgs               *worker_args   = NULL;

static bool get_shmem_latch(void);
static bool get_shmem_storage(void);
static void attach_shmem(void);
static dsa_area *get_dsa_area(void);
static pgsmPerQuerySharedStorage * get_per_query_shared_storage(void);
static Size pgsm_per_query_area_size(void);
static Size pgsm_get_per_query_shared_size(void);
static Oid get_rel_oid(char const *);

static Oid
get_rel_oid(char const *rel_name)
{
    Oid rel_oid;

    SetCurrentStatementStartTimestamp();
    StartTransactionCommand();
    SPI_connect();
    PushActiveSnapshot(GetTransactionSnapshot());

    rel_oid = DatumGetObjectId(DirectFunctionCall1(to_regclass, CStringGetTextDatum(rel_name)));

    PopActiveSnapshot();
    SPI_finish();
    CommitTransactionCommand();

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

static bool 
get_shmem_args(void)
{
    bool found;
    LWLockAcquire(AddinShmemInitLock, LW_EXCLUSIVE);

    worker_args = ShmemInitStruct("WorkerArgs", sizeof(WorkerArgs), &found);

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
write_data_to_rel_direct(Oid tbl_oid, pgsmPerQuerySharedStorage *shared_storage, dsa_area *dsa)
{    
    Relation   rel;
    HeapTuple  tup;
    Datum      values[PER_QUERY_FIELDS];
    bool       nulls[PER_QUERY_FIELDS];
    TupleDesc  desc;
    char      *query_text;
    char      *per_node_plan_info; 
    char      *rels_info;    
    char      *locks_info;

    memset(nulls, true, sizeof(nulls));

    SetCurrentStatementStartTimestamp();
    StartTransactionCommand();
    PushActiveSnapshot(GetTransactionSnapshot());

    rel = try_table_open(tbl_oid, RowExclusiveLock);
    if (rel)
    {
        LWLockAcquire(shared_storage->lock, LW_SHARED);
        for(size_t i = 0; i < shared_storage->size; i++)
        {
            memset(nulls, true, sizeof(nulls));
            memset(values, 0, sizeof(values));

            desc               = rel->rd_att;
            query_text         = dsa_get_address(dsa, shared_storage->store[i].query_text.query_pos);
            per_node_plan_info = dsa_get_address(dsa, shared_storage->store[i].plan_info_text.plan_info_pos);
            rels_info          = dsa_get_address(dsa, shared_storage->store[i].rel_info_text.rel_info_pos);
            locks_info         = dsa_get_address(dsa, shared_storage->store[i].locks_info_text.locks_info_pos);
        
            values[4] = Int64GetDatum(shared_storage->store[i].client_ip);
            values[5] = Int64GetDatum(shared_storage->store[i].execution_id);
            values[6] = Int64GetDatum(shared_storage->store[i].transaction_id);
            values[7] = Int64GetDatum(shared_storage->store[i].transaction_id);
        
            nulls[4]  = false;
            nulls[5]  = false;
            nulls[6]  = false;        
            nulls[7]  = false;

            values[10] = CStringGetTextDatum(query_text);
            values[14] = CStringGetTextDatum(shared_storage->store[i].counters.info.application_name);
            
            nulls[10]  = false;
            nulls[14]  = false;

            values[21] = Int64GetDatum(shared_storage->store[i].counters.time.total_time);
            values[24] = Float8GetDatum(shared_storage->store[i].counters.blocks.shared_blks_read);
            values[26] = Float8GetDatum(shared_storage->store[i].counters.blocks.shared_blks_written);
            values[33] = Float8GetDatum(shared_storage->store[i].counters.blocks.shared_blk_read_time);
            values[34] = Float8GetDatum(shared_storage->store[i].counters.blocks.shared_blk_write_time);

            nulls[21]  = false;
            nulls[24]  = false;
            nulls[26]  = false;        
            nulls[33]  = false;
            nulls[34]  = false;

            values[40] = Int64GetDatum(shared_storage->store[i].counters.sysinfo.utime);
            values[41] = Int64GetDatum(shared_storage->store[i].counters.sysinfo.stime);
            values[42] = Int64GetDatum(shared_storage->store[i].counters.walusage.wal_records);
            values[43] = Int64GetDatum(shared_storage->store[i].counters.walusage.wal_fpi);    

            nulls[40]  = false;
            nulls[41]  = false;
            nulls[42]  = false;        
            nulls[43]  = false; 

            values[46] = CStringGetTextDatum(shared_storage->store[i].counters.info.comments);
            values[49] = CStringGetTextDatum(per_node_plan_info);
            values[50] = CStringGetTextDatum(locks_info);
            values[51] = CStringGetTextDatum(rels_info);     

            nulls[46]  = false;
            nulls[49]  = false;
            nulls[50]  = false;        
            nulls[51]  = false; 

            tup = heap_form_tuple(desc, values, nulls);
            CatalogTupleInsert(rel, tup);
            heap_freetuple(tup);
        }
    }
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
    char                      *schema_name;
    char                      *rel_name;
    Oid                        rel_oid;
    long                       timeout;
    pgsmPerQuerySharedStorage *shared_storage;
    dsa_area                  *dsa;

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

    if (!get_shmem_args())
    {
        elog(NOTICE, "Unable to find worker arguments in shared memory.");
        //return;
    }

    // to windows should be DatumGetInt32
    timeout     = DatumGetInt64(main_arg);
    rel_name    = "public.pg_stat_per_query";
    db_name     = "postgres";

    shared_storage = get_per_query_shared_storage();
    dsa            = get_dsa_area();

    pqsignal(SIGHUP, SignalHandlerForConfigReload);
    pqsignal(SIGTERM, die);
    BackgroundWorkerUnblockSignals();

    BackgroundWorkerInitializeConnection(db_name, NULL, 0);

    rel_oid  = get_rel_oid(rel_name); 
    
    if (rel_oid == 0)
    {
        elog(NOTICE, "Can not get relation OID, stop worker process");
        return;
    }

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