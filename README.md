# OpenLDAP YubiKey OTP Overlay Ansible Deploy

Ez a repository két dolgot ad egyben:

1. egy saját `ykbind` OpenLDAP overlay modult YubiKey OTP ellenőrzéssel
2. egy teljes, Ansible-alapú, Dockeres deploy megoldást Debian 13 alapú OpenLDAP szerverhez

A cél a "one click" deploy: egyetlen playbook futtatása a control node-on lokálisan felépíti az image-et, tarballként áttölti az LDAP VM-re, elindítja a konténert, betölti a schema/modul/overlay konfigurációt, felépíti az LDAP tree alapját, opcionálisan importál LDIF-et, majd smoke teszteket futtat.

## Mit csinál a playbook

Az [ansible/playbooks/deploy-openldap.yml](/home/username/Documents/yubik/ansible/playbooks/deploy-openldap.yml) futtatása:

- a control node-on létrehozza a Docker build contextet
- Debian 13 alapú image-et buildel a [docker/openldap/Dockerfile](/home/username/Documents/yubik/docker/openldap/Dockerfile) alapján
- a meglévő `slapo-ykbind.c` modult multi-stage buildben lefordítja
- a kész image-et `docker save`-val exportálja
- átmásolja az image tarballt a cél hostra és `docker load`-dal betölti
- létrehozza a target hoston a deploy könyvtárakat
- elindítja a konténert Docker Compose-szal
- inicializálja a slapd adatbázist az [docker/openldap/entrypoint.sh](/home/username/Documents/yubik/docker/openldap/entrypoint.sh) segítségével
- betölti a `yubikey-otp` schema-t
- betölti a `ykbind.so` modult és hozzáadja az overlayt a fő adatbázishoz
- létrehozza a base DN-t és a szükséges OU-kat
- opcionálisan bootstrap LDIF-et és teljes tree exportot importál
- lefuttatja az admin bind és base query smoke teszteket

## Repository felépítés

- [ansible/](/home/username/Documents/yubik/ansible): inventory, playbook, role, defaults, vars, templates, files
- [docker/openldap/](/home/username/Documents/yubik/docker/openldap): a konténerhez használt `Dockerfile` és `entrypoint.sh`
- [schema/](/home/username/Documents/yubik/schema): a saját schema LDIF és schema forrás
- [examples/](/home/username/Documents/yubik/examples): kézi LDAP példák és korábbi LDIF minták
- [slapo-ykbind.c](/home/username/Documents/yubik/slapo-ykbind.c): a saját overlay forrása
- [Makefile](/home/username/Documents/yubik/Makefile): a modul build logikája

## Előfeltételek

Control node:

- Ansible telepítve
- működő lokális Docker daemon és `docker` CLI
- SSH elérés a target hostra
- jog a target hoston `become` használatára

Target host:

- Debian-alapú rendszer ajánlott
- internet vagy működő APT proxy
- Debian 12 vagy Debian 13 disztribúciós `docker.io` és `docker-compose` csomagok

Alapértelmezésben a role felteszi a következő host csomagokat Debianon:

- `docker.io`
- `docker-compose`

Megjegyzés: a deploy nem igényli a hivatalos Docker upstream repository használatát, a Debian saját csomagjaira épít.

Ha ezt nem szeretnéd, állítsd `false`-ra a `ldap_manage_host_packages` változót.

## Inventory kitöltése

Minta inventory:

```ini
[openldap]
localhost ansible_connection=local

[openldap:vars]
ansible_become=true
```

Távoli host például:

```ini
[openldap]
ldap-host ansible_host=ldap-host.example.net ansible_user=debian

[openldap:vars]
ansible_become=true
```

Az alap inventory itt van:

- [ansible/inventory/hosts.ini](/home/username/Documents/yubik/ansible/inventory/hosts.ini)

## Fontos változók

Az alapértelmezések itt vannak:

- [ansible/roles/openldap_docker/defaults/main.yml](/home/username/Documents/yubik/ansible/roles/openldap_docker/defaults/main.yml)

Legalább ezeket érdemes átnézni:

