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
  -e '{"radius_dynamic_client_networks":[{"name":"dynamic-205","ipaddr":"192.168.205.0","netmask":24},{"name":"dynamic-101","ipaddr":"192.168.101.0","netmask":24},{"name":"dynamic","ipaddr":"192.168.235.0","netmask":24,"require_message_authenticator":"true"},{"name":"dynamic-4000","ipaddr":"10.0.1.0","netmask":16,"require_message_authenticator":"no"}]}'
```
