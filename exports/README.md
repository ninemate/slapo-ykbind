Ide teheted a deploy során felhasználni kívánt LDIF exportokat.

Tipikus fájlok:

- `full-tree.ldif`
- `full-tree.filtered.ldif`
- `source-cn-config.reference.ldif`
- `migration-config.ldif`

Full import példa:

```bash
cd /home/username/Documents/yubik/ansible
ansible-playbook playbooks/deploy-openldap.yml \
  -e ldap_deploy_mode=full_import \
  -e ldap_admin_password='<set-admin-password>' \
  -e ldap_ldif_import_file=exports/full-tree.ldif
```

A role alapból szűri az `ou=dns,{{ ldap_base_dn }}` subtreet, mert a cél image nem tartalmazza a régi DNS backend támogatást. Kézi szűréshez:

```bash
cd /home/username/Documents/yubik
tools/filter-unsupported-ldif.sh \
  --drop-subtree "ou=dns,dc=example,dc=org" \
  --output exports/full-tree.filtered.ldif \
  exports/full-tree.ldif
```
