#!/bin/bash
#
#  sdc-scan.sh - per-physical-core SDC stress sweep for stress-ng
#
#  Copyright (C) 2026
#
#  This program is free software; you can redistribute it and/or
#  modify it under the terms of the GNU General Public License
#  as published by the Free Software Foundation; either version 2
#  of the License, or (at your option) any later version.
#
#  Sweeps every online physical core (one thread per core, using the
#  SMT sibling pair when SMT is enabled) running the stress-ng compute
#  stressors with --verify, and collects per-core results to identify
#  suspect cores for silent data corruption (SDC) investigation.
#
#  Intended to be run alongside a golden-value checker such as
#  SDCShield running on the same core set for cross-confirmation.
#
#  Usage:
#	NG=./stress-ng ./scripts/sdc-scan.sh [--cpus LIST] [--secs N]
#	        [--method METHOD] [--out DIR] [--sdcshield CMD]
#
#	--cpus LIST     CPU list to scan, default: all online CPUs
#	--secs N        seconds per core, default 120
#	--method METHOD stress-ng --cpu-method, default: all
#	--out DIR       output directory, default: auto sdc_run_<timestamp>
#	--sdcshield CMD optional SDCShield command prefix to run on each
#	                core pair after the stress-ng pass
#

set -u

NG=${NG:-./stress-ng}
CPU_LIST=$(cat /sys/devices/system/cpu/online 2>/dev/null || echo 0)
SECS=120
METHOD=all
OUT=""

#  Parse arguments
while [ $# -gt 0 ]; do
	case "$1" in
	--cpus)
		CPU_LIST="$2"; shift 2 ;;
	--secs)
		SECS="$2"; shift 2 ;;
	--method)
		METHOD="$2"; shift 2 ;;
	--out)
		OUT="$2"; shift 2 ;;
	--sdcshield)
		SDCSHIELD="$2"; shift 2 ;;
	-h|--help)
		sed -n '2,30p' "$0"; exit 0 ;;
	*)
		echo "unknown option: $1" >&2; exit 1 ;;
	esac
done

[ -x "$NG" ] || { echo "stress-ng not found or not executable: $NG (set NG=...)" >&2; exit 1; }

if [ -z "$OUT" ]; then
	OUT="sdc_run_$(date +%Y%m%d_%H%M%S)"
fi
mkdir -p "$OUT" || exit 1

#  ---------------------------------------------------------------------------
#  Stage 0: evidence capture (topology, isolated/offline cores, EDAC, dmesg)
#  ---------------------------------------------------------------------------
echo "=== sdc-scan: stage 0 evidence capture -> $OUT ==="

lscpu -e=CPU,CORE,SOCKET,NODE ONLINE > "$OUT/topology.txt" 2>/dev/null || \
	lscpu -e=CPU,CORE,SOCKET,NODE > "$OUT/topology.txt"

cat /sys/devices/system/cpu/isolated > "$OUT/isolated.txt" 2>/dev/null
cat /sys/devices/system/cpu/offline > "$OUT/offline.txt" 2>/dev/null

{
	for d in /sys/devices/system/edac/mc/mc*; do
		[ -d "$d" ] || continue
		echo "$d: ce=$(cat "$d/ce_count" 2>/dev/null) ue=$(cat "$d/ue_count" 2>/dev/null)"
	done
} > "$OUT/edac-baseline.txt" 2>/dev/null

dmesg 2>/dev/null | grep -iE 'edac|mce|machine check|fault|deconfig|cpu.*err|lockup' \
	> "$OUT/dmesg-baseline.txt" || true

#  Build the SMT sibling map: for each online CPU, the lowest sibling
#  is the physical-core representative; scan each representative once.
#
#  Expand the CPU list into a set, then for each cpu read
#  thread_siblings_list and keep only lowest-numbered siblings.
#
declare -A rep_of_pair	# lowest sibling -> "cpuA,cpuB" pair string
declare -A is_online

expand_list()
{
	#  expand "0-3,8,10-11" into "0 1 2 3 8 10 11"
	local list="$1" out="" part lo hi
	local IFS=','
	for part in $list; do
		if [[ "$part" == *-* ]]; then
			lo="${part%%-*}"; hi="${part##*-}"
			for ((i = lo; i <= hi; i++)); do out="$out $i"; done
		else
			out="$out $part"
		fi
	done
	echo "$out"
}

for c in $(expand_list "$CPU_LIST"); do
	is_online[$c]=1
done

