# OpenLDAP YubiKey OTP Overlay Ansible Deploy

[Magyar összefoglaló](README.hu.md)

This repository contains two deliverables:

1. a custom `ykbind` OpenLDAP overlay module for YubiKey OTP validation
2. an Ansible-based Docker deployment stack for OpenLDAP and an optional FreeRADIUS sidecar on Debian-based hosts

The deployment supports three explicit modes:

- `full_import`: build a fresh image, bootstrap a clean `cn=config`, then import configuration and directory data
- `adopt_existing`: rebuild and redeploy the container stack while preserving an existing LDAP database and config state
- `maintenance`: update an already managed deployment in place without rebuilding the image or reloading the database

The OpenLDAP runtime inside the container is non-root. The image creates a dedicated `ldap-runtime` account and runs `slapd` as `11050:11050` by default. Host-side bind mounts are prepared with matching ownership so deploy, restart, migration, and maintenance flows stay consistent.

## Repository Layout

- [ansible/](ansible): playbooks, inventory, role defaults, templates, and files
- [docker/openldap/](docker/openldap): Docker image build files and entrypoint logic
- [docker/freeradius/](docker/freeradius): FreeRADIUS image build files and runtime checks
- [radius/](radius): place existing FreeRADIUS config trees or file overrides here; see [radius/README.md](radius/README.md)
- [schema/](schema): bundled schema sources and LDIF files
- [exports/](exports): place exported LDIF files here before migration; see [exports/README.md](exports/README.md)
- [tls/](tls): place TLS material here before LDAPS deployment; see [tls/README.md](tls/README.md)
- [tools/filter-unsupported-ldif.sh](tools/filter-unsupported-ldif.sh): helper for removing unsupported subtrees or object classes from export LDIFs
- [slapo-ykbind.c](slapo-ykbind.c): overlay source code
- [Makefile](Makefile): overlay build logic

## What The Playbook Does

Running [ansible/playbooks/deploy-openldap.yml](ansible/playbooks/deploy-openldap.yml):

- builds the OpenLDAP image on the control node
- compiles the custom `ykbind` module in a multi-stage Docker build
- exports the image as a tar archive and transfers it to the target host
- prepares target directories for `data`, `config`, `logs`, `runtime`, and `runtime/tls`
- aligns host-side ownership with the dedicated runtime UID/GID
- renders a Docker Compose file and a systemd unit for the managed stack
- optionally builds and deploys a separate FreeRADIUS container wired to the LDAP service
- enables and starts the systemd service so the container also comes back after reboot
- starts the container with root-only bootstrap steps, then runs `slapd` as the dedicated non-root runtime user
- performs mode-specific initialization, adoption, or maintenance actions
- runs smoke checks against the resulting service

## Requirements

Control node:

- Ansible installed
- a working local Docker daemon and `docker` CLI
- SSH access to the target host
- privilege escalation rights on the target host

Target host:

- Debian-based system recommended
- Docker packages available either directly or through a reachable package proxy
- `docker.io` and `docker-compose` packages, unless you manage host packages yourself

Set `ldap_manage_host_packages=false` if the host already has container tooling installed and managed externally.

## Inventory

Local example:

```ini
[openldap]
localhost ansible_connection=local

[openldap:vars]
ansible_become=true
```

Remote example:

```ini
[openldap]
ldap-host ansible_host=ldap-host.example.net ansible_user=debian

[openldap:vars]
ansible_become=true
```

The default inventory is [ansible/inventory/hosts.ini](ansible/inventory/hosts.ini).

## Important Variables

Defaults live in [ansible/roles/openldap_docker/defaults/main.yml](ansible/roles/openldap_docker/defaults/main.yml).

Commonly adjusted values:

