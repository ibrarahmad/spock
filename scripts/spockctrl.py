#!/usr/bin/env python3

import subprocess
import sys
from datetime import datetime
import argparse
import os
import json

# Set PostgreSQL bin path
PG_PATH = "/usr/local/pgsql.17/bin"

# Update PATH environment variable to include PostgreSQL bin path
os.environ["PATH"] = f"{PG_PATH}:{os.environ.get('PATH', '')}"

# Define variables for the workflow
NODE1_NAME = "n1"
NODE2_NAME = "n2"
NODE3_NAME = "n3"

NODE1_DSN = "host=127.0.0.1 dbname=pgedge port=5431 user=pgedge password=pgedge"
NODE2_DSN = "host=127.0.0.1 dbname=pgedge port=5432 user=pgedge password=pgedge"
NODE3_DSN = "host=127.0.0.1 dbname=pgedge port=5433 user=pgedge password=pgedge"

NODE_LOCATION = "Los Angeles"
NODE_COUNTRY = "USA"

REPLICATION_SLOT = f"spk_pgedge_{NODE2_NAME}_sub_{NODE2_NAME}_{NODE3_NAME}"
SYNC_EVENT_TIMEOUT = 1200000
APPLY_WORKER_TIMEOUT = 1000
COMMIT_TIMESTAMP = f"${NODE3_NAME}.commit_timestamp"

# Helper function to log steps
def log_step(step_number, description, status):
    timestamp = datetime.now().strftime("%Y-%m-%d %H:%M:%S")
    print(f"[{timestamp}] [Step - {step_number:02}] {description} [{status}]")

# Function to execute SQL commands on a specific DSN
def execute_sql(sql, conn_info):
    conn_command = f"psql '{conn_info}' -c \"{sql}\""
    result = subprocess.run(conn_command, shell=True, capture_output=True, text=True)
    return result.returncode, result.stdout, result.stderr

# Function to generate SQL for `node_create`
def node_create(node_name, dsn, location, country):
    sql = f"""
    SELECT spock.node_create(
        node_name => '{node_name}',
        dsn => '{dsn}',
        location => '{location}',
        country => '{country}'
    );
    """
    return sql.strip()

# Function to generate SQL for `sub_create`
def sub_create(sub_name, provider_dsn, replication_sets=None, synchronize_structure=True, synchronize_data=True, enabled=True):
    # Set default values for optional parameters
    if replication_sets is None:
        replication_sets = "['default', 'default_insert_only', 'ddl_sql']"

    sql = f"""
    SELECT spock.sub_create(
        subscription_name => '{sub_name}',
        provider_dsn => '{provider_dsn}',
        replication_sets => ARRAY{replication_sets},
        synchronize_structure => {str(synchronize_structure).lower()},
        synchronize_data => {str(synchronize_data).lower()},
        forward_origins => ARRAY[]::text[],
        apply_delay => '0'::interval,
        force_text_transfer => false,
        enabled => {str(enabled).lower()}
    );
    """
    return sql.strip()

# Function to generate SQL for `sub_drop`
def sub_drop(sub_name):
    sql = f"SELECT spock.sub_drop(subscription_name => '{sub_name}');"
    return sql

# Function to generate SQL for `node_drop`
def node_drop(node_name):
    sql = f"SELECT spock.node_drop(node_name => '{node_name}');"
    return sql

def add_node_workflow(verbose):
    steps = [
        {
            "description": f"Create node {NODE3_NAME} in the cluster",
            "sql": node_create(NODE3_NAME, NODE3_DSN, NODE_LOCATION, NODE_COUNTRY),
            "conn_info": NODE3_DSN
        },
        {
            "description": f"On {NODE1_NAME}, create a subscription (sub_{NODE3_NAME}_{NODE1_NAME}) for replicating data from {NODE3_NAME} to {NODE1_NAME}",
            "sql": sub_create(
                sub_name=f"sub_{NODE3_NAME}_{NODE1_NAME}",
                provider_dsn=NODE3_DSN
            ),
            "conn_info": NODE1_DSN
        },
        {
            "description": f"On {NODE2_NAME}, create a subscription (sub_{NODE3_NAME}_{NODE2_NAME}) for replicating data from {NODE3_NAME} to {NODE2_NAME} without synchronizing structure",
            "sql": sub_create(
                sub_name=f"sub_{NODE3_NAME}_{NODE2_NAME}",
                provider_dsn=NODE3_DSN,
                synchronize_structure=False
            ),
            "conn_info": NODE2_DSN
        },
        {
            "description": f"Wait for the apply worker to complete on {NODE2_NAME}",
            "sql": f"SELECT spock.wait_for_apply_worker(${2}, {APPLY_WORKER_TIMEOUT});",
            "conn_info": NODE2_DSN,
            "ignore_error": True
        },
        {
            "description": f"On {NODE3_NAME}, create a subscription (sub_{NODE2_NAME}_{NODE3_NAME}) for replicating data from {NODE2_NAME} to {NODE3_NAME} and keep it disabled",
            "sql": sub_create(
                sub_name=f"sub_{NODE2_NAME}_{NODE3_NAME}",
                provider_dsn=NODE2_DSN,
                synchronize_structure=False,
                synchronize_data=False,
                enabled=False
            ),
            "conn_info": NODE3_DSN
        },
        {
            "description": f"On {NODE2_NAME}, create a logical replication slot for replicating data from {NODE2_NAME} to {NODE3_NAME}",
            "sql": f"SELECT pg_create_logical_replication_slot('{REPLICATION_SLOT}', 'spock_output');",
            "conn_info": NODE2_DSN
        },
        {
            "description": f"Trigger a synchronization event on {NODE2_NAME}",
            "sql": "SELECT spock.sync_event();",
            "conn_info": NODE2_DSN
        },
        {
            "description": f"On {NODE1_NAME}, wait for the synchronization event triggered by {NODE2_NAME}",
            "sql": f"CALL spock.wait_for_sync_event(true, '{NODE2_NAME}', ${7}::pg_lsn, {SYNC_EVENT_TIMEOUT});",
            "conn_info": NODE1_DSN
        },
        {
            "description": f"On {NODE3_NAME}, create a subscription (sub_{NODE1_NAME}_{NODE3_NAME}) for replicating data from {NODE1_NAME} to {NODE3_NAME}",
            "sql": sub_create(
                sub_name=f"sub_{NODE1_NAME}_{NODE3_NAME}",
                provider_dsn=NODE1_DSN
            ),
            "conn_info": NODE3_DSN
        },
        {
            "description": f"Trigger a synchronization event on {NODE1_NAME}",
            "sql": rf"SELECT spock.sync_event();",
            "conn_info": NODE1_DSN
        },
        {
            "description": f"On {NODE3_NAME}, wait for the synchronization event triggered by {NODE1_NAME}",
            "sql": rf"CALL spock.wait_for_sync_event(true, '{NODE1_NAME}', $10::pg_lsn, {SYNC_EVENT_TIMEOUT});",
            "conn_info": NODE3_DSN,
            "ignore_error": True
        }
    ]

    outputs = execute_steps(steps, verbose)