for c in "${!is_online[@]}"; do
	sib=$(cat "/sys/devices/system/cpu/cpu$c/topology/thread_siblings_list" 2>/dev/null || echo "$c")
	#  expand siblings and find the lowest
	lowest=""
	for s in $(expand_list "$sib"); do
		[ -z "$lowest" ] && lowest=$s
		[ "$s" -lt "$lowest" ] && lowest=$s
	done
	#  join pair as comma list of online siblings (only scan-relevant ones)
	pair=""
	for s in $(expand_list "$sib"); do
		[ -n "${is_online[$s]}" ] || continue
		[ -z "$pair" ] && pair="$s" || pair="$pair,$s"
	done
	[ -z "$pair" ] && continue
	#  keep the pair with the most CPUs for this representative
	cur="${rep_of_pair[$lowest]:-}"
	if [ -z "$cur" ] || [ "${#pair}" -gt "${#cur}" ]; then
		rep_of_pair[$lowest]="$pair"
	fi
done

n_cores=${#rep_of_pair[@]}
echo "=== sdc-scan: $n_cores physical cores to sweep (secs=$SECS method=$METHOD) ==="
printf '%s\n' "${rep_of_pair[@]}" | tr ',' '\n' | sort -n | uniq > "$OUT/scanned-cpus.txt"

#  Skip isolated cores if any
ISOLATED=$(cat /sys/devices/system/cpu/isolated 2>/dev/null)
if [ -n "$ISOLATED" ]; then
	echo "note: isolated CPUs present: $ISOLATED (already offline, cannot be scanned)" | tee -a "$OUT/isolated.txt"
fi

#  ---------------------------------------------------------------------------
#  Stage C: per-core sweep
#  ---------------------------------------------------------------------------
idx=0
fails=0
for rep in $(printf '%s\n' "${!rep_of_pair[@]}" | sort -n); do
	pair=${rep_of_pair[$rep]}
	idx=$((idx + 1))
	yaml="$OUT/core_${pair//,/-}.yaml"
	log="$OUT/core_${pair//,/-}.log"
	echo "--- [$idx/$n_cores] physical core CPUs=$pair ---"
	"$NG" --taskset "$pair" --cpu 2 --cpu-method "$METHOD" --fma 2 --verify \
		--metrics-brief -Y "$yaml" -t "${SECS}s" > "$log" 2>&1
	rc=$?
	if [ $rc -ne 0 ]; then
		fails=$((fails + 1))
		echo "SUSPECT: core $pair stress-ng rc=$rc" | tee -a "$OUT/suspects.txt"
		grep -E "fail|difference|error" "$log" | head -3 | tee -a "$OUT/suspects.txt"
	else
		grep -q "failed: 0" "$log" || {
			fails=$((fails + 1))
			echo "SUSPECT: core $pair verify failures:" | tee -a "$OUT/suspects.txt"
			grep -E "fail|difference" "$log" | head -3 | tee -a "$OUT/suspects.txt"
		}
	fi

	#  Optional SDCShield cross-check on the same core pair
	if [ -n "${SDCSHIELD:-}" ]; then
		shield_log="$OUT/core_${pair//,/-}_shield.log"
		taskset -c "$pair" $SDCSHIELD -t 60 -Y > "$shield_log" 2>&1
		if grep -qE "FAIL|fail" "$shield_log"; then
			fails=$((fails + 1))
			echo "SUSPECT: core $pair sdcshield failure:" | tee -a "$OUT/suspects.txt"
			grep -E "FAIL|fail" "$shield_log" | head -3 | tee -a "$SOUT/suspects.txt" 2>/dev/null || true
		fi
	fi
done

#  ---------------------------------------------------------------------------
#  Summary
#  ---------------------------------------------------------------------------
{
	echo "=== sdc-scan summary ==="
	echo "cores scanned : $n_cores"
	echo "suspect cores : $fails"
	[ -f "$OUT/suspects.txt" ] && cat "$OUT/suspects.txt" || echo "(no suspects)"
} | tee "$OUT/summary.txt"

#  EDAC delta
{
	for d in /sys/devices/system/edac/mc/mc*; do
		[ -d "$d" ] || continue
		echo "$d: ce=$(cat "$d/ce_count" 2>/dev/null) ue=$(cat "$d/ue_count" 2>/dev/null)"
	done
} > "$OUT/edac-after.txt" 2>/dev/null
diff "$OUT/edac-baseline.txt" "$OUT/edac-after.txt" > "$OUT/edac-delta.txt" 2>/dev/null || true

echo "=== sdc-scan done, results in $OUT ==="
[ "$fails" -eq 0 ] || exit 2
exit 0
