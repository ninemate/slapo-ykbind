# FreeRADIUS Config Inputs

Use this directory for FreeRADIUS configuration that should be deployed next to the LDAP stack.

Two supported models:

1. Full existing config tree
   - point `radius_config_src_dir` at a directory containing a full FreeRADIUS config tree
   - that directory will be copied to the target host and mounted to `/etc/freeradius/3.0`

2. Partial overrides
   - provide individual files such as:
     - `radiusd.conf`
     - `clients.conf`
     - `mods-available/ldap`
     - `sites-enabled/default`
     - `sites-enabled/inner-tunnel`
   - or let Ansible render the LDAP module, dynamic client config, and site configs from variables

## Per-NAS YubiKey Bypass

NAS devices with a short password field limit (e.g. 20 characters) cannot append a 44-character YubiKey OTP. The `yubiKeyNasBypass` attribute marks a NAS client entry as exempt from YubiKey validation.

### How it works

1. The `freeradius-site-default.j2` and `freeradius-site-inner-tunnel.j2` templates inject a conditional check in the `authorize` phase.
2. After the LDAP user search (`ldap` module), if `User-Password` is present, FreeRADIUS performs an inline LDAP lookup:
   ```
   "%{ldap:ldap://<ldap-host>:<port>/ou=radius_clients,<base>?yubiKeyNasBypass?sub?(|(cn=%{NAS-Identifier})(cn=%{NAS-IP-Address}))}"
   ```
3. If the lookup returns `"TRUE"`, `Auth-Type` is set to `PAP` instead of `LDAP`.
4. The `pap` module verifies the plaintext `User-Password` against the `userPassword` hash (stored as `control:Password-With-Header` by the ldap module's `update` section). No LDAP bind occurs, so the `ykbind` overlay never intercepts the request.

### Configuration requirements

- The NAS client entry must exist under `ou=radius_clients,<base_dn>` with `cn` equal to the `NAS-IP-Address` or `NAS-Identifier` sent by the device.
- The entry must have the `ykNasBypassAux` auxiliary class and `yubiKeyNasBypass: TRUE`.
- The `radius_yubikey_nas_bypass_enabled` default variable controls whether the bypass check is rendered. Set to `false` to disable (reverts to original `Auth-Type := LDAP` behavior).

### Adding a NAS client to the bypass list

Example LDIF — add to each NAS entry that needs the bypass:

```ldif
dn: cn=10.0.0.1,ou=radius_clients,dc=example,dc=org
changetype: modify
add: objectClass
objectClass: ykNasBypassAux
-
add: yubiKeyNasBypass
yubiKeyNasBypass: TRUE
```

You only add this to NAS entries that should skip YubiKey. Entries without the attribute are unaffected.

### Updating an existing deployment

Run a maintenance deploy to pick up the template changes:

```bash
cd ansible
ansible-playbook -i inventory/hosts.ini playbooks/deploy-openldap.yml \
  -e ldap_deploy_mode=maintenance \
  -e ldap_maintenance_restart_container=true
```

This applies the updated schema and FreeRADIUS config without rebuilding the Docker image or resetting data.

### Full deployment

No special steps. The templates and schema are part of the standard deploy:

```bash
cd ansible
ansible-playbook playbooks/deploy-openldap.yml \
  -e ldap_deploy_mode=full_import \
  -e radius_enabled=true \
  -e ldap_ldif_import_file=exports/full-tree.ldif \
  -e @../vars/<your-vars>.yml
```

Typical playbook usage with a full existing config tree:

```bash
cd ansible
ansible-playbook playbooks/deploy-openldap.yml \
  -e radius_enabled=true \
  -e radius_config_src_dir=radius/full-config
```

Typical playbook usage with existing `clients.conf` and repo-managed LDAP auth templates:

```bash
cd ansible
ansible-playbook playbooks/deploy-openldap.yml \
  -e radius_enabled=true \
  -e radius_clients_conf=radius/clients.conf \
  -e radius_ldap_bind_dn='cn=admin,dc=example,dc=org' \
  -e radius_ldap_bind_password='<set-admin-password>'
```

Typical playbook usage with fully repo-managed LDAP relay auth and LDAP-backed dynamic clients:

```bash
cd ansible
ansible-playbook playbooks/deploy-openldap.yml \
  -e radius_enabled=true \
  -e radius_ldap_host=openldap \
  -e radius_ldap_bind_dn='cn=admin,dc=example,dc=org' \
  -e radius_ldap_bind_password='<set-admin-password>' \
  -e radius_ldap_base_dn='dc=example,dc=org' \
  -e radius_clients_base_dn='ou=radius_clients,dc=example,dc=org' \
  -e '{"radius_dynamic_client_networks":[{"name":"dynamic-a","ipaddr":"192.0.2.0","netmask":24},{"name":"dynamic-b","ipaddr":"198.51.100.0","netmask":24},{"name":"dynamic-ma","ipaddr":"203.0.113.0","netmask":24,"require_message_authenticator":"true"},{"name":"dynamic-wide","ipaddr":"198.18.0.0","netmask":15,"require_message_authenticator":"no"}]}'
```

Offline promotion note:

- the FreeRADIUS image can be promoted the same way as OpenLDAP via `docker save` tar archives
- see [tools/create-release-bundle.sh](../tools/create-release-bundle.sh) and the offline bundle section in [README.md](../README.md)
