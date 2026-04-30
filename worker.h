#ifndef __WORKER_H__
#define __WORKER_H__

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

extern void worker_main(Datum main_arg);

#endif