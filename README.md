# OpenLDAP YubiKey OTP Overlay Ansible Deploy

Ez a repository két dolgot ad egyben:

1. egy saját `ykbind` OpenLDAP overlay modult YubiKey OTP ellenőrzéssel
2. egy teljes, Ansible-alapú, Dockeres deploy megoldást Debian 13 alapú OpenLDAP szerverhez

A cél a "one click" deploy három tiszta móddal:

- `full_import`: teljesen szűz célra épít új `cn=config`-et, majd explicit config/schema/data importtal tölti fel az LDAP-ot
- `adopt_existing`: meglévő `cn=config` és adatkönyvtár bind mounttal indul, és nem próbálja átírni a meglévő LDAP állapotot
- `maintenance`: meglévő, ezzel a role-lal kezelt konténeres LDAP stacket módosít in-place image rebuild és DB újratöltés nélkül

Az OpenLDAP folyamat a konténerben nem rootként fut: a role saját, paraméterezhető `ldap-runtime` user/group accountot épít az image-be, és alapból `11050:11050` uid/gid alatt indítja a `slapd`-t. A host oldali bind mountok ownershipjét ehhez igazítja.

Egyetlen playbook futtatása a control node-on lokálisan felépíti az image-et, tarballként áttölti az LDAP VM-re, elindítja a konténert, majd a választott módnak megfelelően teljes importot végez, átveszi a meglévő állapotot, vagy biztonságos maintenance frissítést hajt végre.

## Mit csinál a playbook

Az [ansible/playbooks/deploy-openldap.yml](/home/username/Documents/yubik/ansible/playbooks/deploy-openldap.yml) futtatása:

- a control node-on létrehozza a Docker build contextet
- Debian 13 alapú image-et buildel a [docker/openldap/Dockerfile](/home/username/Documents/yubik/docker/openldap/Dockerfile) alapján
- a meglévő `slapo-ykbind.c` modult multi-stage buildben lefordítja
- a kész image-et `docker save`-val exportálja
- átmásolja az image tarballt a cél hostra és `docker load`-dal betölti
- létrehozza a target hoston a deploy könyvtárakat
- a bind mountolt `data/`, `config/`, `logs/`, `runtime/` és `runtime/tls/` ownershipjét a dedikált runtime uid/gid értékre állítja
- elindítja a konténert Docker Compose-szal
- inicializálja a slapd konfigurációt és a választott init módot az [docker/openldap/entrypoint.sh](/home/username/Documents/yubik/docker/openldap/entrypoint.sh) segítségével
- a konténeren belül rootként csak az előkészítő bootstrap és jogosultság-javítás fut, a `slapd` folyamat maga dedikált non-root userrel indul
- `full_import` módban szándékosan újra létrehozza a perzisztens `data/` és `config/` könyvtárakat
- `full_import` módban minimális, szűz `cn=config`-et bootstrapol
- `full_import` módban opcionális hordozható `cn=config` módosító LDIF-eket alkalmaz
- `full_import` módban betölti a `yubikey-otp` schema-t, az extra schema LDIF-eket, a `ykbind.so` modult és az overlayt
- `full_import` módban opcionálisan kiszűri a nem támogatott LDIF részeket, majd offline `slapadd`-dal importálja a teljes tree-t
- `adopt_existing` módban a már meglévő bind mountolt `cn=config` és adatállapotot használja, és nem próbálja újraimportálni a schema/modul/overlay konfigurációt
- `maintenance` módban csak a konfigurációs, TLS és compose/runtime változásokat viszi át; nem buildel új image-et és nem írja felül az adatbázist
- lefuttatja a megfelelő smoke teszteket

## Branch kontextus

A `main` branch volt a stabilabb alap: innen maradt meg a normál Ansible build/deploy flow, a Docker image build, az entrypoint alaplogika, a schema/modul/overlay konfiguráció és a smoke test lánc. A `development` branch-ből hasznos irányként a full migration igény maradt meg, de a félkész megoldások ki lettek véve: hardcoded host/adat/jelszó, commitolt `ansible.log`, külön `openldap-ykbind:migrate` image, feltétel nélküli `slapadd` a `configure.yml` közepén, valamint a hardcoded konténernévvel és `/opt/openldap-ykbind` útvonallal dolgozó próbálkozások.

## Repository felépítés

