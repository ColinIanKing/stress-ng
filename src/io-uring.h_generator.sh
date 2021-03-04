#!/usr/bin/env bash
set -Eeuo pipefail

compiler=$1
input=$2


eval "$compiler" -E -DHAVE_LINUX_IO_URING_H "$input" \
| grep IORING_OP \
| tr "\t" " " | sed 's/,//' \
| sed 's/IORING_OP_/#define HAVE_IORING_OP_/'
