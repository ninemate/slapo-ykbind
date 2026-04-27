#!/bin/sh
set -eu

output=""
drop_subtrees=""
drop_objectclasses=""

usage() {
    cat >&2 <<'EOF'
Usage: filter-unsupported-ldif.sh [OPTIONS] INPUT.ldif

Options:
  --output PATH              Write filtered LDIF to PATH. Defaults to stdout.
  --drop-subtree DN          Drop an entry when its DN is DN or below DN.
  --drop-objectclass NAME    Drop an entry containing objectClass: NAME.
EOF
}

append_value() {
    current="$1"
    value="$2"
    if [ -z "${current}" ]; then
        printf '%s' "${value}"
    else
        printf '%s\034%s' "${current}" "${value}"
    fi
}

input=""
while [ "$#" -gt 0 ]; do
    case "$1" in
        --output)
            [ "$#" -ge 2 ] || { usage; exit 2; }
            output="$2"
            shift 2
            ;;
        --drop-subtree)
            [ "$#" -ge 2 ] || { usage; exit 2; }
            drop_subtrees="$(append_value "${drop_subtrees}" "$2")"
            shift 2
            ;;
        --drop-objectclass)
            [ "$#" -ge 2 ] || { usage; exit 2; }
            drop_objectclasses="$(append_value "${drop_objectclasses}" "$2")"
            shift 2
            ;;
        --help|-h)
            usage
            exit 0
            ;;
        -*)
            usage
            exit 2
            ;;
        *)
            if [ -n "${input}" ]; then
                usage
                exit 2
            fi
            input="$1"
            shift
            ;;
    esac
done

[ -n "${input}" ] || { usage; exit 2; }

awk_program='
function ltrim(s) { sub(/^[ \t]+/, "", s); return s }
function rtrim(s) { sub(/[ \t]+$/, "", s); return s }
function trim(s) { return rtrim(ltrim(s)) }
function lower(s) { return tolower(trim(s)) }
function ends_with(s, suffix) {
    return length(s) >= length(suffix) && substr(s, length(s) - length(suffix) + 1) == suffix
}
function field_value(line) {
    sub(/^[^:]+:[ \t]*/, "", line)
    return line
}
function should_drop_dn(dn,    i, d) {
    dn = lower(dn)
    for (i = 1; i <= subtree_count; i++) {
        d = drop_subtree[i]
        if (d != "" && (dn == d || ends_with(dn, "," d))) {
            return 1
        }
    }
    return 0
}
function should_drop_objectclass(oc,    i) {
    oc = lower(oc)
    for (i = 1; i <= objectclass_count; i++) {
        if (drop_objectclass[i] != "" && oc == drop_objectclass[i]) {
            return 1
        }
    }
    return 0
}
function flush_record(    i, line, dn, drop, line_count) {
    if (record == "") {
        return
    }

    line_count = split(record, lines, "\n")
    dn = ""
    drop = 0
    for (i = 1; i <= line_count; i++) {
        line = lines[i]
        if (line ~ /^dn:[ \t]*/) {
            dn = field_value(line)
            while ((i + 1) <= line_count && lines[i + 1] ~ /^ /) {
                i++
                dn = dn substr(lines[i], 2)
            }
            if (should_drop_dn(dn)) {
                drop = 1
                break
            }
        } else if (line ~ /^[oO][bB][jJ][eE][cC][tT][cC][lL][aA][sS][sS]:[ \t]*/) {
            if (should_drop_objectclass(field_value(line))) {
                drop = 1
                break
            }
        }
    }

    if (!drop) {
        printf "%s\n", record
    }
    record = ""
}
BEGIN {
    subtree_count = split(drop_subtrees, drop_subtree, "\034")
    for (i = 1; i <= subtree_count; i++) {
        drop_subtree[i] = lower(drop_subtree[i])
    }
    objectclass_count = split(drop_objectclasses, drop_objectclass, "\034")
    for (i = 1; i <= objectclass_count; i++) {
        drop_objectclass[i] = lower(drop_objectclass[i])
    }
}
/^$/ {
    flush_record()
    next
}
{
    record = record $0 "\n"
}
END {
    flush_record()
}
'

if [ -n "${output}" ]; then
    awk -v drop_subtrees="${drop_subtrees}" -v drop_objectclasses="${drop_objectclasses}" "${awk_program}" "${input}" > "${output}"
else
    awk -v drop_subtrees="${drop_subtrees}" -v drop_objectclasses="${drop_objectclasses}" "${awk_program}" "${input}"
fi
