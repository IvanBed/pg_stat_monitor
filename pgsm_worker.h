#ifndef __PGSM_WORKER_H__
#define __PGSM_WORKER_H__

#include "pg_stat_per_query_storage.h"

#include "storage/ipc.h"
#include "storage/shmem.h"
#include "storage/proc.h"
#include "storage/latch.h"   

#include "postmaster/bgworker.h"
#include "postmaster/interrupt.h"
#include "tcop/tcopprot.h"
 
#include "executor/spi.h"

#include "utils/wait_event.h"
#include <utils/rel.h>
#include <utils/snapmgr.h>
#include "libpq/pqsignal.h"


#include <access/amapi.h>
#include <access/heapam.h>
#include <access/htup_details.h>
#include <access/table.h>
#include <access/tableam.h>
#include <catalog/indexing.h>
#include <utils/builtins.h>
#include <utils/fmgroids.h>
#include "catalog/namespace.h"
#include "utils/lsyscache.h"

extern void worker_main(Datum main_arg);

/*    shared_blk_read_time       float8,
    shared_blk_write_time      float8,
    local_blk_read_time        float8,
    local_blk_write_time       float8,

    temp_blk_read_time         float8,
    temp_blk_write_time        float8, */

/*execution_id, client_ip, transaction_id, execution_time, application_name, query, comments, exec_time, 
per_node_plan_info, rels_info, lock_info, cpu_user_time, cpu_sys_time, wal_records, wal_fpi, shared_blks_read, 
shared_blks_written, shared_blk_read_time, shared_blk_write_time
*/
typedef struct Per_Query_Tuple
{
    int64 execution_id;
    int64 client_ip;
    int64 transaction_id;
    int64 execution_time;
    char *application_name;
    char *query;
    char *comments
    int64 exec_time;
    char *per_node_plan_info;
    char *rels_info
    char *lock_info;
    
    int64 cpu_user_time;
    int64 cpu_sys_time;
    int64 wal_records;
    int64 wal_fpi;

} Per_Query_Tuple;



#endif