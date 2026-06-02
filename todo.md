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

**Ha a radius3.ldif-ben `myRadiusFlag:1`-szerű OID makrók vannak**, exportáld a makró definíciókat is:

```bash
# Régi LDAP-on: OID makrók exportálása
sudo ldapsearch -Q -Y EXTERNAL -H ldapi:/// -LLL -o ldif-wrap=no \
  -b "cn=config" "(olcObjectIdentifier=*)" dn olcObjectIdentifier \
  > exports/radius-oid-macros.ldif
```

## 2. Ellenőrizd a hiányzó schema-kat és exportáld őket

```bash
# Régi LDAP-on: listázd az összes schema-t
sudo ldapsearch -Q -Y EXTERNAL -H ldapi:/// -LLL -o ldif-wrap=no \
  -b "cn=schema,cn=config" "(objectClass=olcSchemaConfig)" cn \
  | grep "^cn:" | sort

# Az új LDAP-on is futtasd ugyanezt, és hasonlítsd össze.
# Ami a régiben van de az újban nincs, exportáld:
for s in ppolicy; do
  sudo ldapsearch -Q -Y EXTERNAL -H ldapi:/// -LLL -o ldif-wrap=no \
    -b "cn=schema,cn=config" "(cn=*$s*)" \
    dn cn objectClass olcAttributeTypes olcObjectClasses \
    > exports/$s.ldif
done
```

A hiányzó schema fájlokat tedd be a vars listába a többi közé.

## 3. OID makrók exportálása (ha szükséges)

```bash
# Régi LDAP-on: OID makrók exportálása
sudo ldapsearch -Q -Y EXTERNAL -H ldapi:/// -LLL -o ldif-wrap=no \
  -b "cn=config" "(olcObjectIdentifier=*)" dn olcObjectIdentifier \
  > exports/radius-oid-macros.ldif
```

Tedd BELE a listába, a radius3 ELÉ:

```yaml
ldap_additional_schema_ldifs:
  - exports/radius-oid-macros.ldif    # ← makrók előbb
  - exports/radius3.ldif
  - exports/dyndbschema.ldif
  - exports/fw1auth.ldif
  - exports/hvfo.ldif
```

## 4. Teljes cleanup mindkét VM-en

```bash
# VM1-en és VM2-n:
systemctl stop openldap-ykbind-compose 2>/dev/null
systemctl disable openldap-ykbind-compose 2>/dev/null
docker rm -f openldap-ykbind freeradius-ldap 2>/dev/null
rm -rf /opt/openldap-ykbind
docker container prune -f
```

## 5. full_import futtatása

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

## 6. Diagnosztika: ldapadd import kimenet ellenőrzése

Ha a full_import után csak bizonyos OU-k populálódtak, futtasd a VM-en:

```bash
docker exec -i openldap-ykbind ldapadd -c -x \
  -D "cn=admin,dc=..." -w '...' \
  -H ldap://127.0.0.1:1389 \
  -f /opt/openldap/bootstrap/ldif/imports/full-tree.ldif 2>&1 \
  | grep -E "(already exists|undefined|violation|error|Skipping)" | head -30
```

Cseréld ki a `dc=...`-t és a jelszót. A kimenet megmutatja, hogy pontosan mely entry-k és miért maradtak ki.

## 7. Ha a teljes image rebuild kell

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
