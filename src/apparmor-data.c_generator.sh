#!/usr/bin/env bash
set -Eeuo pipefail

apparmor_parser=$1
input=$2
output=$3

{
    echo "#include <stddef.h>"
    echo "char g_apparmor_data[]= { "

    eval "$apparmor_parser" -Q "$input" -S \
        | od -tx1 -An -v \
        | sed 's/[0-9a-f][0-9a-f]/0x&,/g' \
        | sed '$ s/.$//'

    echo "};"
    echo "const size_t g_apparmor_data_len = sizeof(g_apparmor_data);"

} > "$output"