- [ansible/](/home/username/Documents/yubik/ansible): inventory, playbook, role, defaults, vars, templates, files
- [docker/openldap/](/home/username/Documents/yubik/docker/openldap): a konténerhez használt `Dockerfile` és `entrypoint.sh`
- [schema/](/home/username/Documents/yubik/schema): a saját schema LDIF és schema forrás
- [examples/](/home/username/Documents/yubik/examples): kézi LDAP példák és korábbi LDIF minták
- [tools/filter-unsupported-ldif.sh](/home/username/Documents/yubik/tools/filter-unsupported-ldif.sh): import előtti LDIF szűrő, alapból az `ou=dns` ág kidobásához
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
ldap_container_base_image: debian:trixie
ldap_local_artifact_root: /tmp/openldap-ykbind-artifacts
ldap_runtime_user: ldap-runtime
ldap_runtime_group: ldap-runtime
ldap_runtime_uid: 11050
ldap_runtime_gid: 11050

ldap_http_proxy: http://proxy.example.net:3128
ldap_https_proxy: http://proxy.example.net:3128
ldap_no_proxy: localhost,127.0.0.1

ldap_listen_port: 389
ldap_ldaps_port: 636
ldap_container_ldap_port: 1389
ldap_container_ldaps_port: 1636
ldap_enable_ldaps: false
ldap_deploy_mode: full_import
ldap_maintenance_restart_container: false

ldap_migration_config_ldifs: []
ldap_ldif_import_file: ""
ldap_ldif_filter_enabled: true
ldap_ldif_drop_subtrees:
  - "ou=dns,{{ ldap_base_dn }}"

ldap_module_build_enabled: true
ldap_enable_syslog_ng: false
ldap_open_files_limit: 1024

ldap_data_dir: /opt/openldap-ykbind/data
ldap_config_dir: /opt/openldap-ykbind/config
ldap_log_dir: /opt/openldap-ykbind/logs
```

További fontos, deploy közben gyakran használt változók:

- `ldap_ports`
- `ldap_container_base_image`
- `ldap_docker_build_network`
- `ldap_skip_local_build`
- `ldap_skip_local_save`
- `ldap_deploy_mode`
- `ldap_runtime_user`
- `ldap_runtime_group`
- `ldap_runtime_uid`
- `ldap_runtime_gid`
- `ldap_data_dir`
- `ldap_config_dir`
- `ldap_log_dir`
- `ldap_tls_host_dir`
- `ldap_migration_config_ldifs`
- `ldap_ldif_import_file`
- `ldap_ldif_filter_enabled`
- `ldap_ldif_drop_subtrees`
- `ldap_ldif_drop_objectclasses`
- `ldap_force_password_reset`
- `ldap_enable_syslog_ng`
- `ldap_open_files_limit`
- `ldap_tls_certificate_src`
- `ldap_tls_private_key_src`
- `ldap_tls_ca_src`
- `ldap_maintenance_restart_container`
- `ldap_smoke_test_user_dn`

Felülírási lehetőségek:

- `ansible/group_vars/all.yml`
- `ansible/host_vars/<host>.yml`
- `ansible-playbook -e key=value`

## One-click deploy futtatás

`full_import` példa lokális inventoryval:

```bash
cd /home/username/Documents/yubik/ansible
ansible-playbook playbooks/deploy-openldap.yml \
  -e ldap_deploy_mode=full_import \
  -e ldap_domain=example.org \
  -e ldap_base_dn=dc=example,dc=org \
  -e ldap_admin_dn="cn=admin,dc=example,dc=org" \
  -e ldap_admin_password='<set-admin-password>' \
  -e ldap_ldif_import_file=exports/full-tree.ldif
```

`adopt_existing` példa távoli hostra:

```bash
cd /home/username/Documents/yubik/ansible
ansible-playbook -i inventory/hosts.ini playbooks/deploy-openldap.yml \
  -e ldap_deploy_mode=adopt_existing \
  -e ldap_config_dir=/opt/openldap/config \
  -e ldap_data_dir=/opt/openldap-ykbind/data