```yaml
ldap_base_dn: dc=example,dc=org
ldap_domain: example.org
ldap_organization: Example Organization
ldap_admin_dn: cn=admin,dc=example,dc=org
ldap_admin_password: changeme

ldap_container_name: openldap-ykbind
ldap_image_name: openldap-ykbind:latest
ldap_container_base_image: debian:trixie

ldap_runtime_user: ldap-runtime
ldap_runtime_group: ldap-runtime
ldap_runtime_uid: 11050
ldap_runtime_gid: 11050

ldap_data_dir: /opt/openldap-ykbind/data
ldap_config_dir: /opt/openldap-ykbind/config
ldap_log_dir: /opt/openldap-ykbind/logs
ldap_tls_host_dir: /opt/openldap-ykbind/runtime/tls

ldap_listen_port: 389
ldap_ldaps_port: 636
ldap_container_ldap_port: 1389
ldap_container_ldaps_port: 1636

ldap_http_proxy: ""
ldap_https_proxy: ""
ldap_no_proxy: localhost,127.0.0.1

ldap_deploy_mode: full_import
ldap_maintenance_restart_container: false
ldap_manage_host_packages: true

radius_enabled: false
radius_container_name: freeradius-ldap
radius_image_name: freeradius-ldap:latest
radius_config_dir: /opt/openldap-ykbind/radius/config
radius_auth_port: 1812
radius_acct_port: 1813
radius_ldap_host: openldap
radius_ldap_bind_dn: cn=admin,dc=example,dc=org
radius_ldap_bind_password: changeme
radius_ldap_base_dn: dc=example,dc=org
radius_clients_base_dn: ou=radius_clients,dc=example,dc=org

ldap_mirrormode_enabled: false
ldap_server_id: ""
ldap_replication_peer_server_id: ""
ldap_node_fqdn: ""
ldap_node_ip: ""
ldap_peer_fqdn: ""
ldap_peer_ip: ""
ldap_replication_bind_dn: cn=mirrormode,dc=example,dc=org
ldap_replication_bind_password: ""
ldap_replication_use_ldaps: false
```

Proxy variables are intentionally empty by default. Set them explicitly if your control node needs them for package installation or Docker builds.

## Deploy Modes

`full_import`

- builds and transfers an image
- resets persistent `data` and `config`
- bootstraps a clean `cn=config`
- applies schema, module, overlay, and optional config LDIF updates
- imports the directory tree from LDIF

`adopt_existing`

- builds and transfers an image
- preserves existing `data` and `config`
- starts from the existing bind-mounted LDAP state
- realigns host-side ownership for the dedicated runtime UID/GID
- does not run destructive database reset or full-tree import steps

`maintenance`

- does not rebuild the image
- does not run `docker save` or `docker load`
- does not reset `data` or `config`
- does not run a full LDIF restore
- updates Compose, environment, TLS material, and related runtime settings in place
- can restart the managed service if `ldap_maintenance_restart_container=true`

## Optional LDAP Mirror Mode

Set `ldap_mirrormode_enabled=true` to have the role configure data-database mirroring between two separately deployed guests. The role keeps this vars-driven so the same playbook can be used in standalone or mirrored mode.

What the role reconciles when mirrormode is enabled:

- `olcServerID` on `cn=config`
- `syncprov` overlay on the main LDAP database
- `olcSyncRepl` on the main LDAP database, pointing to the peer guest
- `olcMultiProvider: TRUE` on the main LDAP database
- optional replication ACL and unlimited replication search limits for `ldap_replication_bind_dn`
- `slapadd -w` during `full_import`, plus node-specific `-S <serverID>` when mirrormode is enabled

The role does not replicate `cn=config` itself between nodes. Each guest gets its own runtime config from Ansible, while the main directory database is mirrored over syncrepl.

Standalone behavior:

- with `ldap_mirrormode_enabled=false`, the role removes managed `olcServerID`, `olcSyncRepl`, `olcMultiProvider`, and the managed replication ACL/limits rule so the node can run standalone again

Typical vars for node 1:

```yaml
ldap_mirrormode_enabled: true
ldap_server_id: 101
ldap_replication_peer_server_id: 102
ldap_node_fqdn: t-v-nua.gironet.test.giro.hu
ldap_node_ip: 10.0.1.221
ldap_peer_fqdn: t-m-nua.gironet.test.giro.hu
ldap_peer_ip: 10.0.1.222
ldap_replication_bind_dn: "cn=mirrormode,{{ ldap_base_dn }}"
ldap_replication_bind_password: "<mirror-password>"
ldap_replication_use_ldaps: false
```

Typical vars for node 2:

```yaml
ldap_mirrormode_enabled: true
ldap_server_id: 102
ldap_replication_peer_server_id: 101
ldap_node_fqdn: t-m-nua.gironet.test.giro.hu
ldap_node_ip: 10.0.1.222
ldap_peer_fqdn: t-v-nua.gironet.test.giro.hu
ldap_peer_ip: 10.0.1.221
ldap_replication_bind_dn: "cn=mirrormode,{{ ldap_base_dn }}"
ldap_replication_bind_password: "<mirror-password>"
ldap_replication_use_ldaps: false
```

