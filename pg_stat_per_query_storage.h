#ifndef __PG_STAT_PER_QUERY_STORAGE_H__
#define __PG_STAT_PER_QUERY_STORAGE_H__

#include "postgres.h"
#include "fmgr.h"
#include "miscadmin.h"

#include "storage/lwlock.h"

#include "utils/builtins.h"
#include "utils/memutils.h"
#include "utils/dsa.h"

#include "nodes/pg_list.h"
#include "nodes/memnodes.h"

#include "datatype/timestamp.h"
#include "storage/spin.h"

#include "executor/instrument.h"

// it will be guc vars too
#define REL_NAME "pg_stat_per_query"
#define STORE_CAPACITY 1
#define DSA_STORE_MAX_SIZE 1024 * 1024

#define FREE 0 
#define ALLOCATED 1
#define STORAGE_FULL -1

#define SPI_OK_INSERT 7

// put there the define part from pg_stat_monitor.h to avoid structs duplicating
#define MAX_RESPONSE_BUCKET 50
#define SQLCODE_LEN         20
#define ERROR_MESSAGE_LEN	100
#define APPLICATIONNAME_LEN	NAMEDATALEN
#define COMMENTS_LEN        256
#define REL_LST				10
#define REL_LEN				132 /* REL_TYPENAME_LEN * 2 (relname + schema) + 1
								 * (for view indication) + 1 and dot and
								 * string terminator */
#define PLAN_TEXT_LEN		1024

/* shared memory storage for the query */

typedef struct CallTime
{
	double		total_time;		/* total execution time, in msec */
	double		min_time;		/* minimum execution time in msec */
	double		max_time;		/* maximum execution time in msec */
	double		mean_time;		/* mean execution time in msec */
	double		sum_var_time;	/* sum of variances in execution time in msec */
} CallTime;

typedef struct PlanInfo
{
	int64		planid;			/* plan identifier */
	char		plan_text[PLAN_TEXT_LEN];	/* plan text */
	size_t		plan_len;		/* strlen(plan_text) */
} PlanInfo;

typedef struct QueryInfo
{
	dsa_pointer parent_query;
	int64		type;			/* type of query, options are query, info,
								 * warning, error, fatal */
	char		application_name[APPLICATIONNAME_LEN];
	char		comments[COMMENTS_LEN];
	char		relations[REL_LST][REL_LEN];	/* List of relation involved
												 * in the query */
	int			num_relations;	/* Number of relation in the query */
	CmdType		cmd_type;		/* query command type
								 * SELECT/UPDATE/DELETE/INSERT */
} QueryInfo;

typedef struct ErrorInfo
{
	int64		elevel;			/* error elevel */
	char		sqlcode[SQLCODE_LEN];	/* error sqlcode  */
	char		message[ERROR_MESSAGE_LEN]; /* error message text */
} ErrorInfo;

typedef struct Calls
{
	int64		calls;			/* # of times executed */
	int64		rows;			/* total # of retrieved or affected rows */
	double		usage;			/* usage factor */
} Calls;

typedef struct Blocks
{
	int64		shared_blks_hit;	/* # of shared buffer hits */
	int64		shared_blks_read;	/* # of shared disk blocks read */
	int64		shared_blks_dirtied;	/* # of shared disk blocks dirtied */
	int64		shared_blks_written;	/* # of shared disk blocks written */
	int64		local_blks_hit; /* # of local buffer hits */
	int64		local_blks_read;	/* # of local disk blocks read */
	int64		local_blks_dirtied; /* # of local disk blocks dirtied */
	int64		local_blks_written; /* # of local disk blocks written */
	int64		temp_blks_read; /* # of temp blocks read */
	int64		temp_blks_written;	/* # of temp blocks written */
	double		shared_blk_read_time;	/* time spent reading shared blocks,
										 * in msec */
	double		shared_blk_write_time;	/* time spent writing shared blocks,
										 * in msec */
	double		local_blk_read_time;	/* time spent reading local blocks, in
										 * msec */
	double		local_blk_write_time;	/* time spent writing local blocks, in
										 * msec */
	double		temp_blk_read_time; /* time spent reading temp blocks, in msec */
	double		temp_blk_write_time;	/* time spent writing temp blocks, in
										 * msec */

	/*
	 * Variables for local entry. The values to be passed to pgsm_update_entry
	 * from pgsm_store.
	 */
	instr_time	instr_shared_blk_read_time; /* time spent reading shared
											 * blocks */
	instr_time	instr_shared_blk_write_time;	/* time spent writing shared
												 * blocks */
	instr_time	instr_local_blk_read_time;	/* time spent reading local blocks */
	instr_time	instr_local_blk_write_time; /* time spent writing local blocks */
	instr_time	instr_temp_blk_read_time;	/* time spent reading temp blocks */
	instr_time	instr_temp_blk_write_time;	/* time spent writing temp blocks */
} Blocks;

