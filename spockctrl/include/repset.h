/* repset.h
 *      Declarations for replication set command handlers.
 *
 * Copyright (c) 2022-2025, pgEdge, Inc.
 * Portions Copyright (c) 1996-2021, PostgreSQL Global Development Group
 * Portions Copyright (c) 1994, The Regents of the University of California
 *
 *-------------------------------------------------------------------------
 */
#ifndef REPSET_H
#define REPSET_H

void print_repset_help(void);
int handle_repset_command(int argc, char *argv[]);

int handle_repset_create_command(int argc, char *argv[]);
int handle_repset_alter_command(int argc, char *argv[]);
int handle_repset_drop_command(int argc, char *argv[]);
int handle_repset_add_table_command(int argc, char *argv[]);
int handle_repset_remove_table_command(int argc, char *argv[]);
int handle_repset_add_partition_command(int argc, char *argv[]);
int handle_repset_remove_partition_command(int argc, char *argv[]);
int handle_repset_list_tables_command(int argc, char *argv[]);


#endif /* REPSET_H */
