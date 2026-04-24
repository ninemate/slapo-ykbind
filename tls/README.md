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

If you do not pass the `ldap_tls_*_src` variables explicitly, the playbook will also try to auto-discover files in this directory based on the configured target filenames:

- the basename of `ldap_tls_cert_file` for the certificate
- the basename of `ldap_tls_key_file` for the private key
- the basename of `ldap_tls_ca_file` for the optional CA file

Example for a PEM CA filename:

```yaml
ldap_enable_ldaps: true
ldap_tls_cert_file: /etc/ldap/tls/tls.crt
ldap_tls_key_file: /etc/ldap/tls/tls.key
ldap_tls_ca_file: /etc/ldap/tls/ca.pem
```

With that setting, the playbook will look for:

- `tls/tls.crt`
- `tls/tls.key`
- `tls/ca.pem`
