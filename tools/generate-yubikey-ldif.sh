#!/usr/bin/env bash
set -euo pipefail

if [[ $# -ne 1 ]]; then
  echo "Usage: $0 users.csv" >&2
  exit 1
fi

csv_file="$1"

if [[ ! -f "$csv_file" ]]; then
  echo "Input CSV not found: $csv_file" >&2
  exit 1
fi

trim() {
  local value="$1"
  value="${value#"${value%%[![:space:]]*}"}"
  value="${value%"${value##*[![:space:]]}"}"
  printf '%s' "$value"
}

is_modhex_public_id() {
  [[ "$1" =~ ^[cbdefghijklnrtuv]{12}$ ]]
}

is_hex_bytes() {
  local value="$1"
  local expected_chars="$2"
  [[ "$value" =~ ^[0-9A-Fa-f]+$ ]] && [[ ${#value} -eq $expected_chars ]]
}

line_no=0
while IFS=';' read -r raw_dn raw_public_id raw_private_uid raw_aes_key raw_enabled extra; do
  line_no=$((line_no + 1))

  if [[ -n "${extra:-}" ]]; then
    echo "Line $line_no: too many CSV columns" >&2
    exit 1
  fi

  if [[ -z "${raw_dn}${raw_public_id}${raw_private_uid}${raw_aes_key}${raw_enabled}" ]]; then
    continue
  fi

  if [[ "${raw_dn:0:1}" == "#" ]]; then
    continue
  fi

  if [[ "$(trim "$raw_dn")" == "dn" ]]; then
    continue
  fi

  dn="$(trim "$raw_dn")"
  public_id="$(trim "$raw_public_id")"
  private_uid="$(trim "$raw_private_uid")"
  aes_key="$(trim "$raw_aes_key")"
  enabled="$(tr '[:lower:]' '[:upper:]' <<<"$(trim "$raw_enabled")")"

  if [[ -z "$dn" || -z "$public_id" || -z "$private_uid" || -z "$aes_key" || -z "$enabled" ]]; then
    echo "Line $line_no: every row must contain dn;public_id;private_uid_hex;aes_key_hex;enabled" >&2
    exit 1
  fi

  if ! is_modhex_public_id "$public_id"; then
    echo "Line $line_no: invalid public_id '$public_id' (must be 12-char modhex)" >&2
    exit 1
  fi

  if ! is_hex_bytes "$private_uid" 12; then
    echo "Line $line_no: invalid private_uid '$private_uid' (must be 12 hex chars / 6 bytes)" >&2
    exit 1
  fi

  if ! is_hex_bytes "$aes_key" 32; then
    echo "Line $line_no: invalid aes_key '$aes_key' (must be 32 hex chars / 16 bytes)" >&2
    exit 1
  fi

  if [[ "$enabled" != "TRUE" && "$enabled" != "FALSE" ]]; then
    echo "Line $line_no: enabled must be TRUE or FALSE" >&2
    exit 1
  fi

  cat <<EOF
dn: $dn
changetype: modify
add: objectClass
objectClass: yubiKeyTokenAux
-
replace: yubiKeyEnabled
yubiKeyEnabled: $enabled
-
replace: yubiKeyPublicId
yubiKeyPublicId: $public_id
-
replace: yubiKeyPrivateUid
yubiKeyPrivateUid: ${private_uid,,}
-
replace: yubiKeyAesKey
yubiKeyAesKey: ${aes_key,,}
-
replace: yubiKeyLastUseCtr
yubiKeyLastUseCtr: 0
-
replace: yubiKeyLastSessionCtr
yubiKeyLastSessionCtr: 0
-
replace: yubiKeyLastTimestamp
yubiKeyLastTimestamp: 0
-
replace: yubiKeyLastCounter
yubiKeyLastCounter: 000000
-
replace: YKsessionTimestamp
YKsessionTimestamp: 000000

EOF
done <"$csv_file"
