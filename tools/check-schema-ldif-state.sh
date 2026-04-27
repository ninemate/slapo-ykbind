#!/bin/sh
set -eu

usage() {
    cat >&2 <<'EOF'
Usage: check-schema-ldif-state.sh --schema-ldif FILE --current-ldif FILE [--output-import-ldif FILE]

Compares one schema LDIF or a full cn=schema,cn=config dump with the current
target schema. When --output-import-ldif is set, it writes an LDIF containing
only schema entries that are completely missing on the target.

States:
  state=missing   at least one schema entry should be imported
  state=present   all schema definitions are already present or skipped safely
  state=conflict  an entry has OIDs that already exist under a different schema
  state=invalid   no olcAttributeTypes/olcObjectClasses definitions were found
EOF
}

schema_ldif=""
current_ldif=""
output_import_ldif=""

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
        --output-import-ldif)
            output_import_ldif="${2:-}"
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

tmpdir="$(mktemp -d)"
trap 'rm -rf "$tmpdir"' EXIT

schema_unfolded="$tmpdir/schema.unfolded.ldif"
current_unfolded="$tmpdir/current.unfolded.ldif"
current_oids="$tmpdir/current.oids"
current_names="$tmpdir/current.names"
entry_list="$tmpdir/entries.list"

unfold_ldif() {
    awk '
        BEGIN { line = "" }
        /^ / { line = line substr($0, 2); next }
        /^$/ {
            if (line != "") {
                print line
                line = ""
            }
            print ""
            next
        }
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

extract_oids() {
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
    ' "$1" | sort -u
}

extract_names() {
    awk '
        /^cn: / {
            v = $0
            sub(/^cn:[[:space:]]*/, "", v)
            sub(/^\{[0-9]+\}/, "", v)
            print v
        }
    ' "$1" | sort -u
}

csv_append() {
    current="$1"
    value="$2"
    if [ -z "$current" ]; then
        printf '%s' "$value"
    else
        printf '%s,%s' "$current" "$value"
    fi
}

normalize_entry_for_import() {
    entry_file="$1"
    schema_name="$2"

    awk -v schema_name="$schema_name" '
        BEGIN {
            print "dn: cn=" schema_name ",cn=schema,cn=config"
            printed_object_class = 0
            printed_cn = 0
        }
        /^dn: / { next }
        /^cn: / {
            if (!printed_cn) {
                print "cn: " schema_name
                printed_cn = 1
            }
            next
        }
        /^objectClass: / {
            print
            printed_object_class = 1
            next
        }
        /^olcObjectIdentifier: / || /^olcAttributeTypes: / || /^olcObjectClasses: / {
            if (!printed_object_class) {
                print "objectClass: olcSchemaConfig"
                printed_object_class = 1
            }
            if (!printed_cn) {
                print "cn: " schema_name
                printed_cn = 1
            }
            print
            next
        }
        END {
            if (!printed_object_class) {
                print "objectClass: olcSchemaConfig"
            }
            if (!printed_cn) {
                print "cn: " schema_name
            }
            print ""
        }
    ' "$entry_file"
}

unfold_ldif "$schema_ldif" > "$schema_unfolded"
unfold_ldif "$current_ldif" > "$current_unfolded"
extract_oids "$current_unfolded" > "$current_oids"
extract_names "$current_unfolded" > "$current_names"

awk -v dir="$tmpdir" -v list="$entry_list" '
    BEGIN { idx = 0; active = 0 }
    /^$/ {
        if (active) {
            close(file)
            print file >> list
            active = 0
        }
        next
    }
    {
        if (!active) {
            idx++
            file = sprintf("%s/entry-%06d.ldif", dir, idx)
            active = 1
        }
        print > file
    }
    END {
        if (active) {
            close(file)
            print file >> list
        }
    }
' "$schema_unfolded"

if [ -n "$output_import_ldif" ]; then
    : > "$output_import_ldif"
fi

schema_source_entries=0
while IFS= read -r entry_file; do
    [ -n "$entry_file" ] || continue
    entry_oids="$tmpdir/$(basename "$entry_file").precount.oids"
    extract_oids "$entry_file" > "$entry_oids"
    entry_definition_total="$(sed '/^$/d' "$entry_oids" | wc -l | tr -d ' ')"
    if [ "$entry_definition_total" -gt 0 ]; then
        schema_source_entries=$((schema_source_entries + 1))
    fi
done < "$entry_list"

schema_entries_total=0
schema_entries_missing=0
schema_entries_present=0
schema_entries_conflict=0
definition_total=0
definition_hits=0
missing_schema_names=""
present_schema_names=""
conflict_schema_names=""
first_schema_name=""

while IFS= read -r entry_file; do
    [ -n "$entry_file" ] || continue

    entry_oids="$tmpdir/$(basename "$entry_file").oids"
    extract_oids "$entry_file" > "$entry_oids"
    entry_definition_total="$(sed '/^$/d' "$entry_oids" | wc -l | tr -d ' ')"
    [ "$entry_definition_total" -gt 0 ] || continue

    schema_entries_total=$((schema_entries_total + 1))
    definition_total=$((definition_total + entry_definition_total))

    schema_name="$(
        awk '
            /^cn: / {
                v = $0
                sub(/^cn:[[:space:]]*/, "", v)
                sub(/^\{[0-9]+\}/, "", v)
                found = 1
                print v
                exit
            }
            /^dn: cn=/ && name == "" {
                v = $0
                sub(/^dn: cn=/, "", v)
                sub(/,cn=schema,cn=config$/, "", v)
                sub(/^\{[0-9]+\}/, "", v)
                name = v
            }
            END {
                if (!found && name != "") {
                    print name
                }
            }
        ' "$entry_file"
    )"
    if [ -z "$schema_name" ]; then
        schema_name="$(basename "$schema_ldif" .ldif)"
    fi
    if [ -z "$first_schema_name" ]; then
        first_schema_name="$schema_name"
    fi

    entry_hits=0
    while IFS= read -r oid; do
        [ -n "$oid" ] || continue
        if grep -Fx "$oid" "$current_oids" >/dev/null 2>&1; then
            entry_hits=$((entry_hits + 1))
        fi
    done < "$entry_oids"
    definition_hits=$((definition_hits + entry_hits))

    name_present=false
    if grep -Fx "$schema_name" "$current_names" >/dev/null 2>&1; then
        name_present=true
    fi

    if [ "$entry_hits" -eq "$entry_definition_total" ]; then
        schema_entries_present=$((schema_entries_present + 1))
        present_schema_names="$(csv_append "$present_schema_names" "$schema_name")"
    elif [ "$name_present" = "true" ] && [ "$schema_source_entries" -gt 1 ]; then
        # Full source dumps often contain built-in schema entries whose content
        # differs slightly between OpenLDAP versions. A same-name target schema
        # must not be re-added; skip it and only import entirely missing entries.
        schema_entries_present=$((schema_entries_present + 1))
        present_schema_names="$(csv_append "$present_schema_names" "$schema_name")"
    elif [ "$entry_hits" -eq 0 ]; then
        schema_entries_missing=$((schema_entries_missing + 1))
        missing_schema_names="$(csv_append "$missing_schema_names" "$schema_name")"
        if [ -n "$output_import_ldif" ]; then
            normalize_entry_for_import "$entry_file" "$schema_name" >> "$output_import_ldif"
        fi
    else
        schema_entries_conflict=$((schema_entries_conflict + 1))
        conflict_schema_names="$(csv_append "$conflict_schema_names" "$schema_name")"
    fi
done < "$entry_list"

if [ "$schema_entries_total" -eq 0 ]; then
    state=invalid
elif [ "$schema_entries_conflict" -gt 0 ]; then
    state=conflict
elif [ "$schema_entries_missing" -gt 0 ]; then
    state=missing
else
    state=present
fi

if [ "$schema_entries_total" -eq 1 ]; then
    schema_name="$first_schema_name"
else
    schema_name=multiple
fi

printf 'schema_name=%s\n' "$schema_name"
printf 'schema_entries_total=%s\n' "$schema_entries_total"
printf 'schema_entries_missing=%s\n' "$schema_entries_missing"
printf 'schema_entries_present=%s\n' "$schema_entries_present"
printf 'schema_entries_conflict=%s\n' "$schema_entries_conflict"
printf 'definition_total=%s\n' "$definition_total"
printf 'definition_hits=%s\n' "$definition_hits"
printf 'missing_schema_names=%s\n' "$missing_schema_names"
printf 'present_schema_names=%s\n' "$present_schema_names"
printf 'conflict_schema_names=%s\n' "$conflict_schema_names"
printf 'state=%s\n' "$state"
