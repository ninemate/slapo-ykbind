#!/bin/sh
set -eu

usage() {
    cat >&2 <<'EOF'
Usage: check-schema-ldif-state.sh --schema-ldif FILE --current-ldif FILE

Compares an OpenLDAP olcSchemaConfig LDIF with an existing cn=schema,cn=config
dump and prints key=value state lines:

  state=missing   no schema entry and none of its OIDs are present
  state=present   schema entry or all schema OIDs are already present
  state=conflict  only part of the schema OIDs are present, or the schema name
                  exists with missing definitions
  state=invalid   the input LDIF does not contain schema definitions
EOF
}

schema_ldif=""
current_ldif=""

while [ "$#" -gt 0 ]; do
    case "$1" in
        --schema-ldif)
            schema_ldif="${2:-}"
            shift 2
            ;;
        --current-ldif)
            current_ldif="${2:-}"
            shift 2
            ;;
        -h|--help)
            usage
            exit 0
            ;;
        *)
            echo "Unknown argument: $1" >&2
            usage
            exit 2
            ;;
    esac
done

if [ -z "$schema_ldif" ] || [ -z "$current_ldif" ]; then
    usage
    exit 2
fi

if [ ! -r "$schema_ldif" ]; then
    echo "Schema LDIF is not readable: $schema_ldif" >&2
    exit 2
fi

if [ ! -r "$current_ldif" ]; then
    echo "Current schema LDIF is not readable: $current_ldif" >&2
    exit 2
fi

schema_unfolded="$(mktemp)"
current_unfolded="$(mktemp)"
oids_file="$(mktemp)"
trap 'rm -f "$schema_unfolded" "$current_unfolded" "$oids_file"' EXIT

unfold_ldif() {
    awk '
        BEGIN { line = "" }
        /^ / { line = line substr($0, 2); next }
        {
            if (line != "") {
                print line
            }
            line = $0
        }
        END {
            if (line != "") {
                print line
            }
        }
    ' "$1"
}

unfold_ldif "$schema_ldif" > "$schema_unfolded"
unfold_ldif "$current_ldif" > "$current_unfolded"

schema_name="$(
    awk '
        /^cn: / {
            v = $0
            sub(/^cn:[[:space:]]*/, "", v)
            sub(/^\{[0-9]+\}/, "", v)
            print v
            exit
        }
    ' "$schema_unfolded"
)"

if [ -z "$schema_name" ]; then
    schema_name="$(basename "$schema_ldif" .ldif)"
fi

awk '
    /^olcAttributeTypes: / || /^olcObjectClasses: / {
        v = $0
        sub(/^[^:]+:[[:space:]]*/, "", v)
        if (match(v, /\([[:space:]]*[0-9][0-9.]*/)) {
            oid = substr(v, RSTART, RLENGTH)
            sub(/^\([[:space:]]*/, "", oid)
            print oid
        }
    }
' "$schema_unfolded" | sort -u > "$oids_file"

definition_total="$(sed '/^$/d' "$oids_file" | wc -l | tr -d ' ')"
definition_hits=0
matched_oids=""

while IFS= read -r oid; do
    [ -n "$oid" ] || continue
    if grep -F "$oid" "$current_unfolded" >/dev/null 2>&1; then
        definition_hits=$((definition_hits + 1))
        if [ -z "$matched_oids" ]; then
            matched_oids="$oid"
        else
            matched_oids="${matched_oids},${oid}"
        fi
    fi
done < "$oids_file"

schema_name_re="$(printf '%s' "$schema_name" | sed 's/[][(){}.+*?^$|\\]/\\&/g')"
schema_entry_present=false
if grep -Eq "^(dn: cn=(\{[0-9]+\})?${schema_name_re},cn=schema,cn=config|cn: (\{[0-9]+\})?${schema_name_re})$" "$current_unfolded"; then
    schema_entry_present=true
fi

if [ "$definition_total" -eq 0 ]; then
    state=invalid
elif [ "$schema_entry_present" = "true" ] && [ "$definition_hits" -eq "$definition_total" ]; then
    state=present
elif [ "$schema_entry_present" = "true" ]; then
    state=conflict
elif [ "$definition_hits" -eq 0 ]; then
    state=missing
elif [ "$definition_hits" -eq "$definition_total" ]; then
    state=present
else
    state=conflict
fi

printf 'schema_name=%s\n' "$schema_name"
printf 'schema_entry_present=%s\n' "$schema_entry_present"
printf 'definition_total=%s\n' "$definition_total"
printf 'definition_hits=%s\n' "$definition_hits"
printf 'matched_oids=%s\n' "$matched_oids"
printf 'state=%s\n' "$state"
