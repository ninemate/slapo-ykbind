mkokai@v-nua:~$ sudo docker exec -i openldap-ykbind ldapadd -c -x   -D "cn=admin,dc=,dc=hu" -w ''   -H ldap://127.0.0.1:1389   -f /opt/openldap/bootstrap/ldif/imports/full-tree.ldif 2>&1   | grep -E "(already exists|undefined|violation|error|Skipping)" | head -30
        additional info: radiusLoginService: attribute type undefined
        additional info: radiusLoginService: attribute type undefined
        additional info: radiusGroupName: attribute type undefined
        additional info: radiusGroupName: attribute type undefined
        additional info: radiusGroupName: attribute type undefined
        additional info: radiusGroupName: attribute type undefined
        additional info: radiusGroupName: attribute type undefined
        additional info: radiusGroupName: attribute type undefined
        additional info: radiusLoginService: attribute type undefined
        additional info: radiusGroupName: attribute type undefined
        additional info: radiusGroupName: attribute type undefined
        additional info: radiusGroupName: attribute type undefined
        additional info: radiusGroupName: attribute type undefined
        additional info: radiusGroupName: attribute type undefined
        additional info: radiusLoginService: attribute type undefined
        additional info: radiusLoginService: attribute type undefined
        additional info: radiusLoginService: attribute type undefined
        additional info: radiusGroupName: attribute type undefined
        additional info: radiusGroupName: attribute type undefined
        additional info: radiusGroupName: attribute type undefined
        additional info: radiusGroupName: attribute type undefined
        additional info: radiusLoginService: attribute type undefined
        additional info: radiusLoginService: attribute type undefined
        additional info: radiusLoginService: attribute type undefined
        additional info: radiusGroupName: attribute type undefined
        additional info: radiusLoginService: attribute type undefined
        additional info: radiusGroupName: attribute type undefined
        additional info: radiusLoginService: attribute type undefined
        additional info: radiusLoginService: attribute type undefined
        additional info: radiusLoginService: attribute type undefined
mkokai@v-nua:~$ sudo docker exec -i openldap-ykbind ldapadd -c -x   -D "cn=admin,dc=,dc=hu" -w ''   -H ldap://127.0.0.1:1389   -f /opt/openldap/bootstrap/ldif/imports/full-tree.filtered.ldif 2>&1   | grep -E "(already exists|undefined|violation|error|Skipping)" | head -30
mkokai@v-nua:~$ ls /opt/openldap-ykbind/
config/             data/               docker-compose.yml  images/             logs/               radius/             runtime/            
mkokai@v-nua:~$ ls /opt/openldap-ykbind/runtime/
ldif  schema  tls
mkokai@v-nua:~$ ls /opt/openldap-ykbind/runtime/ldif/
config-imports        imports           overlay-config.ldif          replication-db-config.ldif      replication-limits-add.ldif   replication-syncprov-add.ldif
database-config.ldif  module-load.ldif  replication-access-add.ldif  replication-global-config.ldif  replication-module-load.ldif  tls-config.ldif
mkokai@v-nua:~$ ls /opt/openldap-ykbind/runtime/ldif/imports/
full-tree.ldif
mkokai@v-nua:~$ ls /opt/openldap-ykbind/runtime/ldif/config-imports/
mkokai@v-nua:~$ 
