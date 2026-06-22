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
#include "stdlib.h"

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

#define PER_QUERY_FIELDS 54

extern void worker_main(Datum main_arg);

typedef struct Per_Query_Tuple
{
    int64 client_ip; // 4
    int64 execution_id;  //5
    
    int64 execution_time; //6    
    int64 transaction_id; //7

    char *application_name; //14
    char *query; //10
    char *comments; //46
    int64 exec_time; // 21
    char *per_node_plan_info; //49
    char *lock_info; //50    
    char *rels_info; //51

    int64  cpu_user_time; //40
    int64  cpu_sys_time;  //41
    int64  wal_records; // 42
    int64  wal_fpi; //43
    double shared_blks_read; //24
    double shared_blks_written; //26
    double shared_blk_read_time;  //33
    double shared_blk_write_time; //34

} Per_Query_Tuple;



#endif