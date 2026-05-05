#include "pg_stat_helpers.h"
//#define INDENT
/* Therer are helper functions that generate strings from statistics info*/

void add_tabs(StringInfoData *res_buf, size_t tabs_cnt)
{
    for (size_t i = 0; i < tabs_cnt; i++)
        appendStringInfo(res_buf, "    ");
}

static void 
write_node_name(NodeTag tag, StringInfoData *res_buf, size_t level)
{
    add_tabs(res_buf, level);
    switch(tag)
    {
        case T_MergeJoinState:
            appendStringInfo(res_buf, "MergeJoin:\n");
            break;
        case T_SortState:
            appendStringInfo(res_buf, "Sort:\n");
            break;        
        case T_SeqScanState:
            appendStringInfo(res_buf, "Seq Scan:\n");
            break;  
        case T_NestLoopState:
            appendStringInfo(res_buf, "Nested Loop:\n");
            break;
        case T_HashJoinState:
            appendStringInfo(res_buf, "Hash Join:\n");
            break;                
        case T_AggState:
            appendStringInfo(res_buf, "Aggregate:\n");        
            break;    
        case T_HashState:
            appendStringInfo(res_buf, "Hash:\n");  
            break;
        case T_ValuesScanState:
            appendStringInfo(res_buf, "Values Scan:\n");  
            break;
        case T_ModifyTableState:
            appendStringInfo(res_buf, "Modify Table:\n"); 
            break;   
        default:
            appendStringInfo(res_buf, "Unknown:\n"); 
            break; 
    }
}

static void 
write_per_node_instr_info(PlanState const *node, StringInfoData *res_buf, size_t level)
{
    if (node->instrument)
    { 
        add_tabs(res_buf, level);
        appendStringInfo(res_buf, "    Actual time: %f ms %ld rows %ld loops\n", INSTR_TIME_GET_MILLISEC(node->instrument->counter), (uint64_t)node->instrument->tuplecount, (uint64_t)node->instrument->nloops);
        
        /*If there were some workers we have to collect some statistics about their work*/
        
        if (node->worker_instrument)
        {
            appendStringInfo(res_buf, "Workers count: %d ", node->worker_instrument->num_workers);
            for (size_t i = 0; i < node->worker_instrument->num_workers; i++)
            {
                appendStringInfo(res_buf, "        Worker %ld actual time: %f ms %f rows %f loop \n", i + 1, INSTR_TIME_GET_MILLISEC(node->worker_instrument->instrument[i].counter),  node->worker_instrument->instrument[i].tuplecount, node->worker_instrument->instrument[i].nloops);
            }
        }
    }
    else 
    {
        appendStringInfo(res_buf, "    NO DATA\nActual time: %d ms %d rows %d loop  \n", 0,  0, 0);
    }
    //add info about buffers and wal usage if pg version < 18
}

static void 
dfs_plan_state(PlanState const *node, StringInfoData *res_buf, size_t level)
{
    if (!node)
        return;
    
    write_node_name(node->type, res_buf, level);
    write_per_node_instr_info(node, res_buf, level);
    level++;
    dfs_plan_state(node->lefttree, res_buf, level);
    dfs_plan_state(node->righttree, res_buf, level);
}

