# Deploy TODO — exact commands

## 0. Prerequisites

Control node-on: Docker CLI + user a `docker` csoportban. Bundle kibontva: `release-bundle/repo/` és `release-bundle/images/`.

## 1. Export custom schemas from the old LDAP (egyszer, a régi gépen)

Egyenként exportáld a nem-built-in schema-kat. A built-in-ek (core, cosine, nis, inetorgperson) és a full cn=config dump NE kerüljön a listába.

```bash
# Régi LDAP-on: listázd a nem-built-in schema-kat
sudo ldapsearch -Q -Y EXTERNAL -H ldapi:/// -LLL -o ldif-wrap=no \
  -b "cn=schema,cn=config" "(objectClass=olcSchemaConfig)" cn

# Exportáld a custom schema-kat egyenként (példa):
for s in radius3 dyndbschema fw1auth hvfo; do
  sudo ldapsearch -Q -Y EXTERNAL -H ldapi:/// -LLL -o ldif-wrap=no \
    -b "cn=schema,cn=config" "(cn=*$s*)" \
    dn cn objectClass olcAttributeTypes olcObjectClasses \
    > exports/$s.ldif
done
```

A vars fájlba CSAK ezek kerüljenek:

```yaml
# production.yml
ldap_additional_schema_ldifs:
  - exports/radius3.ldif
  - exports/dyndbschema.ldif
  - exports/fw1auth.ldif
  - exports/hvfo.ldif
```

Ne legyen benne `exported-schemas.ldif` — az teljes cn=config dump, built-in-ekkel és hibás OID-okkal, ami csak problémát okoz.

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

## 4. Diagnosztika: ldapadd import kimenet ellenőrzése

Ha a full_import után csak bizonyos OU-k populálódtak, futtasd a VM-en:

```bash
docker exec -i openldap-ykbind ldapadd -c -x \
  -D "cn=admin,dc=..." -w '...' \
  -H ldap://127.0.0.1:1389 \
  -f /opt/openldap/bootstrap/ldif/imports/full-tree.ldif 2>&1 \
  | grep -E "(already exists|undefined|violation|error|Skipping)" | head -30
```

Cseréld ki a `dc=...`-t és a jelszót. A kimenet megmutatja, hogy pontosan mely entry-k és miért maradtak ki.

## 5. Hiányzó schema definíciók felkutatása a régi LDAP-on

Ha az ldapadd `radiusLoginService` vagy `radiusGroupName` undefined hibát ad, a régi LDAP valószínűleg flat `.schema` fájlból tölti ezeket. Régi LDAP-on futtasd:

```bash
# 1. Keresés a schema könyvtárakban
grep -r "radiusGroupName" /etc/ldap/schema/ /etc/freeradius/ 2>/dev/null

# 2. Keresés a teljes fájlrendszerben
find / -name "*.schema" -o -name "*.ldif" 2>/dev/null \
  | xargs grep -l "radiusGroupName\|radiusLoginService" 2>/dev/null

# 3. Ellenőrzés cn=config-ban (ha slapd.conf helyett cn=config van)
ls -la /etc/ldap/slapd.d/cn=config/cn=schema/
```

Ha megtaláltad a `.schema` fájlt, másold át a control node `repo/exports/`-ba, és adjuk hozzá `ldap_additional_schema_ldifs`-hez (át kell konvertálni LDIF-be).

## 6. Ha a teljes image rebuild kell

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
