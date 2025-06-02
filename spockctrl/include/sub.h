/* sub.h
 *      Declarations for subscription management command handlers.
 *
 * Copyright (c) 2022-2025, pgEdge, Inc.
 * Portions Copyright (c) 1996-2021, PostgreSQL Global Development Group
 * Portions Copyright (c) 1994, The Regents of the University of California
 *
 *-------------------------------------------------------------------------
 */
#ifndef SUB_H
#define SUB_H

void print_sub_help(void);
int handle_sub_command(int argc, char *argv[]);


/* Function declarations */
int handle_sub_create_command(int argc, char *argv[]);
int handle_sub_drop_command(int argc, char *argv[]);
int handle_sub_enable_command(int argc, char *argv[]);
int handle_sub_disable_command(int argc, char *argv[]);
int handle_sub_show_status_command(int argc, char *argv[]);
int handle_sub_show_table_command(int argc, char *argv[]);
int handle_sub_resync_table_command(int argc, char *argv[]);
int handle_sub_add_repset_command(int argc, char *argv[]);
int handle_sub_remove_repset_command(int argc, char *argv[]);

#endif /* SUB_H */