```

`maintenance` példa meglévő konténeres stack módosítására:

```bash
cd /home/username/Documents/yubik/ansible
ansible-playbook -i inventory/hosts.ini playbooks/deploy-openldap.yml \
  -e ldap_deploy_mode=maintenance \
  -e ldap_enable_ldaps=true \
  -e ldap_tls_certificate_src=tls/tls.crt \
  -e ldap_tls_private_key_src=tls/tls.key \
  -e ldap_tls_ca_src=tls/ca.crt \
  -e ldap_maintenance_restart_container=true
```

Ha a local image már biztosan elkészült a control node-on, újrafuttatáskor a build kihagyható:

```bash
cd /home/username/Documents/yubik/ansible
ansible-playbook -i inventory/hosts.ini playbooks/deploy-openldap.yml \
  -e ldap_skip_local_build=true
```

Ha a local tarball is már létezik és azt is meg akarod tartani:

```bash
cd /home/username/Documents/yubik/ansible
ansible-playbook -i inventory/hosts.ini playbooks/deploy-openldap.yml \
  -e ldap_skip_local_build=true \
  -e ldap_skip_local_save=true
```

Deploy mód megjegyzés:

- `ldap_deploy_mode=full_import`: migration-first mód; a role automatikusan újra létrehozza a perzisztens `data/` és `config/` könyvtárakat, `LDAP_INIT_MODE=config-only` módban indítja a konténert, majd explicit config/schema/data importot hajt végre
- `ldap_deploy_mode=adopt_existing`: meglévő bind mountolt `cn=config` és adatállapotot használ; a role `LDAP_INIT_MODE=disabled` módban indít, és nem próbál módosító init/import műveleteket futtatni
- `ldap_deploy_mode=maintenance`: meglévő, a role által kezelt konténeren fut; ellenőrzi a container name-et, a bind mountokat és a managed labelt vagy a legacy signature-t, majd csak in-place frissítéseket végez
- a régi `migration` módnév továbbra is elfogadott, belsőleg `full_import`-ként viselkedik
- ha a `ldap_base_dn` még az alap `dc=example,dc=org` értéken van, full import módban a role megpróbálja az import LDIF első `dn:` sorából levezetni
- ha az admin DN is még alapértéken van, full import módban azt `cn=admin,<derived_base_dn>` formára állítja
- `full_import` és `adopt_existing` módban image build és `docker load` történik; `maintenance` módban ez a két lépés kimarad

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

Ownership szabályok:

- a writable és a `slapd` által olvasott bind mountok alapból `11050:11050` tulajdonba kerülnek
- ez a `ldap_runtime_uid` és `ldap_runtime_gid` változókkal módosítható
- a role a hoston rekurzívan igazítja a `data/`, `config/`, `logs/`, `runtime/ldif/`, `runtime/schema/`, `runtime/ldif/imports/`, `runtime/ldif/config-imports/` és `runtime/tls/` ownershipjét
- a TLS private key alapból `0600`, a cert és CA `0644`, mindhárom a runtime uid/gid tulajdonában
- a konténer belül a `slapd` magas belső portokon figyel: `1389` és opcionálisan `1636`; a host oldali publikált port marad `389` és `636`

Lokális control node build artifactok alapértelmezett helye:

```text
/tmp/openldap-ykbind-artifacts
```

Fontos: mivel az image build a control node-on fut, a `ldap_http_proxy`, `ldap_https_proxy` és `ldap_no_proxy` változók a control node nézőpontjából értendők. Ha a proxy csak a guest VM-ből érhető el, de a control node-ról nem, a megakadás a `Build OpenLDAP image on control node` task alatt fog jelentkezni.

## Migrációs export meglévő LDAP-ból

A stabil import sorrend:

1. config: csak a hordozható, célrendszeren is értelmezhető `cn=config` módosítások
2. schema: minden olyan schema LDIF, amelyre a full tree hivatkozik
3. full tree: a teljes adatfa, az eldobandó `ou=dns` ág nélkül

Teljes adatfa export:

```bash
ldapsearch -x -LLL -o ldif-wrap=no \
  -D "cn=admin,dc=example,dc=org" \
  -W \
  -H ldap://OLD-LDAP-HOST:389 \
  -b "dc=example,dc=org" \
  "(objectClass=*)" > /home/username/Documents/yubik/exports/full-tree.ldif
```

Schema lista export referencia célra a forrás OpenLDAP hoston, helyben futtatva:

```bash
ldapsearch -Q -Y EXTERNAL -H ldapi:/// -LLL -o ldif-wrap=no \
  -b "cn=schema,cn=config" \
  "(objectClass=olcSchemaConfig)" > /home/username/Documents/yubik/schema/source-schema.reference.ldif