```yaml
ldap_base_dn: dc=example,dc=org
ldap_domain: example.org
ldap_organization: Example Organization
ldap_admin_dn: cn=admin,dc=example,dc=org
ldap_admin_password: changeme

ldap_container_name: openldap-ykbind
ldap_image_name: openldap-ykbind:latest
ldap_local_artifact_root: /tmp/openldap-ykbind-artifacts

ldap_http_proxy: http://proxy.example.net:3128
ldap_https_proxy: http://proxy.example.net:3128
ldap_no_proxy: localhost,127.0.0.1

ldap_listen_port: 389
ldap_ldaps_port: 636
ldap_enable_ldaps: false

ldap_ldif_import_enabled: false
ldap_ldif_import_file: ""
ldap_bootstrap_ldif_file: ""

ldap_module_build_enabled: true

ldap_data_dir: /opt/openldap-ykbind/data
ldap_config_dir: /opt/openldap-ykbind/config
ldap_log_dir: /opt/openldap-ykbind/logs
```

További fontos, deploy közben gyakran használt változók:

- `ldap_base_ous`
- `ldap_ports`
- `ldap_force_password_reset`
- `ldap_force_full_import`
- `ldap_tls_certificate_src`
- `ldap_tls_private_key_src`
- `ldap_tls_ca_src`
- `ldap_smoke_test_user_dn`

Felülírási lehetőségek:

- `ansible/group_vars/all.yml`
- `ansible/host_vars/<host>.yml`
- `ansible-playbook -e key=value`

## One-click deploy futtatás

Lokális inventoryval:

```bash
cd /home/username/Documents/yubik/ansible
ansible-playbook playbooks/deploy-openldap.yml
```

Példa saját alap DN-nel és admin jelszóval:

```bash
cd /home/username/Documents/yubik/ansible
ansible-playbook playbooks/deploy-openldap.yml \
  -e ldap_domain=example.org \
  -e ldap_base_dn=dc=example,dc=org \
  -e ldap_organization="Example Org" \
  -e ldap_admin_dn="cn=admin,dc=example,dc=org" \
  -e ldap_admin_password='<set-admin-password>'
```

Példa távoli hostra:

```bash
cd /home/username/Documents/yubik/ansible
ansible-playbook -i inventory/hosts.ini playbooks/deploy-openldap.yml
```

## A deploy által létrehozott target oldali könyvtárak

Alapértelmezett root:

```text
/opt/openldap-ykbind
```

Fontos alkönyvtárak a target hoston:

- `images/`: a control node-ról átmásolt image tarball
- `runtime/ldif/`: generált LDIF-ek
- `runtime/schema/`: schema LDIF-ek
- `runtime/tls/`: opcionális TLS fájlok
- `data/`: LDAP adat perzisztencia
- `config/`: `cn=config` perzisztencia
- `logs/`: log perzisztencia

Lokális control node build artifactok alapértelmezett helye:

```text
/tmp/openldap-ykbind-artifacts
```

## Full tree export meglévő LDAP-ból

Ez a rész a teljes adatfára vonatkozik, nem a `cn=config` exportjára.

Példa export parancs meglévő LDAP szerverről:

```bash
ldapsearch -x -LLL -o ldif-wrap=no \
  -D "cn=admin,dc=example,dc=org" \
  -W \
  -H ldap://OLD-LDAP-HOST:389 \
  -b "dc=example,dc=org" \
  "(objectClass=*)" > /home/username/Documents/yubik/exports/full-tree.ldif
```

Ha bootstrap LDIF-et is akarsz használni, például külön OU-khoz vagy service entrykhez:

```bash
ldapsearch -x -LLL -o ldif-wrap=no \
  -D "cn=admin,dc=example,dc=org" \
  -W \
  -H ldap://OLD-LDAP-HOST:389 \
  -b "ou=People,dc=example,dc=org" \
  "(objectClass=*)" > /home/username/Documents/yubik/exports/bootstrap.ldif
```

Az exportált LDIF-et teheted:

- a repositoryban például `exports/full-tree.ldif` vagy `exports/bootstrap.ldif` alá
- bármely abszolút elérési útra a control node-on