PGDLLEXPORT char const *
generate_plan_info(QueryDesc const *queryDesc)
{
    StringInfoData buf;

    if (!queryDesc)
    {
        elog(NOTICE, "queryDesc is NULL");
        return NULL;
    }
    initStringInfo(&buf);
    dfs_plan_state(queryDesc->planstate, &buf, 0);
    return buf.data;
}
/*RowLock on orders (transaction 12345) [mode: ExclusiveLock] [wait: 45ms] [relation: orders_pkey]*/
static void 
write_lock_info(LockInstanceData const *instance, LockTagType locktag_type, LOCKMODE mode, double lock_wait_activity, StringInfoData *res_buf)
{  
    int          pid;
    char const  *lockmode_name;

    char const  *db_name;
    char const  *rel_name;

    uint32_t     page_blocknum;
    uint16_t     page_offset;

    uint32_t     transaction_xid;
    
    if (!instance)
    {
        return;
    }

    pid           = instance->pid;
    // check whether it have to be free or not!!!
    lockmode_name = GetLockmodeName(instance->locktag.locktag_lockmethodid, mode);
   
    switch (locktag_type)
    {
        case LOCKTAG_RELATION_EXTEND:
        case LOCKTAG_RELATION:
            db_name  = get_database_name(instance->locktag.locktag_field1);
            rel_name = get_rel_name(instance->locktag.locktag_field2);
            appendStringInfo(res_buf, "Realtion lock, type %s\n\tdb: %s, relation: %s\n\tholder: %d, wait time activity: %ld s\n", lockmode_name, db_name, rel_name, pid, (uint64_t)lock_wait_activity);

            // The same point as 107
            //pfree(rel_name);
            break;
        case LOCKTAG_DATABASE_FROZEN_IDS:
            db_name  = get_database_name(instance->locktag.locktag_field1); 
            appendStringInfo(res_buf, "Database frozen lock, type %s\n\tdb: %s\n\tholder: %d, wait time activity: %ld s\n", lockmode_name, db_name, pid, (uint64_t)lock_wait_activity);
            break;
        case LOCKTAG_PAGE:
            db_name       = get_database_name(instance->locktag.locktag_field1);
            rel_name      = get_rel_name(instance->locktag.locktag_field2);
            page_blocknum = instance->locktag.locktag_field3;
            appendStringInfo(res_buf, "Page lock, type %s\n\tdb: %s, relation: %s, page block number: %d\n\tholder: %d, wait time activity: %ld s\n", lockmode_name, db_name, rel_name, page_blocknum, pid, (uint64_t)lock_wait_activity);
            break;
        case LOCKTAG_TUPLE:
            db_name       = get_database_name(instance->locktag.locktag_field1);
            rel_name      = get_rel_name(instance->locktag.locktag_field2);
            page_blocknum = instance->locktag.locktag_field3;
            page_offset   = instance->locktag.locktag_field4;
            appendStringInfo(res_buf, "Page lock, type %s\n\tdb: %s, relation: %sn\t page block number: %d, offset within page %d \n\tholder: %d, wait time activity: %ld s\n", lockmode_name, db_name, rel_name, page_blocknum, page_offset, pid, (uint64_t)lock_wait_activity);
            break;
        case LOCKTAG_TRANSACTION:
            transaction_xid  = instance->locktag.locktag_field1; 
            appendStringInfo(res_buf, "Transaction lock, type %s\n\txid: %d\n\tholder: %d, wait time activity: %ld s\n", lockmode_name, transaction_xid, pid, (uint64_t)lock_wait_activity);
            break;
        case LOCKTAG_VIRTUALTRANSACTION:



            break;
        case LOCKTAG_SPECULATIVE_TOKEN:



            break;
        case LOCKTAG_APPLY_TRANSACTION:



            break;
        case LOCKTAG_OBJECT:



            break;
        case LOCKTAG_USERLOCK:



            break;        
        case LOCKTAG_ADVISORY:



            break;        
        default:            /* treat unknown locktags like OBJECT */



            break;
    }

}

static void 
write_locks_info(LockData const *locks_data, StringInfoData *buf)
{
    LockInstanceData *instance;
    bool              granted;
    LOCKMODE          mode;
    TimestampTz       end_timestamp; 
    double            lock_wait_activity;

    granted = false;
    mode    = 0;
    end_timestamp = GetCurrentTimestamp();
    
    for (size_t i = 0; i < locks_data->nelements; i++)
    {
        instance  = &(locks_data->locks[i]);
        if (instance->holdMask)
        {
            for (mode = 0; mode < MAX_LOCKMODES; mode++)
            {
                if (instance->holdMask & LOCKBIT_ON(mode))
                {
                    granted = true;
                    instance->holdMask &= LOCKBIT_OFF(mode);
                    break;
                }
            }
        }
        
        if (!granted)
        {
            if (instance->waitLockMode != NoLock)
            {
                mode = instance->waitLockMode;
            }
            else
            {
                continue;
            }
        }

        if (instance->waitStart != 0)
            lock_wait_activity = TimestampDifferenceMilliseconds(instance->waitStart, end_timestamp) / 1000.0;
        else 
            continue;

        write_lock_info(instance, (LockTagType) instance->locktag.locktag_type, mode, lock_wait_activity, buf);        
    }
}
//to think about returnig the StringInfoData not the cstring
PGDLLEXPORT char const * 
generate_locks_info(LockData const *locks_data)
{
    StringInfoData buf;    
    if (!locks_data)
    {
        elog(NOTICE, "locks_data is NULL");
        return NULL;
    }

    initStringInfo(&buf);
    write_locks_info(locks_data, &buf);
    return buf.data;
}