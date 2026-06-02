# Deploy TODO — exact commands

## 0. Prerequisites

Control node-on: Docker CLI + user a `docker` csoportban. Bundle kibontva: `release-bundle/repo/` és `release-bundle/images/`.

## 1. Export ALL schemas from the old LDAP (egyszer, a régi gépen)

```bash
# Régi LDAP-on: export ALL schemas (including built-in ones)
# A check script úgyis kiszűri a már meglévőket.
sudo ldapsearch -Q -Y EXTERNAL -H ldapi:/// -LLL -o ldif-wrap=no \
  -b "cn=schema,cn=config" "(objectClass=olcSchemaConfig)" \
  dn olcAttributeTypes olcObjectClasses \
  > exports/exported-schemas.ldif
```

A vars fájlba:

```yaml
# production.yml
ldap_additional_schema_ldifs:
  - exports/exported-schemas.ldif
```

## 2. Teljes cleanup mindkét VM-en

```bash
# VM1-en és VM2-n:
systemctl stop openldap-ykbind-compose 2>/dev/null
systemctl disable openldap-ykbind-compose 2>/dev/null
docker rm -f openldap-ykbind freeradius-ldap 2>/dev/null
rm -rf /opt/openldap-ykbind
docker container prune -f
```

## 3. full_import futtatása

```bash
cd release-bundle/repo/ansible
ansible-playbook -i inventory/hosts.ini playbooks/deploy-openldap.yml \
  -e @../vars/production.yml \
  -e ldap_deploy_mode=full_import \
  -e ldap_ldif_import_file=exports/full-tree.ldif \
  -e ldap_skip_local_build=true \
  -e ldap_skip_local_save=true \
  -e radius_skip_local_build=true \
  -e radius_skip_local_save=true \
  -e ldap_local_image_archive=$PWD/../images/openldap-ykbind_latest.tar \
  -e radius_local_image_archive=$PWD/../images/freeradius-ldap_latest.tar \
  -Kk
```

## 4. Ha a teljes image rebuild kell

Build gépen:

```bash
cd ~/slapo-ykbind
git checkout main
git pull
docker build -t openldap-ykbind:latest -f docker/openldap/Dockerfile .
docker build -t freeradius-ldap:latest -f docker/freeradius/Dockerfile .
./tools/create-release-bundle.sh
```

Aztán a friss bundle-t átmásolni a control node-ra, cleanup, full_import.
