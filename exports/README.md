Ide teheted a deploy során felhasználni kívánt LDIF exportokat.

Tipikus fájlok:

- `full-tree.ldif`
- `bootstrap.ldif`

Példa futtatás:

```bash
cd /home/username/Documents/yubik/ansible
ansible-playbook playbooks/deploy-openldap.yml \
  -e ldap_admin_password='<set-admin-password>' \
  -e ldap_ldif_import_enabled=true \
  -e ldap_ldif_import_file=exports/full-tree.ldif
```
