all: pg_stat_per_query_storage worker pg_stat_monitor

pg_stat_monitor:
	$(MAKE) -f Makefile.pg_stat_monitor USE_PGXS=1 install

pg_stat_per_query_storage:
	$(MAKE) -f Makefile.pg_stat_per_query_storage USE_PGXS=1 install

worker:
	$(MAKE) -f Makefile.worker USE_PGXS=1 install

clean: clean_worker clean_pg_stat_per_query_storage clean_pg_stat_monitor

clean_pg_stat_monitor:
	$(MAKE) -f Makefile.pg_stat_monitor USE_PGXS=1 clean

clean_pg_stat_per_query_storage:
	$(MAKE) -f Makefile.pg_stat_per_query_storage USE_PGXS=1 clean

clean_worker:
	$(MAKE) -f Makefile.worker USE_PGXS=1 clean