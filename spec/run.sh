#!/usr/bin/env bash
# Re-run every TLA+ model/configuration in this directory with TLC, one at a time,
# with 2 TLC worker threads (the machine is shared).
#
# Usage: ./run.sh                 run every normal configuration
#        LONG=1 ./run.sh          also run the long liveness/large configurations
#        ./run.sh Suspension      run only configurations whose name starts with the argument
#        WORKERS=4 ./run.sh       use a different TLC worker count
#        SUMMARY_ONLY=1 ./run.sh  only rebuild results/summary.txt from existing outputs
# Output: results/<config>.out        full TLC output (including any counterexample)
#         results/<config>.trace.txt  condensed counterexample (changed variables per step)
#         results/summary.txt         one line per configuration
set -u
HERE="$(cd "$(dirname "$0")" && pwd)"
cd "$HERE"
JAVA="${JAVA:-/usr/bin/java}"
WORKERS="${WORKERS:-2}"
LONG="${LONG:-0}"
JAR="$HERE/tools/tla2tools.jar"
JAR_SHA256=936a262061c914694dfd669a543be24573c45d5aa0ff20a8b96b23d01e050e88
if [ ! -f "$JAR" ]; then
  mkdir -p tools
  curl -sfL -o "$JAR" https://github.com/tlaplus/tlaplus/releases/download/v1.7.4/tla2tools.jar
fi
if [ "$(shasum -a 256 "$JAR" | cut -d' ' -f1)" != "$JAR_SHA256" ]; then
  echo "tools/tla2tools.jar does not match the pinned v1.7.4 checksum" >&2
  exit 1
fi
mkdir -p results states

# config  module  expected  [long]
#   pass              : no invariant or property may fail
#   fail(old)         : the OLD protocol (8a9f1a9) is expected to fail
#   fail(mutation)    : a deliberately broken variant, expected to fail
#   fail(witness)     : a coverage witness (reachability check), expected to fail
RUNS=(
  "Termination                   Termination        pass"
  "Termination_stall             Termination        pass"
  "Termination_stall_big         Termination        pass"
  "Termination_mut_enqueue       Termination        fail(mutation)"
  "Termination_mut_take          Termination        fail(mutation)"
  "Termination_stall_big_live    Termination        pass             long"
  "TerminationBlocks             TerminationBlocks  pass"
  "TerminationBlocks_c1          TerminationBlocks  pass"
  "TerminationBlocks_big         TerminationBlocks  pass"
  "TerminationBlocks_mut_count   TerminationBlocks  fail(mutation)"
  "TerminationBlocks_c1_live     TerminationBlocks  pass             long"
  "TerminationFlags_safety       TerminationFlags   fail(old)"
  "TerminationFlags_stalebit     TerminationFlags   fail(old)"
  "TerminationFlags_live         TerminationFlags   fail(old)"
  "TerminationFlags_stall        TerminationFlags   fail(old)"
  "TerminationFlags_stall_only   TerminationFlags   fail(old)"
  "TerminationFlags_nostall_small TerminationFlags  pass"
  "TerminationFlags_alldone      TerminationFlags   pass"
  "TerminationFlags_overapprox   TerminationFlags   fail(old)"
  "Suspension                    Suspension         pass"
  "Suspension_b9d4               Suspension         pass"
  "Suspension_sep                Suspension         pass"
  "Suspension_big                Suspension         pass"
  "Suspension_live               Suspension         pass"
  "Suspension_mut_norecheck      Suspension         fail(mutation)"
  "Suspension_mut_blind          Suspension         fail(mutation)"
  "Suspension_mut_checkthenstore Suspension         fail(mutation)"
  "Suspension_mut_relaxed        Suspension         fail(mutation)"
  "Suspension_cover_W_StaleEntryCas           Suspension fail(witness)"
  "Suspension_cover_W_TwoEntriesOneSuspension Suspension fail(witness)"
  "Suspension_cover_W_CasRace                 Suspension fail(witness)"
  "Suspension_cover_W_NullListRace            Suspension fail(witness)"
  "Suspension_cover_W_TransferBeforeRecheck   Suspension fail(witness)"
  "CycleDetection_n2             CycleDetection     pass"
  "CycleDetection_n3c1           CycleDetection     pass"
  "CycleDetection_n3_full        CycleDetection     pass             long"
  "CycleDetection_n2_b9d4        CycleDetection     pass"
  "CycleDetection_n3c1_b9d4      CycleDetection     pass"
  "CycleDetection_mut_noconsensus  CycleDetection   fail(mutation)"
  "CycleDetection_mut_walkfirst    CycleDetection   fail(mutation)"
  "CycleDetection_mut_nosecondpass CycleDetection   fail(mutation)"
  "CycleDetection_n2_cover_W_Resolves         CycleDetection fail(witness)"
  "CycleDetection_n2_cover_W_ConcurrentDetect CycleDetection fail(witness)"
  "CycleDetection_cover_W_Resolves            CycleDetection fail(witness)"
  "CycleDetection_cover_W_ConcurrentDetect    CycleDetection fail(witness)"
  "CycleDetection_cover_W_OddAge              CycleDetection fail(witness)"
  "CycleDetection_cover_W_BlockedOnResolver   CycleDetection fail(witness)"
)

