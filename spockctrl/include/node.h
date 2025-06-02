/* node.h
 *      Declarations for node management command handlers.
 *
 * Copyright (c) 2022-2025, pgEdge, Inc.
 * Portions Copyright (c) 1996-2021, PostgreSQL Global Development Group
 * Portions Copyright (c) 1994, The Regents of the University of California
 *
 *-------------------------------------------------------------------------
 */
#ifndef NODE_H
#define NODE_H

void print_node_help(void);
int handle_node_command(int argc, char *argv[]);

int handle_node_create_command(int argc, char *argv[]);
int handle_node_drop_command(int argc, char *argv[]);
int handle_node_add_interface_command(int argc, char *argv[]);
int handle_node_drop_interface_command(int argc, char *argv[]);


#endif /* NODE_H */
