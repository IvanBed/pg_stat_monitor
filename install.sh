#!bin/bash

echo "Check PostgreSQL installation"
service postgresql status

if [ "$?" -gt "0" ]; then
    echo "PostgreSQL is not installed, please install PostgreSQL"
    exit
fi
echo "Ok! PostgreSQL is installed"

echo "Modify PostgreSQL configuration file"

echo "Add shared libraries"
sudo -u postgres psql -U postgres -d postgres -c "ALTER SYSTEM SET shared_preload_libraries = 'pg_stat_per_query_storage,pg_stat_monitor,pgsm_worker';"

if [ "$?" -gt "0" ]; then
    echo "Can not  add shared libraries"
    exit
fi

echo "Add additional lock for DSA"
sudo -u postgres psql -U postgres -d postgres -c "ALTER SYSTEM SET max_locks_per_transaction = 512;"

if [ "$?" -gt "0" ]; then
    echo "Can not add additional lock for DSA"
fi

echo "Include extension configuration file"
sudo -u postgres psql -U postgres -d postgres -c "ALTER SYSTEM SET include_if_exists = 'pg_stat_monitor.conf'"

if [ "$?" -gt "0" ]; then
    echo "Can not include extension configuration file"
fi

echo "The extension configuration file is included in the main configuration file; check the settings after the installation process is complete"

echo "Install pg_stat_monitor extension"
make all

if [ "$?" -gt "0" ]; then
    echo "Can not install pg_stat_monitor extension"
    exit
fi
echo "pg_stat_monitor extension has been installed successfully"

echo "Restart PostgreSQL server"
sudo systemctl start postgresql

if [ "$?" -gt "0" ]; then
    echo "Can not restart PostgreSQL server"
    exit
fi

echo "Create extension pg_stat_monitor"
sudo -u postgres psql -U postgres -d postgres -c "CREATE EXTENSION pg_stat_monitor;"

if [ "$?" -gt "0" ]; then
    echo "Can not create extension pg_stat_monitor"
    exit
fi