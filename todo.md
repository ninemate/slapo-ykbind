# Deploy TODO — exact commands

## 0. Prerequisites

Control node-on: Docker CLI + user a `docker` csoportban. Bundle kibontva: `release-bundle/repo/` és `release-bundle/images/`.

## 1. Teljes cleanup mindkét VM-en

```bash
# VM1-en és VM2-n:
systemctl stop openldap-ykbind-compose 2>/dev/null
systemctl disable openldap-ykbind-compose 2>/dev/null
docker rm -f openldap-ykbind freeradius-ldap 2>/dev/null
rm -rf /opt/openldap-ykbind
docker container prune -f
```

## 2. Schema kitörlése (ha mégis bent maradt)

```bash
# VM1-en és VM2-n, container futása után:
SCHEMA_DN=$(docker exec openldap-ykbind ldapsearch -Q -LLL -Y EXTERNAL -H ldapi:/// \
  -b cn=schema,cn=config "(olcAttributeTypes=*55555*)" dn 2>/dev/null | grep ^dn: | head -1)
if [ -n "$SCHEMA_DN" ]; then
  docker exec openldap-ykbind ldapdelete -Q -Y EXTERNAL -H ldapi:/// "$SCHEMA_DN"
  echo "törölve: $SCHEMA_DN"
else
  echo "nincs 55555-ös schema"
fi
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

## 4. Ha a schema import továbbra is duplicate-ot dob

A schema OID-k már bootstrap után is bent vannak az image-ben. Workaround: a `schema_import.yml` check scriptje nem ismeri fel a meglévő OID-kat, ezért újra akarja importálni.

**Ideiglenes workaround** — a configure.yml schema import részének kihagyása:

```bash
# Control node-on: ideiglenesen átnevezni a schema LDIF-et, hogy ne találja meg
mv release-bundle/repo/schema/yubikey-otp.ldif release-bundle/repo/schema/yubikey-otp.ldif.bak

# full_import futtatása (configure.yml sikeresen átugorja a schema importot)
ansible-playbook ... -e ldap_deploy_mode=full_import ...

# Utána vissza
mv release-bundle/repo/schema/yubikey-otp.ldif.bak release-bundle/repo/schema/yubikey-otp.ldif
```

**VAGY** — `adopt_existing` mód, ami kihagyja a configure.yml-t (schema, module, overlay nem állítódik be):

```bash
ansible-playbook ... -e ldap_deploy_mode=adopt_existing ...
```

Utána `maintenance` mód a module/overlay beállításához (de ez újra meghívja a schema importot — szóval csak a workaround után).

## 5. Hiányzó schema-k exportálása a régi LDAP-ról

A `full-tree.ldif` tartalmazhat olyan attribútumokat (pl. `radiusLoginService`) amik nincsenek a standard OpenLDAP schema-k között. Ezeket a régi LDAP-ról kell exportálni.

```bash
# Régi LDAP-on: dump all non-built-in schemas
ldapsearch -Q -Y EXTERNAL -H ldapi:/// -LLL -o ldif-wrap=no \
  -b "cn=schema,cn=config" "(objectClass=olcSchemaConfig)" \
  dn olcAttributeTypes olcObjectClasses \
  | awk 'BEGIN{RS="";ORS="\n\n"} !/cn=\{0\}(core|cosine|nis|inetorgperson|displaymail)/' \
  > exports/exported-schemas.ldif
```

A vars fájlba add hozzá:

```yaml
ldap_additional_schema_ldifs:
  - exports/exported-schemas.ldif
```

Így a configure.yml betölti őket az slapadd előtt.

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