```

Egy konkrét, szükséges custom schema exportja:

```bash
ldapsearch -Q -Y EXTERNAL -H ldapi:/// -LLL -o ldif-wrap=no \
  -b "cn={4}custom,cn=schema,cn=config" \
  -s base \
  "(objectClass=olcSchemaConfig)" > /home/username/Documents/yubik/schema/custom.ldif
```

Config export referencia célra, szintén a forrás hoston:

```bash
ldapsearch -Q -Y EXTERNAL -H ldapi:/// -LLL -o ldif-wrap=no \
  -b "cn=config" \
  "(objectClass=*)" > /home/username/Documents/yubik/exports/source-cn-config.reference.ldif
```

Ne importáld vakon a teljes `cn=config` exportot. Abban host- és buildfüggő elemek lehetnek: modul path, backend modulok, TLS útvonalak, accesslog/syncprov beállítások, régi backendek, eltérő `{N}` sorszámok. A támogatott út az, hogy a cél image saját Debian/OpenLDAP configot hoz létre, majd a szükséges hordozható config módosításokat `ldap_migration_config_ldifs` listában adod meg.

Példa migrációs config LDIF:

```ldif
dn: cn=config
changetype: modify
replace: olcLogLevel
olcLogLevel: stats
```

## `ou=dns` szűrése

A forrás LDAP-ban lévő `ou=dns` ág a régi DNS backend/schema miatt nem importálható ebbe a cél image-be. Alapértelmezésben a role import előtt kiszűri ezt a teljes subtreet:

```yaml
ldap_ldif_filter_enabled: true
ldap_ldif_drop_subtrees:
  - "ou=dns,{{ ldap_base_dn }}"
```

Kézi szűrés ugyanazzal a helperrel:

```bash
cd /home/username/Documents/yubik
tools/filter-unsupported-ldif.sh \
  --drop-subtree "ou=dns,dc=example,dc=org" \
  --output exports/full-tree.filtered.ldif \
  exports/full-tree.ldif
```

Ha más nem támogatott objektumosztály is marad az exportban, ugyanitt lehet külön objektumosztály alapján is dobni:

```bash
tools/filter-unsupported-ldif.sh \
  --drop-subtree "ou=dns,dc=example,dc=org" \
  --drop-objectclass idnsRecord \
  --output exports/full-tree.filtered.ldif \
  exports/full-tree.ldif
```

## Full import futtatása

`full_import` módban a role mindig szűz cél LDAP-ot készít elő. A perzisztens `data/` és `config/` könyvtárak törlésre és újra létrehozásra kerülnek, a konténer csak minimális `cn=config` állapottal indul, és sem az entrypoint, sem az Ansible nem hoz létre előre base DN-t vagy OU-kat.

Teljes restore:

```bash
cd /home/username/Documents/yubik/ansible
ansible-playbook playbooks/deploy-openldap.yml \
  -e ldap_deploy_mode=full_import \
  -e ldap_domain=example.org \
  -e ldap_base_dn=dc=example,dc=org \
  -e ldap_admin_dn="cn=admin,dc=example,dc=org" \
  -e ldap_admin_password='<set-admin-password>' \
  -e ldap_additional_schema_ldifs='["schema/custom.ldif"]' \
  -e ldap_migration_config_ldifs='["exports/migration-config.ldif"]' \
  -e ldap_ldif_import_file=exports/full-tree.ldif
```

Teljes próba egy új távoli hostra, meglévő schema és full-tree fájlokkal:

```bash
cd /home/username/Documents/yubik/ansible
ansible-playbook -i inventory/hosts.ini playbooks/deploy-openldap.yml \
  --limit ldap_new_host \
  -Kk \
  -e ldap_deploy_mode=full_import \
  -e ldap_domain=example.org \
  -e ldap_base_dn=dc=example,dc=org \
  -e ldap_admin_dn="cn=admin,dc=example,dc=org" \
  -e ldap_admin_password='<set-admin-password>' \
  -e ldap_container_base_image=debian:trixie \
  -e ldap_ldif_filter_enabled=true \
  -e ldap_ldif_drop_subtrees='["ou=dns,dc=example,dc=org"]' \
  -e ldap_additional_schema_ldifs='["schema/custom.ldif","schema/legacy-app.ldif"]' \
  -e ldap_migration_config_ldifs='["exports/migration-config.ldif"]' \
  -e ldap_ldif_import_file=exports/full-tree.ldif