def remove_node_workflow(verbose):
    steps = [
        {"description": f"Drop subscription (sub_{NODE2_NAME}_{NODE3_NAME}) on {NODE3_NAME}", "sql": sub_drop(f"sub_{NODE2_NAME}_{NODE3_NAME}"), "conn_info": NODE3_DSN, "ignore_error": True},
        {"description": f"Drop subscription (sub_{NODE1_NAME}_{NODE3_NAME}) on {NODE3_NAME}", "sql": sub_drop(f"sub_{NODE1_NAME}_{NODE3_NAME}"), "conn_info": NODE3_DSN, "ignore_error": True},
        {"description": f"Drop subscription (sub_{NODE3_NAME}_{NODE1_NAME}) on {NODE1_NAME}", "sql": sub_drop(f"sub_{NODE3_NAME}_{NODE1_NAME}"), "conn_info": NODE1_DSN, "ignore_error": True},
        {"description": f"Drop subscription (sub_{NODE3_NAME}_{NODE2_NAME}) on {NODE2_NAME}", "sql": sub_drop(f"sub_{NODE3_NAME}_{NODE2_NAME}"), "conn_info": NODE2_DSN, "ignore_error": True},
        {"description": f"Drop node {NODE3_NAME}", "sql": node_drop(NODE3_NAME), "conn_info": NODE3_DSN, "ignore_error": True}
    ]

    execute_steps(steps, verbose)

def execute_steps(steps, verbose=0):
    step_outputs = {}  # Store step outputs by step number

    for i, step in enumerate(steps, start=1):
        desc = step["description"]
        sql = step["sql"]
        conn_info = step.get("conn_info")
        ignore_error = step.get("ignore_error", False)

        for key in sorted(step_outputs.keys(), key=lambda x: -len(str(x))):
            placeholder = f"${key}"
            sql = sql.replace(placeholder, step_outputs[key])

        # Replace placeholders like $3 with prior step outputs
        for key, value in step_outputs.items():
            placeholder = f"${key}"
            sql = sql.replace(placeholder, value)

        if verbose >= 1:
            print(f"Executing on: {conn_info}")
        if verbose == 2:
            print(f"SQL Query:\n{sql}\n")

        rc, stdout, stderr = execute_sql(sql, conn_info)

        if rc == 0:
            output_lines = stdout.strip().splitlines()
            # Try to extract useful output (usually 2nd to last line for single-column results)
            extracted = ""
            for line in reversed(output_lines):
                line = line.strip()
                if line and not line.startswith("(") and not line.startswith("sub_create") and not line.startswith("node_create"):
                    extracted = line
                    break

            # Quote only if the output contains a slash (likely pg_lsn)
            if "/" in extracted:
                step_outputs[i] = f"'{extracted}'"
            else:
                step_outputs[i] = extracted

            if verbose >= 1:
                print(stdout)
            log_step(i, desc, "OK")
        elif ignore_error:
            step_outputs[i] = ""
            if verbose >= 1:
                print(f"\033[93m{stderr}\033[0m")
            log_step(i, desc, "IGNORED")
        else:
            if verbose >= 1:
                print(f"\033[91m{stderr}\033[0m")
            log_step(i, desc, "FAILED")
            sys.exit(f"\033[91mStep failed: {desc}\033[0m")

    return step_outputs

if __name__ == "__main__":
    parser = argparse.ArgumentParser(description="Spock Node Management")
    parser.add_argument("-a", "--add-node", action="store_true", help="Add a new node (default)")
    parser.add_argument("-r", "--remove-node", action="store_true", help="Remove an existing node")
    parser.add_argument("-v", "--verbose", type=int, choices=[0, 1, 2], default=0, help="Set verbosity level (0: steps only, 1: steps and output, 2: steps, query, and output)")
    args = parser.parse_args()

    if args.remove_node:
        remove_node_workflow(args.verbose)
    else:
        add_node_workflow(args.verbose)