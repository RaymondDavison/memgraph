#!/bin/bash

function echo_info { printf "\033[1;36m~~ $1 ~~\033[0m\n"; }
function echo_success { printf "\033[1;32m~~ $1 ~~\033[0m\n\n"; }
function echo_failure { printf "\033[1;31m~~ $1 ~~\033[0m\n\n"; }

## Environment setup

# Get script location.
DIR="$( cd "$( dirname "${BASH_SOURCE[0]}" )" && pwd )"
cd "$DIR"

# Find memgraph binaries.
binary_dir="$DIR/../../../build"
if [ ! -d $binary_dir ]; then
    binary_dir="$DIR/../../../build_release"
fi

# Results for apollo
RESULTS="$DIR/.apollo_measurements"

# Benchmark parameters
NODES=15000

## Startup
declare -a HA_PIDS

for server_id in 1 2 3
do
  $binary_dir/memgraph_ha --server_id $server_id \
    --coordination_config_file="coordination.json" \
    --raft_config_file="raft.json" \
    --port $((7686 + $server_id)) \
    --durability_directory=dur$server_id &
  HA_PIDS[$server_id]=$!
done

# Allow some time for leader election.
sleep 3

# Start the memgraph process and wait for it to start.
echo_info "Starting HA benchmark"
$binary_dir/tests/feature_benchmark/ha/benchmark \
    --query-count=$NODES \
    --timeout=60 \
    --output-file=$RESULTS &
pid=$!

wait -n $pid
code=$?

# Shutdown
for server_id in 1 2 3
do
  kill -9 ${HA_PIDS[$server_id]}
done

# Cleanup
for server_id in 1 2 3
do
  wait -n ${HA_PIDS[$server_id]}
  rm -r dur$server_id
done

if [ $code -eq 0 ]; then
    echo_success "Benchmark finished successfully"
else
    echo_failure "Benchmark didn't finish successfully"
    exit $code
fi

exit 0
