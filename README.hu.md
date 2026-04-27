# OpenLDAP YubiKey OTP Overlay Ansible Deploy

[English README](README.md)

Ez egy rövid magyar összefoglaló. Az elsődleges, karbantartott dokumentáció az angol [README.md](README.md).

Röviden:

- a repo egy `ykbind` OpenLDAP overlay modult és egy Ansible-alapú, Dockeres deploy stacket tartalmaz
- a konténeren belüli `slapd` nem rootként fut, hanem dedikált `ldap-runtime` userrel
- a host oldali bind mountok ownershipjét az Ansible ehhez a runtime UID/GID-hez igazítja
- a deploy három módban használható: `full_import`, `adopt_existing`, `maintenance`
- a Docker Compose stack-et a hoston systemd service kezeli, ezért reboot után automatikusan visszaindul

Gyors hivatkozások:

- export LDIF fájlok: [exports/README.md](exports/README.md)
- TLS fájlok: [tls/README.md](tls/README.md)
- playbook: [ansible/playbooks/deploy-openldap.yml](ansible/playbooks/deploy-openldap.yml)
- role defaultok: [ansible/roles/openldap_docker/defaults/main.yml](ansible/roles/openldap_docker/defaults/main.yml)

Jellemző futtatások:

```bash
cd ansible
ansible-playbook playbooks/deploy-openldap.yml \
  -e ldap_deploy_mode=full_import \
  -e ldap_admin_password='<set-admin-password>' \
  -e ldap_ldif_import_file=exports/full-tree.ldif
```

```bash
cd ansible
ansible-playbook -i inventory/hosts.ini playbooks/deploy-openldap.yml \
  -e ldap_deploy_mode=adopt_existing
```

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

Ha részletesebb leírás kell a módokról, ownershipről, systemd service-ről vagy migrációról, az angol README a mérvadó.
