#!/usr/bin/env bash
# test.sh - run the C test suites (tests/<suite>/main.c, built by `make test`).
#
#   scripts/test.sh                   every case
#   scripts/test.sh mech              one suite
#   scripts/test.sh mech/knob-orbit-quarter-turn
#   scripts/test.sh --audit           check break switches against the tests
#
# Each case runs in its own process (a fresh app every time), in parallel,
# headless (a private Xvfb display). A case that names a break switch
# (fails_if) is run a second time with that switch on -- the "red leg":
#
#   normal run   with the switch on   result
#   passes       fails                PASS
#   fails        (not run)            FAIL
#   passes       still passes         UNTRUSTED  (the test doesn't detect what it claims)
#
# Cases without a switch pass as "unproven" and are listed, so gaps stay visible.
# Exit status is non-zero on any FAIL or UNTRUSTED.
set -uo pipefail
cd "$(dirname "$0")/.."
BUILD=${BUILD:-build/host-test}

list_cases() {   # "binary case fails_if" per line
    for bin in "$BUILD"/tests/*; do
        [ -x "$bin" ] || continue
        "$bin" --list | while read -r name brk; do echo "$bin $name $brk"; done
    done
}

if [ "${1:-}" = "--audit" ]; then
    declared=$(cat sfxr/src/*_breaks.def vrui/src/*_breaks.def examples/*/*_breaks.def apps/*/*_breaks.def 2>/dev/null \
               | sed -n 's/^BREAK(\([a-z0-9_]*\),.*/\1/p' | sort -u)
    used=$(list_cases | awk '$3 != "-" { print $3 }' | sort -u)
    status=0
    for b in $declared; do grep -qx "$b" <<<"$used" || { echo "UNUSED switch (no test proves it): $b"; status=1; }; done
    for b in $used; do grep -qx "$b" <<<"$declared" || { echo "UNKNOWN switch named by a test: $b"; status=1; }; done
    list_cases | awk '$3 == "-" { print "unproven case (no fails_if): " $2 }'
    [ $status = 0 ] && echo "audit: every switch is used by a test, every test names a real switch"
    exit $status
fi

filter=${1:-}
cases=$(list_cases | awk -v f="$filter" 'f == "" || $2 == f || index($2, f "/") == 1')
[ -n "$cases" ] || { echo "no test cases match '${filter}' (built? make test)"; exit 2; }

# Private headless display.
if [ -z "${SFXT_DISPLAY:-}" ]; then
    command -v Xvfb >/dev/null || { echo "needs Xvfb (apt install xvfb)"; exit 2; }
    DISP=":$((90 + RANDOM % 400))"
    Xvfb "$DISP" -screen 0 640x480x24 >/dev/null 2>&1 &
    XVFB=$!
    trap 'kill $XVFB 2>/dev/null' EXIT
    sleep 0.5
    export DISPLAY=$DISP
else
    export DISPLAY=$SFXT_DISPLAY
fi
export LIBGL_ALWAYS_SOFTWARE=1

out=$(mktemp -d)
run_one() {   # bin name brk outdir
    local bin=$1 name=$2 brk=$3 o=$4 f
    f="$o/$(echo "$name" | tr '/' '_')"
    local t0=$SECONDS
    if ! SFXR_BREAK= "$bin" "$name" >"$f.out" 2>&1; then
        echo "FAIL $name" >"$f.res"
    elif [ "$brk" = "-" ]; then
        echo "PASS $name (unproven)" >"$f.res"
    elif SFXR_BREAK=$brk "$bin" "$name" >"$f.red" 2>&1; then
        echo "UNTRUSTED $name (still passes with $brk on)" >"$f.res"
    else
        echo "PASS $name (red leg: $brk)" >"$f.res"
    fi
}
export -f run_one
echo "$cases" | xargs -P "$(nproc)" -L 1 bash -c 'run_one "$0" "$1" "$2" '"$out"

pass=0; fail=0; untrusted=0; unproven=0
for r in "$out"/*.res; do
    line=$(cat "$r")
    echo "$line"
    case $line in
        FAIL*)      fail=$((fail + 1)); sed 's/^/    /' "${r%.res}.out" | grep -v '^    PASS\|^    FAIL' ;;
        UNTRUSTED*) untrusted=$((untrusted + 1)) ;;
        *unproven*) pass=$((pass + 1)); unproven=$((unproven + 1)) ;;
        PASS*)      pass=$((pass + 1)) ;;
    esac
done
rm -rf "$out"
echo "---"
echo "$pass passed ($unproven unproven), $fail failed, $untrusted untrusted"
[ $fail = 0 ] && [ $untrusted = 0 ]
