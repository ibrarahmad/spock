/* sql.h
 *      Declarations for SQL command execution functions.
 *
 * Copyright (c) 2022-2025, pgEdge, Inc.
 * Portions Copyright (c) 1996-2021, PostgreSQL Global Development Group
 * Portions Copyright (c) 1994, The Regents of the University of California
 *
 *-------------------------------------------------------------------------
 */
#ifndef SPOCKCTRL_SQL_H
#define SPOCKCTRL_SQL_H

int handle_sql_exec_command(int argc, char *argv[]);
void print_sql_help(void);

#endif /* SPOCKCTRL_SQL_H */