If both nodes have LDAPS configured and certificates matching the guest FQDNs, switch replication to LDAPS with:

```yaml
ldap_enable_ldaps: true
ldap_replication_use_ldaps: true
```

## Host Paths And Ownership

Default deployment root:

```text
/opt/openldap-ykbind
```

Important target-side directories:

- `images/`: transferred image archive
- `runtime/ldif/`: generated LDIF files
- `runtime/schema/`: schema LDIF files
- `runtime/tls/`: optional TLS files
- `data/`: persistent LDAP database
- `config/`: persistent `cn=config`
- `logs/`: persistent logs

Ownership model:

- writable bind mounts are owned by `ldap_runtime_uid:ldap_runtime_gid`
- default ownership is `11050:11050`
- Ansible prepares and re-aligns `data`, `config`, `logs`, `runtime/ldif`, `runtime/schema`, `runtime/ldif/imports`, `runtime/ldif/config-imports`, and `runtime/tls`
- TLS private keys are written as `0600`; certificates and CA files as `0644`
- inside the container, `slapd` listens on high ports `1389` and optional `1636`; host publishing remains `389` and `636`

## Systemd-Managed Compose Service

The Docker Compose stack is managed by a rendered systemd unit instead of ad hoc `docker-compose up` calls.

- default unit name: `openldap-ykbind-compose.service`
- default unit path: `/etc/systemd/system/openldap-ykbind-compose.service`
- enabled automatically by Ansible
- started automatically after deploy
- started again automatically after reboot

The unit uses security hardening compatible with Compose-driven container management, including:

- `NoNewPrivileges=yes`
- `PrivateTmp=yes`
- `PrivateDevices=yes`
- `ProtectSystem=full`
- `ProtectHome=yes`
- `ProtectKernelTunables=yes`
- `ProtectKernelModules=yes`
- `ProtectControlGroups=yes`
- `ProtectClock=yes`
- `ProtectHostname=yes`
- `RestrictSUIDSGID=yes`
- `LockPersonality=yes`
- `MemoryDenyWriteExecute=yes`
- `RestrictRealtime=yes`
- `SystemCallArchitectures=native`
- `ReadWritePaths=/opt/openldap-ykbind`

Typical host commands:

```bash
systemctl status openldap-ykbind-compose
systemctl restart openldap-ykbind-compose
systemctl stop openldap-ykbind-compose
systemctl start openldap-ykbind-compose
journalctl -u openldap-ykbind-compose -f
```

## Optional RADIUS Container

Set `radius_enabled=true` to deploy a separate FreeRADIUS container in the same Compose stack.

The RADIUS service:

- listens on `1812/udp` and `1813/udp` by default
- connects to the LDAP container over the Compose network
- relays PAP authentication to LDAP bind, so the LDAP side decides whether a given user needs `password` or `password+OTP`
- can use a full existing FreeRADIUS config tree
- can also run with a smaller override model where Ansible deploys only selected files such as:
  - `clients.conf`
  - `mods-available/ldap`
  - `sites-enabled/default`
  - `sites-enabled/inner-tunnel`

Supported config models:

- full tree:
  - set `radius_config_src_dir` to a directory containing an existing FreeRADIUS config tree
  - that tree is copied to the target host and mounted to `/etc/freeradius/3.0`
- partial overrides:
  - set one or more of `radius_radiusd_conf`, `radius_clients_conf`, `radius_mods_available_ldap`, `radius_sites_available_default`, `radius_sites_available_inner_tunnel`
  - or let Ansible render the LDAP module, dynamic client config, and site configs from variables

Default LDAP wiring for the managed RADIUS templates:

- LDAP host: `radius_ldap_host`
- LDAP port: `1389`
- bind DN: `radius_ldap_bind_dn`
- bind password: `radius_ldap_bind_password`
- base DN: `radius_ldap_base_dn`
- user base DN: `radius_ldap_user_base_dn`
- user filter: `radius_ldap_user_filter`
- client base DN: `radius_clients_base_dn`

Managed relay behavior:

- FreeRADIUS does not split `password+OTP`
- PAP credentials are sent to LDAP bind unchanged
- users with `ykbind` / YubiKey policy enabled in LDAP must authenticate with `password+OTP`
- users without that LDAP-side policy authenticate with plain password
- device reply attributes such as `Juniper-Local-User-Name`, `CP-Gaia-User-Role`, `CP-Gaia-SuperUser-Access`, `MBG-Management-Privilege-Level`, `Symbol-Admin-Role`, and `Class` are mapped from LDAP reply attributes

