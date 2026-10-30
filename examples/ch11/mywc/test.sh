#!/bin/sh
# test.sh - compare ./mywc with the system's wc on the same input.
# Prints a line for each difference. Exit status 0 if they all agree.

LC_ALL=C          # wc counts words by locale; use the plain C rules
export LC_ALL
passed=0
failed=0

# same NAME: run the file test.in through wc and ./mywc and compare.
same() {
    want=$(wc < test.in | awk '{ print $1, $2, $3 }')
    got=$(./mywc < test.in)
    if [ "$got" = "$want" ]; then
        passed=$((passed + 1))
    else
        echo "FAIL $1: mywc says '$got', wc says '$want'"
        failed=$((failed + 1))
    fi
}

printf ''                    > test.in; same empty
printf 'one line\n'          > test.in; same one-line
printf 'no newline'          > test.in; same no-newline
printf 'a\tb\r\n\n  c  \n\n' > test.in; same whitespace
cat ./*.c                    > test.in; same sources
cat ./*.c ./*.c ./*.c ./*.c  > test.in; same bigger-than-4096
rm -f test.in

echo "test.sh: $passed passed, $failed failed"
[ "$failed" -eq 0 ]
