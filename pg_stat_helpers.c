#include "pg_stat_helpers.h"
//#define INDENT
/* Therer are helper functions that generate strings from statistics info*/

static void 
add_tabs(StringInfoData *res_buf, size_t tabs_cnt)
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
			
            /*
			 * scan nodes
			 */

        case T_SeqScanState:
            appendStringInfo(res_buf, "Seq Scan:\n");
            break;  

		case T_SampleScanState:
			appendStringInfo(res_buf, "Sample Scan:\n");
			break;

		case T_GatherState:
			appendStringInfo(res_buf, "Gather:\n");
			break;

		case T_GatherMergeState:
            appendStringInfo(res_buf, "Gather Merge:\n");
			break;

		case T_IndexScanState:
            appendStringInfo(res_buf, "Index Scan:\n");
			break;

		case T_IndexOnlyScanState:
			appendStringInfo(res_buf, "Index Only Scan:\n");
			break;

		case T_BitmapIndexScanState:
	        appendStringInfo(res_buf, "Bitmap Index Scan:\n");
			break;

		case T_BitmapHeapScanState:
	        appendStringInfo(res_buf, "Bitmap Heap Scan:\n");
			break;

		case T_TidScanState:
	        appendStringInfo(res_buf, "Tuple Identifier Scan:\n");
			break;

		case T_TidRangeScanState:
			appendStringInfo(res_buf, "Tuple Identifier Range Scan:\n");
			break;

		case T_SubqueryScanState:
			appendStringInfo(res_buf, "Subquery Scan:\n");
			break;

		case T_FunctionScanState:
			appendStringInfo(res_buf, "Function Scan:\n");
			break;

		case T_TableFuncScanState:
			appendStringInfo(res_buf, "Table Functions Scan:\n");
			break;

		case T_CteScanState:
			appendStringInfo(res_buf, "Common Table Expressions Scan:\n");
			break;

		case T_NamedTuplestoreScanState:
			appendStringInfo(res_buf, "Named Tuple Store Scan:\n");
			break;

		case T_WorkTableScanState:
			appendStringInfo(res_buf, "Work Table Scan:\n");
			break;

		case T_ForeignScanState:
			appendStringInfo(res_buf, "Foreign Scan:\n");
			break;

		case T_CustomScanState:
			appendStringInfo(res_buf, "Custom Scan:\n");
			break;

        //---------------------------------------------------------
		case T_ResultState:
			appendStringInfo(res_buf, "Result:\n");
			break;

		case T_ProjectSetState:
			appendStringInfo(res_buf, "Project Set:\n");
			break;

		case T_AppendState:
			appendStringInfo(res_buf, "Append:\n");
			break;

		case T_MergeAppendState:
			appendStringInfo(res_buf, "Merge Append:\n");
			break;

		case T_RecursiveUnionState:
			appendStringInfo(res_buf, "Recursive Union:\n");
			break;

		case T_BitmapAndState:
			appendStringInfo(res_buf, "Bitmap And State:\n");
			break;

		case T_BitmapOrState:
			appendStringInfo(res_buf, "Bitmap Or State:\n");
			break;

		case T_LimitState:
			appendStringInfo(res_buf, "Limit:\n");
			break;

		case T_IncrementalSortState:
			appendStringInfo(res_buf, "Incremental Sort:\n");
			break;

		case T_MaterialState:
			appendStringInfo(res_buf, "Material node:\n");
			break;

		case T_GroupState:
			appendStringInfo(res_buf, "Group node:\n");
			break;

		case T_SetOpState:
			appendStringInfo(res_buf, "SetOp node:\n");
			break;

		case T_LockRowsState:
			appendStringInfo(res_buf, "LockRows node:\n");
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
    initStringInfo(&buf);
    if (queryDesc)
        dfs_plan_state(queryDesc->planstate, &buf, 0);
    else 
        elog(NOTICE, "queryDesc is NULL");
    
    return buf.data;
}

