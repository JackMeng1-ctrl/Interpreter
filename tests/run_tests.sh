#!/usr/bin/env bash
#
# Runs the examples and a set of small programs under lexical and dynamic
# scope and checks their output.
#
# Usage: make test    (or tests/run_tests.sh after building both programs)

set -uo pipefail
cd "$(dirname "$0")/.."

PARSE=${PARSE:-./parse}
INTERP=${INTERP:-./interpreter}
passed=0
failed=0

# check MODE EXPECTED FILE [LABEL]
# EXPECTED is the exact output, or ERROR when the program must stop with an
# interpreter error (non-zero exit status and a message starting "Error:").
check() {
    local mode=$1 expected=$2 file=$3 label=${4:-$3}
    local actual status ok=0
    actual=$("$PARSE" < "$file" | "$INTERP" "--$mode" 2>&1)
    status=$?
    if [[ $expected == ERROR ]]; then
        [[ $status -ne 0 && $actual == Error:* ]] && ok=1
    else
        [[ $status -eq 0 && $actual == "$expected" ]] && ok=1
    fi
    if (( ok )); then
        passed=$((passed + 1))
    else
        failed=$((failed + 1))
        printf 'FAIL  %s  (--%s)\n      expected: %s\n      actual:   %s\n' \
               "$label" "$mode" "$expected" "$actual"
    fi
}

# check_program MODE EXPECTED 'source code'
check_program() {
    local tmp
    tmp=$(mktemp)
    printf '%s\n' "$3" > "$tmp"
    check "$1" "$2" "$tmp" "$3"
    rm -f "$tmp"
}

# Examples
check lexical 3628800 examples/factorial.417
check dynamic 3628800 examples/factorial.417
check lexical 3628800 examples/self_application.417
check dynamic 3628800 examples/self_application.417
check lexical 6765    examples/fibonacci.417
check dynamic 6765    examples/fibonacci.417
check lexical 11      examples/scope.417
check dynamic 101     examples/scope.417
check lexical 15      examples/closures.417
check dynamic ERROR   examples/closures.417
check lexical 3       examples/counter.417
check dynamic ERROR   examples/counter.417
check lexical $'hello\n417\ntrue' examples/strings.417
check dynamic $'hello\n417\ntrue' examples/strings.417

# Values and built-ins
check_program lexical 42            '42'
check_program lexical hello         '"hello"'
check_program lexical 10            'x'
check_program lexical true          'eq(add(2, 3), 5)'
check_program lexical false         'zero?(sub(5, 4))'
check_program lexical '<function>'  'λ(n) { n }'
check_program lexical 1             '{ let n = 0; cond ((n, 0) => 1) (true => 2) }'
check_program lexical 7             '{ let y = 3; y = add(y, 4); y }'

# Errors
check_program lexical ERROR '{ }'
check_program lexical ERROR 'undefined_name'
check_program lexical ERROR 'add(1, true)'
check_program lexical ERROR 'eq(1, "1")'
check_program lexical ERROR 'λ(a) { a }(1, 2)'
check_program lexical ERROR '{ let f = 1; f(2) }'
check_program lexical ERROR 'cond (eq(1, 2) => 1)'
check_program lexical ERROR 'cond (1 => 2)'
check_program lexical ERROR '{ undefined_name = 1 }'
check_program lexical ERROR 'mul(9223372036854775807, 2)'
check_program lexical ERROR '{ let loop = 0; loop = λ(n) { loop(n) }; loop(1) }'

printf '%d passed, %d failed\n' "$passed" "$failed"
(( failed == 0 ))
