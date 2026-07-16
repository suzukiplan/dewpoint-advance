#!/bin/sh
set -eu

tmp_dir=$(mktemp -d "${TMPDIR:-/tmp}/conftype-test.XXXXXX")
trap 'rm -rf "$tmp_dir"' EXIT HUP INT TERM

printf '\357\273\277 AppName = Test Game\nEmpty =\nEquals = left=right\n# ignored\n' > "$tmp_dir/package.conf"
printf 'name=${AppName}\nempty=[${Empty}]\nequals=${Equals}\nunknown=${Unknown}\nincomplete=${AppName' > "$tmp_dir/input"
printf 'name=Test Game\nempty=[]\nequals=left=right\nunknown=${Unknown}\nincomplete=${AppName' > "$tmp_dir/expected"

./conftype "$tmp_dir/package.conf" "$tmp_dir/input" > "$tmp_dir/actual"
cmp "$tmp_dir/expected" "$tmp_dir/actual"

printf 'invalid line\n' > "$tmp_dir/invalid.conf"
if ./conftype "$tmp_dir/invalid.conf" "$tmp_dir/input" > /dev/null 2> /dev/null; then
    echo "conftype accepted an invalid configuration" >&2
    exit 1
fi

echo "conftype tests passed"
