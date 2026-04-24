Place LDAPS certificate files in this directory.

Recommended filenames:

- `tls.crt`
- `tls.key`
- `ca.crt`

Example run:

```bash
cd ansible
ansible-playbook playbooks/deploy-openldap.yml \
  -e ldap_admin_password='<set-admin-password>' \
  -e ldap_enable_ldaps=true \
  -e ldap_tls_certificate_src=tls/tls.crt \
  -e ldap_tls_private_key_src=tls/tls.key \
  -e ldap_tls_ca_src=tls/ca.crt
```