typedef struct JitInfo
{
	int64		jit_functions;	/* total number of JIT functions emitted */
	double		jit_generation_time;	/* total time to generate jit code */
	int64		jit_inlining_count; /* number of times inlining time has been
									 * > 0 */
	double		jit_deform_time;	/* total time to deform tuples in jit code */
	int64		jit_deform_count;	/* number of times deform time has been >
									 * 0 */
	double		jit_inlining_time;	/* total time to inline jit code */
	int64		jit_optimization_count; /* number of times optimization time
										 * has been > 0 */
	double		jit_optimization_time;	/* total time to optimize jit code */
	int64		jit_emission_count; /* number of times emission time has been
									 * > 0 */
	double		jit_emission_time;	/* total time to emit jit code */

	/*
	 * Variables for local entry. The values to be passed to pgsm_update_entry
	 * from pgsm_store.
	 */
	instr_time	instr_generation_counter;	/* generation counter */
	instr_time	instr_inlining_counter; /* inlining counter */
	instr_time	instr_deform_counter;	/* deform counter */
	instr_time	instr_optimization_counter; /* optimization counter */
	instr_time	instr_emission_counter; /* emission counter */
} JitInfo;

typedef struct SysInfo
{
	double		utime;			/* user cpu time */
	double		stime;			/* system cpu time */
} SysInfo;

typedef struct Wal_Usage
{
	int64		wal_records;	/* # of WAL records generated */
	int64		wal_fpi;		/* # of WAL full page images generated */
	uint64		wal_bytes;		/* total amount of WAL bytes generated */
	int64		wal_buffers_full;	/* # of times the WAL buffers became full */
} Wal_Usage;

typedef struct Lock_Usage
{

} Lock_Usage;

typedef struct Rel_info
{

} Rel_info;

typedef struct Counters
{
	Calls		calls;
	QueryInfo	info;
	CallTime	time;

	Calls		plancalls;
	CallTime	plantime;
	PlanInfo	planinfo;

	Blocks		blocks;
	SysInfo		sysinfo;
	JitInfo		jitinfo;
	ErrorInfo	error;
	Wal_Usage	walusage;
	int			resp_calls[MAX_RESPONSE_BUCKET];	/* execution time's in
													 * msec */
	int64		parallel_workers_to_launch; /* # of parallel workers planned
											 * to be launched */
	int64		parallel_workers_launched;	/* # of parallel workers actually
											 * launched */
} Counters;

// to think about whether i can use a common pgsmEntry from pg_stat_monitor header
typedef struct pgsmPerQueryEntry
{
	uint64_t    execution_id;	   
	//uint32_t    transaction_id;
	char		datname[NAMEDATALEN];	/* database name */
	char		username[NAMEDATALEN];	/* user name */
	Counters	counters;		/* the statistics for this query */
	int			encoding;		/* query text encoding */
	TimestampTz stats_since;	/* timestamp of entry allocation */
	TimestampTz minmax_stats_since; /* timestamp of last min/max values reset */
	slock_t		mutex;			/* protects the counters only */

    // add some information about plan, locks and so on

	union
	{
		dsa_pointer locks_info_pos;	/* lock info location within dsa buffer */
		char	   *locks_info_pointer;
	}  locks_info_text;

	union
	{
		dsa_pointer plan_info_pos;	/* plan info text location within dsabuffer */
		char	   *plan_info_pointer;
	}  plan_info_text;
	
	union
	{
		dsa_pointer query_pos;	/* query location within query buffer */
		char	   *query_pointer;
	}  query_text;

} pgsmPerQueryEntry;

/*
 * Global shared store for per query statistics
 */

typedef struct StorageRelOidInfo
{
    Oid rel_oid;
	bool is_init;
} StorageRelOidInfo;

typedef struct pgsmPerQuerySharedStorage
{
	LWLock	          *lock;			/* protects list search/modification */
	//slock_t		       mutex;			/* protects following fields only: */
	void	          *raw_dsa_area;	/* DSA area pointer to store query texts for interproccess communication */
	pgsmPerQueryEntry *store;

	size_t             store_capacity;
    uint8_t           *free_space_bitmap;
	bool		       pgsm_oom;
} pgsmPerQuerySharedStorage;

typedef struct pgsmPerQueryLocalStorage
{
	pgsmPerQuerySharedStorage *shared_storage;
	dsa_area                  *dsa;		/* local dsa area for backend attached to the
								         * dsa area created by postmaster at startup. */
	MemoryContext             pgsm_mem_cxt;

} pgsmPerQueryLocalStorage;

//args: query info in enrty struct and local storage, that contens shared part and locally initialized dsa

extern bool pgsm_add_per_query_entry(pgsmPerQuerySharedStorage *shared_storage, dsa_area *dsa, pgsmPerQueryEntry *entry);
extern void pgsm_cleanup_storage(pgsmPerQuerySharedStorage *, dsa_area *dsa, int const *);

#endif