/*RowLock on orders (transaction 12345) [mode: ExclusiveLock] [wait: 45ms] [relation: orders_pkey]*/
static void 
write_lock_info(LockInstanceData const *instance, LockTagType locktag_type, LOCKMODE mode, double lock_wait_activity, StringInfoData *res_buf)
{  
    int          pid;
    char const  *lockmode_name;

    char const  *db_name   = NULL;
    char const  *rel_name  = NULL;

    uint32_t     page_blocknum;
    uint16_t     page_offset;

    uint32_t     transaction_xid;
    
    if (!instance)
    {
        return;
    }

    if (!res_buf)
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
            elog(NOTICE, "LOCKTAG_RELATION_EXTEND and LOCKTAG_RELATION");
            elog(NOTICE, "db_name %s", db_name);
            elog(NOTICE, "rel_name %s", rel_name);
            if (db_name)
                pfree(db_name);
            /*if (rel_name)
                pfree(rel_name);*/
            break;
        case LOCKTAG_DATABASE_FROZEN_IDS:
            db_name  = get_database_name(instance->locktag.locktag_field1); 
            appendStringInfo(res_buf, "Database frozen lock, type %s\n\tdb: %s\n\tholder: %d, wait time activity: %ld s\n", lockmode_name, db_name, pid, (uint64_t)lock_wait_activity);
            elog(NOTICE, "LOCKTAG_DATABASE_FROZEN_IDS");
            elog(NOTICE, "db_name %s", db_name);
            if (db_name)
                pfree(db_name);
            break;
        case LOCKTAG_PAGE:
            db_name       = get_database_name(instance->locktag.locktag_field1);
            rel_name      = get_rel_name(instance->locktag.locktag_field2);
            page_blocknum = instance->locktag.locktag_field3;
            appendStringInfo(res_buf, "Page lock, type %s\n\tdb: %s, relation: %s, page block number: %d\n\tholder: %d, wait time activity: %ld s\n", lockmode_name, db_name, rel_name, page_blocknum, pid, (uint64_t)lock_wait_activity);
            elog(NOTICE, "LOCKTAG_PAGE");
            elog(NOTICE, "db_name %s", db_name);
            elog(NOTICE, "rel_name %s", rel_name);            
            if (db_name)
                pfree(db_name);
            /*if (rel_name)
                pfree(rel_name);*/
            break;
        case LOCKTAG_TUPLE:
            db_name       = get_database_name(instance->locktag.locktag_field1);
            rel_name      = get_rel_name(instance->locktag.locktag_field2);
            page_blocknum = instance->locktag.locktag_field3;
            page_offset   = instance->locktag.locktag_field4;
            appendStringInfo(res_buf, "Page lock, type %s\n\tdb: %s, relation: %sn\t page block number: %d, offset within page %d \n\tholder: %d, wait time activity: %ld s\n", lockmode_name, db_name, rel_name, page_blocknum, page_offset, pid, (uint64_t)lock_wait_activity);
            //elog(NOTICE, "LOCKTAG_TUPLE");
            //elog(NOTICE, "db_name %s", db_name);
            //elog(NOTICE, "rel_name %s", rel_name);             
            if (db_name)
                pfree(db_name);
            /*if (rel_name)
                pfree(rel_name);*/
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

    if (!locks_data)
    {
        return;
    }

    if (!buf)
    {
        return;
    }


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

PGDLLEXPORT char const * 
generate_locks_info(LockData const *locks_data)
{
    StringInfoData buf;
    initStringInfo(&buf);

    if (locks_data)
        write_locks_info(locks_data, &buf);
    else 
        elog(NOTICE, "locks_data is NULL");
    
    return buf.data;
}

static void 
write_rel_info(Relation rel, StringInfoData *buf)
{
    if (!rel)
    {
        return;
    }      
    
    elog(NOTICE, "write_rel_info ");
    elog(NOTICE, "rel id %d", rel->rd_id);
    char const *rel_name  = NULL;
    
    rel_name = get_rel_name(rel->rd_id);
    appendStringInfo(buf, "Relation: %s\n", rel_name); 
    if (rel->pgstat_info)
    { 
        appendStringInfo(buf, "    Tuples moved to a new page: %ld\n", rel->pgstat_info->counts.tuples_newpage_updated);
        appendStringInfo(buf, "    Tuples updated: %ld\n", rel->pgstat_info->counts.tuples_updated);
        appendStringInfo(buf, "    Tuples hot updated: %ld\n", rel->pgstat_info->counts.tuples_hot_updated);
    }
    else 
    {
        appendStringInfo(buf, "Relation info structure is NULL\n");
    }

    //if (rel_name)
    //    free(rel_name);
}

static void 
write_rels_info(QueryDesc *queryDesc, StringInfoData *buf)
{
    EState   *query_state;
    Relation *rels_arr;
    if (!queryDesc)
    {
        return;
    }   
    
    query_state = queryDesc->estate;
    if (!query_state)
    {
        return;
    }

    rels_arr = query_state->es_relations;
    if (!rels_arr)
    {
        return;
    }

    elog(NOTICE, "write_rels_info");

    for (size_t rel_idx = 0; rel_idx < query_state->es_range_table_size; rel_idx++)
    {
        Relation cur_rel = rels_arr[rel_idx];
        if (cur_rel)
            write_rel_info(cur_rel, buf);
    }
}

PGDLLEXPORT char const * 
generate_rels_info(QueryDesc *queryDesc)
{
    StringInfoData buf;
    initStringInfo(&buf);
    elog(NOTICE, "generate_rels_info");
    if (queryDesc)
        write_rels_info(queryDesc, &buf);
    else
        elog(NOTICE, "queryDesc is NULL");
    
    return buf.data;
}

