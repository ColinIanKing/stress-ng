#!/usr/bin/env bash
set -Eeuo pipefail

compiler=$1
input=$2

eval "$compiler" -E -DHAVE_SYS_PERSONALITY_H "$input" \
| grep -e "PER_[A-Z0-9]* =.*," \
| cut -d "=" -f 1 \
| sed 's/.$/,/'