SUMMARY=results/summary.txt

summarize() {   # name expect long -> one summary line from results/<name>.out
  local name=$1 expect=$2 long=$3 out="results/$1.out" verdict match stats depth secs workers
  if [ ! -f "$out" ]; then
    printf '%-44s %-15s %-12s %s\n' "$name" "$expect" "-" "not run${long:+ (long; LONG=1)}"
    return
  fi
  if grep -q "Model checking completed. No error has been found." "$out"; then
    verdict="PASS"
  elif grep -qE "^Error: (Invariant .* is violated|Temporal properties were violated)" "$out"; then
    verdict="FAIL: $(grep -E '^Error: (Invariant .* is violated|Temporal properties were violated)' "$out" | head -1 | sed 's/^Error: //; s/Temporal properties were violated./temporal property violated/; s/ is violated\./ violated/')"
  else
    verdict="ERROR (incomplete run, see $out)"
  fi
  case "$expect:$verdict" in
    pass:PASS|fail*:FAIL*) match="as expected" ;;
    *) match="UNEXPECTED" ;;
  esac
  stats=$(grep -E "states generated, .* distinct states found" "$out" | tail -1 | sed 's/ states generated, / gen, /; s/ distinct states found, .*/ distinct/')
  depth=$(grep -E "depth of the complete state graph" "$out" | tail -1 | sed 's/.* is \([0-9]*\)\./depth \1/')
  workers=$(grep -oE "with [0-9]+ workers" "$out" | head -1 | sed 's/with //; s/ workers/w/')
  secs=$(cat "results/$name.time" 2>/dev/null || echo "?")
  printf '%-44s %-15s %-12s %-42s | %s | %s | %ss | %s\n' "$name" "$expect" "$match" "$verdict" "${stats:--}" "${depth:--}" "$secs" "${workers:-?}"
}

for line in "${RUNS[@]}"; do
  read -r name mod expect long <<<"$line"
  if [ $# -gt 0 ] && [[ "$name" != "$1"* ]]; then continue; fi
  if [ "${long:-}" = "long" ] && [ "$LONG" != "1" ]; then continue; fi
  if [ ! -f "$name.cfg" ]; then echo "missing $name.cfg"; continue; fi
  if [ "${SUMMARY_ONLY:-0}" = "1" ]; then continue; fi
  start=$(date +%s)
  "$JAVA" -XX:+UseParallelGC -cp "$JAR" tlc2.TLC -workers "$WORKERS" -deadlock \
      -config "$name.cfg" -metadir "states/$name" "$mod.tla" > "results/$name.out" 2>&1
  end=$(date +%s)
  echo "$((end-start))" > "results/$name.time"
  rm -rf "states/$name"
  if grep -qE "^Error: (Invariant .* is violated|Temporal properties were violated)" "results/$name.out"; then
    python3 -I scripts/trace_summary.py "results/$name.out" > "results/$name.trace.txt" 2>/dev/null || true
  fi
  summarize "$name" "$expect" "${long:-}"
done

# Rebuild the summary from every output file present (so partial runs keep it whole).
{
  printf '%-44s %-15s %-12s %-42s | %s | %s | %s | %s\n' "config" "expected" "match" "verdict" "states" "depth" "wall" "TLC workers"
  for line in "${RUNS[@]}"; do
    read -r name mod expect long <<<"$line"
    summarize "$name" "$expect" "${long:-}"
  done
} > "$SUMMARY"
echo "summary written to $SUMMARY"