Managed dynamic client behavior:

- define allowed source networks in `radius_dynamic_client_networks`
- client records are looked up under `radius_clients_base_dn`
- the lookup key defaults to `cn=%{Packet-Src-IP-Address}`
- the shared secret defaults to the LDAP attribute `radiusClientSecret`
- the RADIUS shortname defaults to `radiusClientIdentifier`

The generated FreeRADIUS templates are intentionally small. They are meant as a practical baseline, not as a replacement for a mature site-specific config tree.

If you migrate a full existing `radiusd.conf`, do not keep numeric or distro-specific runtime identity directives such as `user = 11060`, `group = 11060`, `user = freerad`, or `group = freerad`. Either remove those lines or set them to `user = radius-runtime` and `group = radius-runtime` so they match the container runtime account.

## Example Runs

Fresh import:

```bash
cd ansible
ansible-playbook playbooks/deploy-openldap.yml \
  -e ldap_deploy_mode=full_import \
  -e ldap_domain=example.org \
  -e ldap_base_dn=dc=example,dc=org \
  -e ldap_admin_dn="cn=admin,dc=example,dc=org" \
  -e ldap_admin_password='<set-admin-password>' \
  -e ldap_ldif_import_file=exports/full-tree.ldif
```

Adopt an existing deployment and move it onto the current non-root runtime model:

```bash
cd ansible
ansible-playbook -i inventory/hosts.ini playbooks/deploy-openldap.yml \
  -e ldap_deploy_mode=adopt_existing \
  -e ldap_config_dir=/opt/openldap-ykbind/config \
  -e ldap_data_dir=/opt/openldap-ykbind/data \
  -e ldap_log_dir=/opt/openldap-ykbind/logs
```

Maintenance update for TLS and runtime settings:

```bash
cd ansible
ansible-playbook -i inventory/hosts.ini playbooks/deploy-openldap.yml \
  -e ldap_deploy_mode=maintenance \
  -e ldap_enable_ldaps=true \
  -e ldap_tls_certificate_src=tls/tls.crt \
  -e ldap_tls_private_key_src=tls/tls.key \
  -e ldap_tls_ca_src=tls/ca.crt \
  -e ldap_maintenance_restart_container=true
```

Enable the optional RADIUS sidecar with a repo-managed LDAP module and site config:

```bash
cd ansible
ansible-playbook -i inventory/hosts.ini playbooks/deploy-openldap.yml \
  -e radius_enabled=true \
  -e radius_ldap_host=openldap \
  -e radius_ldap_bind_dn='cn=admin,dc=example,dc=org' \
  -e radius_ldap_bind_password='<set-admin-password>' \
  -e radius_ldap_base_dn='dc=example,dc=org' \
  -e radius_clients_base_dn='ou=radius_clients,dc=example,dc=org' \
  -e '{"radius_dynamic_client_networks":[{"name":"dynamic-205","ipaddr":"192.168.205.0","netmask":24},{"name":"dynamic-101","ipaddr":"192.168.101.0","netmask":24},{"name":"dynamic","ipaddr":"192.168.235.0","netmask":24,"require_message_authenticator":"true"},{"name":"dynamic-4000","ipaddr":"10.0.1.0","netmask":16,"require_message_authenticator":"no"}]}'
```

Deploy with a full existing FreeRADIUS config tree:

```bash
cd ansible
ansible-playbook -i inventory/hosts.ini playbooks/deploy-openldap.yml \
  -e radius_enabled=true \
  -e radius_config_src_dir=radius/full-config
```

Skip local rebuild when the control node image already exists:

```bash
cd ansible
ansible-playbook -i inventory/hosts.ini playbooks/deploy-openldap.yml \
  -e ldap_skip_local_build=true
```

Skip both local build and local save when the image archive is already present:

```bash
cd ansible
ansible-playbook -i inventory/hosts.ini playbooks/deploy-openldap.yml \
  -e ldap_skip_local_build=true \
  -e ldap_skip_local_save=true
```

## Migration Notes

Recommended migration order:

1. prepare portable `cn=config` modifications only
2. export and import required custom schema LDIFs
3. import the full tree after filtering unsupported parts

Examples:

```bash
ldapsearch -x -LLL -o ldif-wrap=no \
  -D "cn=admin,dc=example,dc=org" \
  -W \
  -H ldap://old-ldap-host:389 \
  -b "dc=example,dc=org" \
  "(objectClass=*)" > exports/full-tree.ldif
```

