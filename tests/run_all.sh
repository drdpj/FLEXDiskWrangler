#!/bin/bash
# FLEX Disk Wrangler - Copyright (C) 2026 Daniel Jameson - GPL v3 or later
# run_all.sh <build dir> [<the sample FLEX discs' folder> [<the UniFLEX sample discs' folder>]]
# Builds the program, flexdw and every test (the core's too), makes the synthetic discs (flex_check.py make and
# uniflex_check.py make), and runs: the core's tests, voltest, the GUI test, flexdw's golden cases, the independent
# FLEX and UniFLEX readers against flexdw on every synthetic disc and (given the folders) every sample disc, flexdw's
# changes against the independent model of how FLEX writes (flex_check.py writes), and the check that flexdw links no
# Qt GUI library.
# Ends "ALL PASSED" or "SOME TESTS FAILED"; exit status 0 only if everything passed.
src=$(cd "$(dirname "$0")/.." && pwd)
b=${1:?usage: run_all.sh <build dir> [<sample FLEX discs> [<sample UniFLEX discs>]]}; real=$2; ufreal=$3
[ -n "$real" ] && real=$(cd "$real" && pwd)
[ -n "$ufreal" ] && ufreal=$(cd "$ufreal" && pwd)
mkdir -p "$b"; b=$(cd "$b" && pwd)
fail=0
jobs=$(nproc 2>/dev/null || echo 4)
step() { echo; echo "######## $*"; }
build() { mkdir -p "$b/$1" && (cd "$b/$1" && qmake "$2" >/dev/null && make -j"$jobs" >/dev/null 2>"$b/$1.log") || { echo "BUILD FAILED: $1 (see $b/$1.log)"; fail=1; }; }
step "build ($(qmake -query QT_VERSION 2>/dev/null | sed 's/^/Qt /'))"
build app "$src/flexdiskwrangler.pro"
build flexdw "$src/flexdw.pro"
for t in voltest guitest; do build $t "$src/tests/$t.pro"; done
for t in settingstest xfertest uitest devicetest importtest clitest volumetest moduletest; do build $t "$src/core/tests/$t.pro"; done
export QT_QPA_PLATFORM=offscreen
W=$(mktemp -d); chmod 755 "$W"
python3 -I "$src/tests/flex_check.py" make "$W/syn" || { echo "FAIL: flex_check.py make"; fail=1; }
python3 -I "$src/tests/uniflex_check.py" make "$W/syn" || { echo "FAIL: uniflex_check.py make"; fail=1; }
# the sample discs are only read, but the tests work on copies, as the protocol says
if [ -n "$real" ]; then mkdir -p "$W/real" && cp "$real"/* "$W/real/"; fi
if [ -n "$ufreal" ]; then mkdir -p "$W/ufreal" && cp "$ufreal"/*.dsk "$W/ufreal/"; fi

step "core: its own tests, against the pinned tag"
for t in settingstest xfertest uitest devicetest importtest clitest volumetest moduletest; do
  timeout 300 "$b/$t/$t" > "$W/$t.log" 2>&1
  tail -1 "$W/$t.log" | grep -qi "all ok" && echo "$t: ok" || { echo "FAIL $t"; tail -20 "$W/$t.log"; fail=1; }
done

step "voltest: detection, the volumes, changes, faithful reload, Save As${real:+, the sample discs}${ufreal:+, the UniFLEX samples}"
timeout 600 "$b/voltest/voltest" "$W/syn" ${real:+"$W/real"} ${real:+${ufreal:+"$W/ufreal"}} > "$W/voltest.log" 2>&1
tail -1 "$W/voltest.log" | grep -q "ALL OK" && echo "voltest: $(grep -c '  ok:' "$W/voltest.log") checks passed" || { grep -v "  ok:" "$W/voltest.log"; fail=1; }

step "guitest: the window"
timeout 300 "$b/guitest/guitest" "$W/syn" > "$W/guitest.log" 2>&1
tail -1 "$W/guitest.log" | grep -q "ALL OK" && echo "guitest: $(grep -c '  ok:' "$W/guitest.log") checks passed" || { grep -v "  ok:" "$W/guitest.log"; fail=1; }

step "flexdw: golden cases, and no Qt GUI libraries"
timeout 600 "$src/tests/cli/run_cli.sh" "$b/flexdw/flexdw" "$W/cli" > "$W/cli.log" 2>&1
tail -1 "$W/cli.log" | grep -q "^ALL PASSED" && echo "flexdw golden: $(grep -c '^pass' "$W/cli.log") cases passed" || { grep -v '^pass' "$W/cli.log" | head -30; fail=1; }
if command -v ldd >/dev/null 2>&1; then
  ldd "$b/flexdw/flexdw" | grep -qiE "QtGui|QtWidgets" && { echo "FAIL: flexdw links QtGui or QtWidgets"; fail=1; } || echo "flexdw links neither QtGui nor QtWidgets"
fi

step "the independent FLEX reader (flex_check.py) against flexdw: synthetic discs"
(cd "$W/syn" && timeout 600 python3 -I "$src/tests/flex_check.py" compare "$b/flexdw/flexdw" *.dsk noise.bin) > "$W/syn-compare.log" 2>&1 || fail=1
cat "$W/syn-compare.log"
if [ -n "$real" ]; then
  step "the independent FLEX reader against flexdw: the sample discs ($real)"
  (cd "$W/real" && timeout 900 python3 -I "$src/tests/flex_check.py" compare "$b/flexdw/flexdw" *) > "$W/real-compare.log" 2>&1 || fail=1
  cat "$W/real-compare.log"
else
  echo; echo "(no sample discs given: the real-disc checks were not run)"
fi

step "the independent UniFLEX reader (uniflex_check.py) against flexdw: synthetic discs"
timeout 600 python3 -I "$src/tests/uniflex_check.py" compare "$b/flexdw/flexdw" "$W/syn"/uf-*.dsk > "$W/uf-compare.log" 2>&1 || fail=1
sed "s#$W/syn/##" "$W/uf-compare.log"
if [ -n "$real" ] && [ -n "$ufreal" ]; then
  step "the UniFLEX sample discs ($ufreal, and C012_1, C014 in $real): the reader, and each check as uniflex_samples.txt says"
  timeout 1800 python3 -I "$src/tests/uniflex_check.py" samples "$b/flexdw/flexdw" "$src/tests/uniflex_samples.txt" "$W/ufreal" "$W/real" > "$W/uf-samples.log" 2>&1 || fail=1
  cat "$W/uf-samples.log"
else
  echo; echo "(no UniFLEX sample folder given: the UniFLEX real-disc checks were not run)"
fi

step "flexdw's changes against the independent model of how FLEX writes (flex_check.py writes)${real:+, the sample system discs too}"
timeout 900 python3 -I "$src/tests/flex_check.py" writes "$b/flexdw/flexdw" "$W/syn" ${real:+"$W/real"} > "$W/writes.log" 2>&1 || fail=1
cat "$W/writes.log"

rm -rf "$W"
echo; [ $fail -eq 0 ] && echo "ALL PASSED" || echo "SOME TESTS FAILED"
exit $fail