Az Ansible role a relatív útvonalakat a repository gyökeréhez viszonyítva oldja fel.

## Full tree import futtatása

Teljes restore jellegű import:

```bash
cd /home/username/Documents/yubik/ansible
ansible-playbook playbooks/deploy-openldap.yml \
  -e ldap_admin_password='<set-admin-password>' \
  -e ldap_ldif_import_enabled=true \
  -e ldap_ldif_import_file=exports/full-tree.ldif
```

Bootstrap LDIF import:

```bash
cd /home/username/Documents/yubik/ansible
ansible-playbook playbooks/deploy-openldap.yml \
  -e ldap_admin_password='<set-admin-password>' \
  -e ldap_bootstrap_ldif_file=exports/bootstrap.ldif
```

Fontos restore megjegyzések:

- a full restore alapból csak üres tree-re ajánlott
- ha a base DN alatt már vannak child entry-k, a playbook megáll
- ezt csak akkor írd felül, ha biztosan restore-t akarsz:

```bash
cd /home/username/Documents/yubik/ansible
ansible-playbook playbooks/deploy-openldap.yml \
  -e ldap_admin_password='<set-admin-password>' \
  -e ldap_ldif_import_enabled=true \
  -e ldap_ldif_import_file=exports/full-tree.ldif \
  -e ldap_force_full_import=true
```

Idempotencia megjegyzés:

- alapértelmezésben a bootstrap marker itt jön létre: `/opt/openldap-ykbind/data/.bootstrap-import-done`
- alapértelmezésben a full import marker itt jön létre: `/opt/openldap-ykbind/data/.full-import-done`
- ha újra akarod futtatni az importot, a marker törlése mellett a perzisztens LDAP adatot is tisztázni kell

## TLS / LDAPS

Az image nyitja a `389` és `636` portot. A playbook támogatja az LDAPS bekapcsolását, de a tanúsítványkezelést szándékosan egyszerű, bővíthető formában hagyja meg.

Legkényelmesebb használat:

```bash
cd /home/username/Documents/yubik/ansible
ansible-playbook playbooks/deploy-openldap.yml \
  -e ldap_admin_password='<set-admin-password>' \
  -e ldap_enable_ldaps=true \
  -e ldap_tls_certificate_src=tls/tls.crt \
  -e ldap_tls_private_key_src=tls/tls.key \
  -e ldap_tls_ca_src=tls/ca.crt
```

Ekkor a role:

- bemásolja a fájlokat a target host `runtime/tls/` könyvtárába
- mountolja őket a konténerbe
- beírja az LDAP TLS útvonalakat a `cn=config` alá

Használt változók:

- `ldap_enable_ldaps`
- `ldap_ldaps_port`
- `ldap_tls_certificate_src`
- `ldap_tls_private_key_src`
- `ldap_tls_ca_src`
- `ldap_tls_cert_file`
- `ldap_tls_key_file`
- `ldap_tls_ca_file`

## Modul build és deploy működés

Az image build nem csak bemásolja a modult, hanem ténylegesen le is fordítja.

Fő pontok:

- Debian 13 alapú multi-stage Docker build
- `apt-get build-dep openldap`
- `apt-get source openldap`
- a repositoryban lévő [Makefile](/home/username/Documents/yubik/Makefile) fut
- a lefordított `ykbind.so` a runtime image `/usr/lib/ldap/ykbind.so` helyére kerül
- a kész image a control node-on tarballként exportálódik
- a tarball a cél LDAP hostra kerül és ott `docker load` importálja
- a playbook utána betölti a modult a `cn=module{0},cn=config` alá
- végül felveszi az overlayt a fő adatbázisra

Ha kézzel akarod debugolni a buildet, a legegyszerűbb a control node-on az Ansible által kirakott build contextet használni:

```bash
docker build -t openldap-ykbind:debug /tmp/openldap-ykbind-artifacts/context
```

Deploy oldalon a modul buildje kikapcsolható:

```bash
cd /home/username/Documents/yubik/ansible
ansible-playbook playbooks/deploy-openldap.yml \
  -e ldap_module_build_enabled=false
```