```bash
ldapsearch -Q -Y EXTERNAL -H ldapi:/// -LLL -o ldif-wrap=no \
  -b "cn=schema,cn=config" \
  "(objectClass=olcSchemaConfig)" > schema/source-schema.reference.ldif
```

```bash
ldapsearch -Q -Y EXTERNAL -H ldapi:/// -LLL -o ldif-wrap=no \
  -b "cn=config" \
  "(objectClass=*)" > exports/source-cn-config.reference.ldif
```

Do not import a full `cn=config` dump blindly. Environment-specific items such as module paths, TLS paths, backend modules, numbering, or legacy backend references should be translated into portable `changetype: modify` LDIF operations instead.

Unsupported branches can be filtered with:

```bash
tools/filter-unsupported-ldif.sh \
  --drop-subtree "ou=dns,dc=example,dc=org" \
  --output exports/full-tree.filtered.ldif \
  exports/full-tree.ldif
```

## Runtime Path Preparation

The Docker image explicitly prepares runtime directories instead of relying on Debian package side effects. This matters because `slapd slapd/no_configuration boolean true` prevents the package from creating all expected state directories.

The image now creates these paths explicitly:

- `/opt/openldap/bootstrap`
- `/opt/openldap/bootstrap/ldif`
- `/opt/openldap/bootstrap/schema`
- `/etc/ldap/tls`
- `/etc/ldap/slapd.d`
- `/run/slapd`
- `/var/lib/ldap`
- `/var/log/slapd`

At container start, the entrypoint verifies and re-prepares the writable runtime paths before dropping privileges to the dedicated `ldap-runtime` account.

The optional FreeRADIUS image also runs non-root. It uses a dedicated `radius-runtime` user with default UID/GID `11060:11060`, and Ansible prepares the host-side `radius/config`, `radius/logs`, and `radius/run` directories accordingly.

The Debian `freeradius` package installs its default config tree under `/etc/freeradius/3.0`, but that tree is normally owned by the package's own `freerad` account with group-only read permissions. The image build now normalizes that packaged config tree so the dedicated `radius-runtime` account can read the baseline files and partial override mounts without falling back to the distro-specific service user. The same normalization is applied to the package-provided snakeoil key path under `/etc/ssl/private` so the packaged EAP module can still pass `freeradius -CX` under the non-root runtime.

## RADIUS Testing

After deploy, useful checks are:

```bash
systemctl status openldap-ykbind-compose
docker exec freeradius-ldap freeradius -CX -d /etc/freeradius/3.0
docker exec freeradius-ldap ldapwhoami -x \
  -D "cn=admin,dc=example,dc=org" \
  -w '<set-admin-password>' \
  -H ldap://openldap:1389
```

Managed dynamic client config sanity check:

```bash
docker exec freeradius-ldap grep -n 'dynamic_clients_ref' /etc/freeradius/3.0/clients.conf
docker exec freeradius-ldap grep -n 'ou=radius_clients,dc=example,dc=org' /etc/freeradius/3.0/clients.conf
```

Relay auth smoke test from inside the container:

```bash
docker exec freeradius-ldap radtest '<uid>' '<password-or-password+otp>' 127.0.0.1:1812 0 testing123
```

LDAP-backed client entries are expected under `ou=radius_clients,<base dn>`. With the managed defaults:

- `cn` is matched against the packet source IP
- `radiusClientSecret` provides the shared secret
- `radiusClientIdentifier` becomes the FreeRADIUS shortname / NAS identifier hint

Typical LDAP checks:

```bash
docker exec openldap-ykbind ldapsearch -x -LLL \
  -D "cn=admin,dc=example,dc=org" \
  -w '<set-admin-password>' \
  -H ldap://127.0.0.1:1389 \
  -b "ou=radius_clients,dc=example,dc=org" \
  "(cn=192.168.205.10)" cn radiusClientIdentifier radiusClientSecret
```

## YubiKey Overlay Usage

The bundled schema and overlay are intended to be added to existing user entries as auxiliary classes. The exact structural object classes used in your directory are environment-specific, so keep those mappings in your own migration LDIFs or provisioning logic rather than in repo-specific examples.

## Sensitive Data Hygiene

Current documentation intentionally avoids:

- exact proxy IP addresses
- environment-specific hostnames or inventory names
- personal filesystem paths
- deployment-area specific object class names

If you need local examples, keep them in untracked operator notes rather than in committed repository files.