```

A `ldap_migration_config_ldifs` opcionális. Ha nincs hordozható config módosítás, hagyd üresen. A `ldap_additional_schema_ldifs` listába csak olyan schema LDIF kerüljön, amely a cél OpenLDAP-ban ténylegesen betölthető. Ne add meg a teljes `source-schema.reference.ldif` fájlt importként; abból csak a ténylegesen hiányzó custom schema entryket exportáld külön. A régi DNS backendhez tartozó schema és adat ne kerüljön be, ha a céloldalon nincs hozzá backend/plugin támogatás.

A migrációs config LDIF-ek legyenek újrafuttatható `changetype: modify` jellegű módosítások, vagy csak egyszer használt, dokumentált lépések. Teljes `cn=config` dump automatikus visszatöltése nem cél, mert túl sok környezetfüggő elemet tartalmaz.

Az import sorrendje a playbookban:

1. Docker image build/load
2. perzisztens `data/` és `config/` könyvtárak újra létrehozása
3. konténerindítás `LDAP_INIT_MODE=config-only` módban, non-root `slapd` runtime-mal
4. suffix/rootDN/rootPW beállítás a cél `cn=config` alatt
5. `ldap_migration_config_ldifs` alkalmazása `ldapmodify -Y EXTERNAL` paranccsal
6. bundled `yubikey-otp` schema és `ldap_additional_schema_ldifs` betöltése
7. `ykbind.so` modul és overlay konfigurálása
8. `ou=dns` és egyéb megadott részek szűrése a full-tree LDIF-ből
9. konténer leállítása
10. offline `slapadd -F /etc/ldap/slapd.d -n 1 -l full-tree.ldif`
11. DB ownership javítása és konténer újraindítása

## Adopt existing futtatása

`adopt_existing` módban a role nem próbálja újrainicializálni vagy átírni a meglévő LDAP-ot. A bind mountolt `cn=config` és adatkönyvtárakból indul, `LDAP_INIT_MODE=disabled` módban.

```bash
cd /home/username/Documents/yubik/ansible
ansible-playbook -i inventory/hosts.ini playbooks/deploy-openldap.yml \
  --limit ldap_existing_host \
  -Kk \
  -e ldap_deploy_mode=adopt_existing \
  -e ldap_config_dir=/opt/openldap/config \
  -e ldap_data_dir=/opt/openldap-ykbind/data \
  -e ldap_log_dir=/opt/openldap-ykbind/logs \
  -e ldap_container_base_image=debian:trixie
```

Ebben a módban:

- a `ldap_config_dir` és `ldap_data_dir` könyvtárnak a playbook indulásakor már léteznie és tartalmaznia kell adatot
- a role nem futtat schema importot
- a role nem futtat migration config LDIF-et
- a role nem módosítja a `cn=module` entryt
- a role nem hoz létre base DN-t vagy OU-kat
- a role a host oldali ownershipet a dedikált runtime uid/gid értékre igazítja, hogy a non-root konténer írni tudjon
- a role csak külső `ldapi:///` smoke teszteket fut

## Maintenance futtatása

`maintenance` módban a role csak már korábban ezzel a stackkel deployolt konténeres LDAP szolgáltatást fogad el. Ellenőrzi a `ldap_container_name` alatti konténert, a bind mountokat és a managed metadata-t, és ha ezek nem illenek a várt stackre, hibával leáll.

```bash
cd /home/username/Documents/yubik/ansible
ansible-playbook -i inventory/hosts.ini playbooks/deploy-openldap.yml \
  --limit ldap_existing_host \
  -Kk \
  -e ldap_deploy_mode=maintenance \
  -e ldap_enable_ldaps=true \
  -e ldap_tls_certificate_src=tls/tls.crt \
  -e ldap_tls_private_key_src=tls/tls.key \
  -e ldap_tls_ca_src=tls/ca.crt
```

Ebben a módban:

- nincs local docker build
- nincs `docker save` vagy `docker load`
- nincs `data/` vagy `config/` reset
- nincs full-tree LDIF import
- nincs destruktív DB újrainicializálás
- van compose/template/env frissítés
- van TLS fájl frissítés
- van idempotens schema/module/overlay/TLS config ellenőrzés és szükség esetén `ldapmodify`
- opcionálisan van kontrollált restart a `ldap_maintenance_restart_container=true` változóval

Fontos restore megjegyzések:

- a full restore nem merge mechanizmus, hanem üres céladatbázisba történő betöltés
- ha a base DN már létezik, a playbook megáll, mert az import biztosan ütközne
- `full_import` módban a role automatikusan törli és újra létrehozza a perzisztens `data/` és `config/` könyvtárakat
- `adopt_existing` módban a role ezekhez a könyvtárakhoz nem nyúl destruktívan
- relatív LDIF útvonalakat a role a repository gyökeréhez viszonyítva old fel

## TLS / LDAPS

A konténeren belül a `slapd` a `1389` és `1636` magas portokat használja, a host felé továbbra is a `389` és `636` portok publikálódnak. A playbook támogatja az LDAPS bekapcsolását, de a tanúsítványkezelést szándékosan egyszerű, bővíthető formában hagyja meg.

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
- maintenance módban image rebuild nélkül is tudja frissíteni a fájlokat
- ha a cert/key tartalma változott, állítsd `ldap_maintenance_restart_container=true` értékre, hogy a futó `slapd` biztosan újranyissa a fájlokat

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
- `apt-get source openldap`
- a szükséges build dependency-ket explicit Debian csomaglistából telepíti
- a builder stage OpenLDAP forrásból futtatott `configure`, `make depend` és `make -C include` lépéssel állítja elő a modulhoz szükséges `portable.h`, `ldap_features.h` és `ldap_config.h` headereket
- a repositoryban lévő [Makefile](/home/username/Documents/yubik/Makefile) fut
- a lefordított `ykbind.so` a runtime image `/usr/lib/ldap/ykbind.so` helyére kerül
- a runtime image saját `ldap-runtime` user/group accountot hoz létre, és a `slapd` `gosu` segítségével erre a dedikált uid/gid-re vált induláskor
- a kész image a control node-on tarballként exportálódik
- a tarball a cél LDAP hostra kerül és ott `docker load` importálja
- a playbook utána ellenőrzi a `cn=config` alatti module listákat, és csak akkor tölti be a modult, ha még nincs jelen
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

Ez hasznos gyors sanity checkhez is: így először a konténer deploy láncot tudod validálni a saját overlay buildje nélkül, majd csak utána kapcsolod vissza a modulfodítást.

## LDAP inicializálás és konfiguráció

A deploy során a role:

- `full_import` módban újra létrehozza a perzisztens `data/` és `config/` könyvtárakat
- `full_import` módban csak minimális `cn=config`-et bootstrapol
- `full_import` módban importálja a saját schema LDIF-et
- `full_import` módban opcionálisan további schema LDIF-eket is importál a `ldap_additional_schema_ldifs` listából
- `full_import` módban opcionálisan hordozható config LDIF-eket alkalmaz a `ldap_migration_config_ldifs` listából
- `full_import` módban szükség esetén beállítja a `olcSuffix`, `olcRootDN`, `olcRootPW` értékeket
- `full_import` módban betölti a modult és felveszi az overlayt
- `adopt_existing` módban nem futtat destruktív vagy módosító init/import műveleteket a meglévő `cn=config` ellen
- `maintenance` módban idempotens schema/module/overlay/TLS config ellenőrzést és szükség esetén update-et futtat, de nem írja felül az adatfát

A schema import nem fix `cn={N}` indexre támaszkodik. A role minden schema import előtt kiolvassa a teljes `cn=schema,cn=config` tartalmat, majd a betöltendő LDIF-et schema entrynként hasonlítja össze a meglévő állapottal. Egy teljes forrásoldali `cn=schema,cn=config` dump is megadható: a már meglévő beépített schema entryk kimaradnak, és csak a teljesen hiányzó custom schema entryk kerülnek egy szűrt import LDIF-be. Ha egy hiányzó nevű entry OID-jai részben már léteznek más schema alatt, a playbook hibával megáll, mert az `ldapadd` ilyen esetben `duplicate attributeType` hibával bukna.

Az entrypoint init módjai:

