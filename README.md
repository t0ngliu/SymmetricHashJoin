# Symmetric Hash Join
A hashing algorithm to replace the current hybrid hash join in PostgreSQL

This is the final project for the course **Database II**, which is a implementation of a symmetric hash join. This is to replace the current hybrid hash join in [PostgreSQL 8.1.4](http://www.postgresql.org/ftp/source/v8.1.4/)

Team members:

* [t0ngliu](https://github.com/t0ngliu "t0ngliu profile")
* [mutefish](https://github.com/mutefish "mutefish profile")

### How to build
	
* Replace the following files in PostgreSQL 8.1.4 with the files in this project
* postgresql-8.1.4/src/backend/optimizer/plan/createplan.c
	* postgresql-8.1.4/src/include/nodes/execnodes.h
	* postgresql-8.1.4/src/backend/executor/nodeHashjoin.c
	* postgresql-8.1.4/src/backend/executor/nodeHash.c
* `cd` into the PostgreSQL 8.1.4 folder
* Run `./configure --prefix=$HOME/pgbuild --enable-debug --enable-cassert`
* Run `make`
* Run `make install`
* Run `cd $HOME/pgbuild/bin`
* Run `./initdb -D ../data`
* In the $HOME/pgbuild/data disable the other join methods other than hashjoin, ie.
	* enable_mergejoin = off
	* enable_nestloop = off
* Run `./postmaster -D ../data`
* Open a NEW terminal window and run `cd $HOME/pgbuild/bin`
* Run `./psql postgres`

Now load a schema and test out a join query! :)