## LDAP inicializálás és konfiguráció

A deploy során a role:

- létrehozza a base DN entryt, ha még nem létezik
- létrehozza a `ldap_base_ous` listában szereplő OU-kat
- importálja a saját schema LDIF-et
- opcionálisan további schema LDIF-eket is importál a `ldap_additional_schema_ldifs` listából
- szükség esetén beállítja a `olcSuffix`, `olcRootDN`, `olcRootPW` értékeket
- opcionálisan TLS fájlútvonalakat konfigurál a `cn=config` alatt

Megjegyzés: a `slapd` Debianos inicializálása a `ldap_domain` alapján hozza létre az első suffixet. A legtisztább működéshez a `ldap_domain` és `ldap_base_dn` legyen összhangban.

## Ellenőrzés deploy után

Az Ansible smoke tesztek automatikusan lefutnak, de manuálisan ezek a leghasznosabb parancsok:

Konténer állapot:

```bash
docker-compose -f /opt/openldap-ykbind/docker-compose.yml ps
```

Admin bind:

```bash
docker exec openldap-ykbind ldapwhoami \
  -x -D "cn=admin,dc=example,dc=org" \
  -w '<set-admin-password>' \
  -H ldap://127.0.0.1
```

Base query:

```bash
docker exec openldap-ykbind ldapsearch \
  -x -D "cn=admin,dc=example,dc=org" \
  -w '<set-admin-password>' \
  -H ldap://127.0.0.1 \
  -LLL -b "dc=example,dc=org" -s base "(objectClass=*)" dn
```

Schema és overlay jelenlét:

```bash
docker exec openldap-ykbind ldapsearch \
  -Q -Y EXTERNAL -H ldapi:/// \
  -LLL -b cn=schema,cn=config "(cn=yubikey-otp)" dn

docker exec openldap-ykbind ldapsearch \
  -Q -Y EXTERNAL -H ldapi:/// \
  -LLL -b cn=config "(olcOverlay=ykbind)" dn
```

## Az overlay működése röviden

A kliens simple bind credentialt küld:

```text
<password><otp>
```

Az overlay:

- levágja a végéről a 44 karakteres OTP-t
- ellenőrzi a modhex és AES ticket adatot
- ellenőrzi a replay állapotot
- siker esetén a maradék statikus jelszót adja át a normál OpenLDAP jelszóellenőrzésnek
- sikeres bind után frissíti a replay mezőket

Fő schema attribútumok:

- `yubiKeyEnabled`
- `yubiKeyPublicId`
- `yubiKeyPrivateUid`
- `yubiKeyAesKey`
- `yubiKeyLastUseCtr`
- `yubiKeyLastSessionCtr`
- `yubiKeyLastTimestamp`
- `yubiKeyLastCounter`

Kompatibilitási aliasok:

- `YKkeyID`
- `YKaesKey`
- `YKkeyCounter`
- `YKsessionTimestamp`

## Kézi LDAP példák

Megmaradt kézi referenciafájlok:

- [examples/module-load.ldif](/home/username/Documents/yubik/examples/module-load.ldif)
- [examples/module-path-and-load.ldif](/home/username/Documents/yubik/examples/module-path-and-load.ldif)
- [examples/overlay-config.ldif](/home/username/Documents/yubik/examples/overlay-config.ldif)
- [examples/acl-yubikey-secrets.ldif](/home/username/Documents/yubik/examples/acl-yubikey-secrets.ldif)

Ezek főleg debughoz és kézi finomhangoláshoz hasznosak; a normál deploy út az Ansible playbook.

## Ismert korlátok

- a full tree import restore jellegű, nem általános merge mechanizmus
- a `olcRootPW` idempotens cseréje csak akkor fut le, ha a suffix/rootDN eltér vagy a `ldap_force_password_reset=true`
- a `ldap_domain` és `ldap_base_dn` eltérése támogatott, de nem ez a Debian `slapd` inicializálás natív útja
- TLS esetén a tanúsítványanyag meglétét a role ellenőrzi, de a PKI életciklus-kezelést nem automatizálja
