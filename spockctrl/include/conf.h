/* conf.h
 *      Definitions and functions for handling configuration data.
 *
 * Copyright (c) 2022-2025, pgEdge, Inc.
 * Portions Copyright (c) 1996-2021, PostgreSQL Global Development Group
 * Portions Copyright (c) 1994, The Regents of the University of California
 *
 *-------------------------------------------------------------------------
 */
#ifndef CONF_H
#define CONF_H

typedef struct {
    char *cluster_name;
    char *version;
} RaledConfig;

typedef struct {
    char *log_level;
    char *log_destination;
    char *log_file;
} LogConfig;

typedef struct {
    char *postgres_ip;
    int   postgres_port;
    char *postgres_user;
    char *postgres_password;
    char *postgres_db;
} PostgresConfig;

typedef struct {
    char *node_name;
    PostgresConfig postgres;
} SpockNodeConfig;

typedef struct {
    RaledConfig    raled;
    LogConfig      log;
    SpockNodeConfig *spock_nodes;
    int            spock_node_count;
} Config;

/* Function to parse the JSON configuration file */
int load_config(const char *filename);
void free_config(void);

/* Functions to retrieve PostgreSQL connection parameters for a given node name. */
const char *get_postgres_ip(const char *node_name);
int get_postgres_port(const char *node_name);
const char *get_postgres_user(const char *node_name);
const char *get_postgres_password(const char *node_name);
const char *get_postgres_db(const char *node_name);

/*
 * get_postgres_coninfo
 *      Constructs the PostgreSQL connection string for a given node.
 *
 * node_name: Name of the node to get connection info for.
 * buffer: Caller-provided buffer to store the connection string.
 * buffer_size: Size of the caller-provided buffer.
 *
 * Returns true on success (connection string written to buffer),
 * false on failure (e.g., node not found, buffer too small).
 *
 * The connection string is of the form:
 * "host=<ip> port=<port> user=<user> password=<password> dbname=<db>"
 */
bool get_postgres_coninfo(const char *node_name, char *buffer, size_t buffer_size);

#endif /* CONF_H */
