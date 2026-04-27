#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
OUTPUT_DIR="${1:-$ROOT_DIR/release-bundle}"
LDAP_IMAGE_NAME="${LDAP_IMAGE_NAME:-openldap-ykbind:latest}"
RADIUS_IMAGE_NAME="${RADIUS_IMAGE_NAME:-freeradius-ldap:latest}"

LDAP_IMAGE_ARCHIVE_NAME="${LDAP_IMAGE_NAME//[:\/]/_}.tar"
RADIUS_IMAGE_ARCHIVE_NAME="${RADIUS_IMAGE_NAME//[:\/]/_}.tar"

REPO_OUT_DIR="$OUTPUT_DIR/repo"
IMAGES_OUT_DIR="$OUTPUT_DIR/images"

echo "Preparing release bundle under: $OUTPUT_DIR"
rm -rf "$OUTPUT_DIR"
mkdir -p "$REPO_OUT_DIR" "$IMAGES_OUT_DIR"

echo "Verifying local images exist..."
docker image inspect "$LDAP_IMAGE_NAME" >/dev/null
docker image inspect "$RADIUS_IMAGE_NAME" >/dev/null

echo "Copying repository contents..."
rsync -a \
  --exclude '.git' \
  --exclude 'release-bundle' \
  --exclude '.venv' \
  --exclude '__pycache__' \
  --exclude '*.pyc' \
  "$ROOT_DIR/" "$REPO_OUT_DIR/"

echo "Exporting OpenLDAP image archive..."
docker save -o "$IMAGES_OUT_DIR/$LDAP_IMAGE_ARCHIVE_NAME" "$LDAP_IMAGE_NAME"

echo "Exporting FreeRADIUS image archive..."
docker save -o "$IMAGES_OUT_DIR/$RADIUS_IMAGE_ARCHIVE_NAME" "$RADIUS_IMAGE_NAME"

cat >"$OUTPUT_DIR/DEPLOY.txt" <<EOF
Release bundle created: $(date -u +"%Y-%m-%dT%H:%M:%SZ")

Included images:
- $LDAP_IMAGE_NAME -> images/$LDAP_IMAGE_ARCHIVE_NAME
- $RADIUS_IMAGE_NAME -> images/$RADIUS_IMAGE_ARCHIVE_NAME

Typical offline deploy:

  cd repo/ansible
  ansible-playbook -i inventory/hosts.ini playbooks/deploy-openldap.yml \\
    -e @../vars/<target-vars>.yml \\
    -e ldap_skip_local_build=true \\
    -e ldap_skip_local_save=true \\
    -e radius_skip_local_build=true \\
    -e radius_skip_local_save=true \\
    -e ldap_local_image_archive=\$PWD/../images/$LDAP_IMAGE_ARCHIVE_NAME \\
    -e radius_local_image_archive=\$PWD/../images/$RADIUS_IMAGE_ARCHIVE_NAME \\
    -Kk
EOF

echo "Release bundle ready."
echo "Repository: $REPO_OUT_DIR"
echo "Images:"
echo "  $IMAGES_OUT_DIR/$LDAP_IMAGE_ARCHIVE_NAME"
echo "  $IMAGES_OUT_DIR/$RADIUS_IMAGE_ARCHIVE_NAME"
