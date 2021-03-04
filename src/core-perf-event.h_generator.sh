#!/usr/bin/env bash
set -Eeuo pipefail

compiler=$1
input=$2

eval "$compiler" -E -DHAVE_LINUX_PERF_EVENT_H "$input" \
| grep PERF_COUNT \
| tr "\t" " " | sed 's/,/ /' | sed 's/^ *//' \
| awk '{print "#define _SNG_" $1 " (1)"}'