- `LDAP_INIT_MODE=fresh`: Debian `slapd` bootstrap, config és kezdő adatbázis létrejön
- `LDAP_INIT_MODE=config-only`: Debian `slapd` bootstrap után a `/var/lib/ldap` törlődik, így csak a működő `cn=config` marad meg
- `LDAP_INIT_MODE=disabled`: nincs bootstrap; csak előre mountolt, érvényes `/etc/ldap/slapd.d` mellett használható

Megjegyzés: `full_import` módban a `slapd` Debianos inicializálása csak átmeneti, minimális `cn=config` állapot előállítására szolgál. A role ezután explicit config/schema/data import lépésekkel készíti elő a cél LDAP-ot. `adopt_existing` módban ez a bootstrap nem fut le, mert a konténer közvetlenül a már meglévő bind mountolt `cn=config`-ből indul.

Runtime megjegyzés:

- a container entrypoint rootként csak bootstrapol és ownershipet igazít
- a tényleges `slapd` folyamat dedikált `ldap_runtime_uid:ldap_runtime_gid` accounttal fut
- a host oldali bind mountok ownershipje ezért ugyanarra a numerikus uid/gid-re áll be

Konténer-specifikus megjegyzés:

- az `invoke-rc.d ... policy-rc.d denied execution of start` üzenet a bootstrap során várható és önmagában nem hiba
- a `syslog-ng` alapból ki van kapcsolva, mert Dockerben a capability-kezelés fölösleges zajt okoz
- a `ldap_open_files_limit` alapból `1024`, mert egyes Debian/OpenLDAP konténeres futásoknál a túl magas `nofile` limit `ch_calloc` crash-t okozhat

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
  -H ldap://127.0.0.1:1389
```

Base query:

```bash
docker exec openldap-ykbind ldapsearch \
  -x -D "cn=admin,dc=example,dc=org" \
  -w '<set-admin-password>' \
  -H ldap://127.0.0.1:1389 \
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

## YubiKey attribútumok felvétele meglévő userekre

Ha a user entryk strukturális object class-a például `customProfileClass`, ahhoz gond nélkül hozzáadható a YubiKey auxiliary class:

- `objectClass: yubiKeyTokenAux`

A biztonságos sorrend:

1. add hozzá a `yubiKeyTokenAux` osztályt
2. állítsd be a `yubiKeyEnabled: FALSE` értéket
3. töltsd fel a userhez tartozó YubiKey secret mezőket
4. csak ezután állítsd `yubiKeyEnabled: TRUE` értékre

Ez fontos, mert az overlay akkor is elkezdhet OTP-t követelni, ha a secret mezők már bent vannak, de a `yubiKeyEnabled` mező nincs explicit `FALSE` értékre állítva.

Minden `cn` azonosítójú entry előkészítése `ou=people` alatt, ahol van `objectClass=customProfileClass`, de még nincs `yubiKeyTokenAux`:

```bash
ldapsearch -x -LLL -D "cn=admin,dc=example,dc=org" -W \
  -H ldap://127.0.0.1:389 \
  -b "ou=people,dc=example,dc=org" \
  "(&(objectClass=customProfileClass)(cn=*)(!(objectClass=yubiKeyTokenAux)))" dn |
awk '
  /^dn: / {
    dn=substr($0,5)
    print "dn: " dn
    print "changetype: modify"
    print "add: objectClass"
    print "objectClass: yubiKeyTokenAux"
    print "-"
    print "add: yubiKeyEnabled"
    print "yubiKeyEnabled: FALSE"
    print ""
  }
' > /tmp/yubikey-bootstrap-disabled.ldif
```

Alkalmazás:

```bash
ldapmodify -x -D "cn=admin,dc=example,dc=org" -W \
  -H ldap://127.0.0.1:389 \
  -f /tmp/yubikey-bootstrap-disabled.ldif
```

Ha egy entryn már rajta van a `yubiKeyTokenAux`, de még nincs `yubiKeyEnabled`, akkor külön ezt is fel lehet venni:

