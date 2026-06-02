slapd configuration validation failed under /etc/ldap/slapd.d
config error processing cn=yubikey-otp,cn=schema,cn=config,cn=schema,cn=config: 
slaptest: bad configuration directory!
slapd configuration validation failed under /etc/ldap/slapd.d
config error processing cn=yubikey-otp,cn=schema,cn=config,cn=schema,cn=config: 
slaptest: bad configuration directory!
slapd configuration validation failed under /etc/ldap/slapd.d
config error processing cn=yubikey-otp,cn=schema,cn=config,cn=schema,cn=config: 
slaptest: bad configuration directory!
mkokai@v-nua:~$ ls /opt/openldap-ykbind/
config/             data/               docker-compose.yml  images/             logs/               radius/             runtime/            
mkokai@v-nua:~$ ls /opt/openldap-ykbind/config/
ls: cannot open directory '/opt/openldap-ykbind/config/': Permission denied
mkokai@v-nua:~$ sudo ls /opt/openldap-ykbind/config/
'cn=config'  'cn=config.ldif'
mkokai@v-nua:~$ sudo -i
root@v-nua:~# ls /opt/openldap-ykbind/config/cn\=config
cn=config/      cn=config.ldif  
root@v-nua:~# ls /opt/openldap-ykbind/config/cn\=config/
'cn=module{0}.ldif'  'cn=schema'  'cn=schema.ldif'  'olcDatabase={0}config.ldif'  'olcDatabase={-1}frontend.ldif'  'olcDatabase={1}mdb'  'olcDatabase={1}mdb.ldif'
root@v-nua:~# ls /opt/openldap-ykbind/config/cn\=config
cn=config/      cn=config.ldif  
root@v-nua:~# ls /opt/openldap-ykbind/config/cn\=config
cn=config/      cn=config.ldif  
root@v-nua:~# ls /opt/openldap-ykbind/config/cn\=config
cn=config/      cn=config.ldif  
root@v-nua:~# ls /opt/openldap-ykbind/config/cn\=config/
cn=module{0}.ldif              cn=schema/                     cn=schema.ldif                 olcDatabase={0}config.ldif     olcDatabase={-1}frontend.ldif  olcDatabase={1}mdb/            olcDatabase={1}mdb.ldif
root@v-nua:~# ls /opt/openldap-ykbind/config/cn\=config/cn\=schema/cn\=\{
cn={0}core.ldif                 cn={1}cosine.ldif               cn={2}cn=yubikey-otp.ldif       cn={3}inetorgperson.ldif        cn={5}cn={3}inetorgperson.ldif  cn={6}hvfo.ldif                 cn={9}cn={7}hvfo.ldif
cn={1}cn=schema.ldif            cn={1}extra-1.ldif              cn={2}nis.ldif                  cn={4}cn={2}nis.ldif            cn={5}dyndbschema.ldif          cn={7}cn={5}dyndbschema.ldif    
cn={1}cn=yubikey-otp.ldif       cn={2}cn={0}core.ldif           cn={3}cn={1}cosine.ldif         cn={4}yubikey-otp.ldif          cn={6}cn={4}radius3.ldif        cn={8}cn={6}fw1auth.ldif        
root@v-nua:~# ls /opt/openldap-ykbind/config/cn\=config/cn\=schema/cn\=\{
