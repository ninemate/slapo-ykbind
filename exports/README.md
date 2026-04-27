Place LDIF exports used during migration in this directory.

Typical files:

- `full-tree.ldif`
- `full-tree.filtered.ldif`
- `source-cn-config.reference.ldif`
- `migration-config.ldif`

Full import example:

```bash
cd ansible
ansible-playbook playbooks/deploy-openldap.yml \
  -e ldap_deploy_mode=full_import \
  -e ldap_admin_password='<set-admin-password>' \
  -e ldap_ldif_import_file=exports/full-tree.ldif
```

The role filters the `ou=dns,{{ ldap_base_dn }}` subtree by default because the target image does not include the legacy DNS backend. Manual filtering example:

```bash
tools/filter-unsupported-ldif.sh \
  --drop-subtree "ou=dns,dc=example,dc=org" \
  --output exports/full-tree.filtered.ldif \
  exports/full-tree.ldif
```