```bash
ldapsearch -x -LLL -D "cn=admin,dc=example,dc=org" -W \
  -H ldap://127.0.0.1:389 \
  -b "ou=people,dc=example,dc=org" \
  "(&(objectClass=customProfileClass)(objectClass=yubiKeyTokenAux)(!(yubiKeyEnabled=*)))" dn |
awk '
  /^dn: / {
    dn=substr($0,5)
    print "dn: " dn
    print "changetype: modify"
    print "add: yubiKeyEnabled"
    print "yubiKeyEnabled: FALSE"
    print ""
  }
' > /tmp/yubikey-set-disabled.ldif
```

```bash
ldapmodify -x -D "cn=admin,dc=example,dc=org" -W \
  -H ldap://127.0.0.1:389 \
  -f /tmp/yubikey-set-disabled.ldif
```

Példa egy user YubiKey secret mezőinek feltöltésére úgy, hogy még letiltott állapotban maradjon:

```ldif
dn: cn=Alice Example,ou=people,dc=example,dc=org
changetype: modify
replace: yubiKeyEnabled
yubiKeyEnabled: FALSE
-
replace: yubiKeyPublicId
yubiKeyPublicId: cccjgjgkhcbb
-
replace: yubiKeyPrivateUid
yubiKeyPrivateUid: 001122334455
-
replace: yubiKeyAesKey
yubiKeyAesKey: 8899aabbccddeeff0011223344556677
-
replace: yubiKeyLastUseCtr
yubiKeyLastUseCtr: 0
-
replace: yubiKeyLastSessionCtr
yubiKeyLastSessionCtr: 0
-
replace: yubiKeyLastTimestamp
yubiKeyLastTimestamp: 0
```

Ezután az adott user engedélyezése:

```ldif
dn: cn=Alice Example,ou=people,dc=example,dc=org
changetype: modify
replace: yubiKeyEnabled
yubiKeyEnabled: TRUE
```

Tömeges engedélyezés azoknál a usereknél, ahol a secret mezők már megvannak:

```bash
ldapsearch -x -LLL -D "cn=admin,dc=example,dc=org" -W \
  -H ldap://127.0.0.1:389 \
  -b "ou=people,dc=example,dc=org" \
  "(&(objectClass=customProfileClass)(objectClass=yubiKeyTokenAux)(yubiKeyPrivateUid=*)(yubiKeyAesKey=*))" dn |
awk '
  /^dn: / {
    dn=substr($0,5)
    print "dn: " dn
    print "changetype: modify"
    print "replace: yubiKeyEnabled"
    print "yubiKeyEnabled: TRUE"
    print ""
  }
' > /tmp/yubikey-enable.ldif
```

```bash
ldapmodify -x -D "cn=admin,dc=example,dc=org" -W \
  -H ldap://127.0.0.1:389 \
  -f /tmp/yubikey-enable.ldif
```

Javasolt ACL-lel védeni a secret mezőket. Van hozzá példa itt:

- [examples/acl-yubikey-secrets.ldif](/home/username/Documents/yubik/examples/acl-yubikey-secrets.ldif)

## Kézi LDAP példák

Megmaradt kézi referenciafájlok:

- [examples/module-load.ldif](/home/username/Documents/yubik/examples/module-load.ldif)
- [examples/module-path-and-load.ldif](/home/username/Documents/yubik/examples/module-path-and-load.ldif)
- [examples/overlay-config.ldif](/home/username/Documents/yubik/examples/overlay-config.ldif)
- [examples/acl-yubikey-secrets.ldif](/home/username/Documents/yubik/examples/acl-yubikey-secrets.ldif)

Ezek főleg debughoz és kézi finomhangoláshoz hasznosak; a normál deploy út az Ansible playbook.

## Ismert korlátok

- a full tree import restore jellegű, nem általános merge mechanizmus
- a teljes `cn=config` 1:1 importja nem támogatott automatikusan; hordozható `ldapmodify` LDIF-eket használj
- az `ou=dns` ág alapból kidobásra kerül, mert a cél image nem tartalmazza a régi DNS backend/plugin támogatást
- a `olcRootPW` idempotens cseréje csak akkor fut le, ha a suffix/rootDN eltér vagy a `ldap_force_password_reset=true`
- a `ldap_domain` és `ldap_base_dn` eltérése támogatott, de nem ez a Debian `slapd` inicializálás natív útja
- TLS esetén a tanúsítványanyag meglétét a role ellenőrzi, de a PKI életciklus-kezelést nem automatizálja
- maintenance mód csak a role által kezelt, mount- és metadata-szinten felismerhető konténeren fut
