TASK [openldap_docker : Render managed dynamic_clients module config to target host] ************************************************************************************************************************
ok: [v-nua.gironet.giro.hu]
ok: [m-nua.gironet.giro.hu]

TASK [openldap_docker : Copy managed dynamic_clients module config into mods-enabled on target host] ********************************************************************************************************
ok: [m-nua.gironet.giro.hu]
ok: [v-nua.gironet.giro.hu]

TASK [openldap_docker : Copy external default site config to target host] ***********************************************************************************************************************************
skipping: [m-nua.gironet.giro.hu]
skipping: [v-nua.gironet.giro.hu]

TASK [openldap_docker : Render managed default site config to target host] **********************************************************************************************************************************
ok: [m-nua.gironet.giro.hu]
ok: [v-nua.gironet.giro.hu]

TASK [openldap_docker : Copy external inner-tunnel site config to target host] ******************************************************************************************************************************
skipping: [m-nua.gironet.giro.hu]
skipping: [v-nua.gironet.giro.hu]

TASK [openldap_docker : Render managed inner-tunnel site config to target host] *****************************************************************************************************************************
ok: [v-nua.gironet.giro.hu]
ok: [m-nua.gironet.giro.hu]

TASK [openldap_docker : Store RADIUS config change state] ***************************************************************************************************************************************************
ok: [m-nua.gironet.giro.hu]
ok: [v-nua.gironet.giro.hu]

TASK [openldap_docker : Verify docker-compose is available] *************************************************************************************************************************************************
ok: [m-nua.gironet.giro.hu]
ok: [v-nua.gironet.giro.hu]

TASK [openldap_docker : Copy prebuilt image archive to target host] *****************************************************************************************************************************************
ok: [v-nua.gironet.giro.hu]
ok: [m-nua.gironet.giro.hu]

TASK [openldap_docker : Copy prebuilt RADIUS image archive to target host] **********************************************************************************************************************************
ok: [m-nua.gironet.giro.hu]
ok: [v-nua.gironet.giro.hu]

TASK [openldap_docker : Check whether the image already exists on target host] ******************************************************************************************************************************
ok: [v-nua.gironet.giro.hu]
ok: [m-nua.gironet.giro.hu]

TASK [openldap_docker : Load prebuilt image on target host] *************************************************************************************************************************************************
skipping: [m-nua.gironet.giro.hu]
skipping: [v-nua.gironet.giro.hu]

TASK [openldap_docker : Check whether the RADIUS image already exists on target host] ***********************************************************************************************************************
ok: [m-nua.gironet.giro.hu]
ok: [v-nua.gironet.giro.hu]

TASK [openldap_docker : Load prebuilt RADIUS image on target host] ******************************************************************************************************************************************
skipping: [m-nua.gironet.giro.hu]
skipping: [v-nua.gironet.giro.hu]

TASK [openldap_docker : Check whether the managed systemd service is already active] ************************************************************************************************************************
ok: [v-nua.gironet.giro.hu]
ok: [m-nua.gironet.giro.hu]

TASK [openldap_docker : Determine whether the managed systemd service needs a restart] **********************************************************************************************************************
ok: [m-nua.gironet.giro.hu]
ok: [v-nua.gironet.giro.hu]

TASK [openldap_docker : Enable and reconcile the managed systemd service] ***********************************************************************************************************************************
changed: [m-nua.gironet.giro.hu]
changed: [v-nua.gironet.giro.hu]

TASK [openldap_docker : Wait for LDAP TCP listener on target host] ******************************************************************************************************************************************
ok: [m-nua.gironet.giro.hu]
ok: [v-nua.gironet.giro.hu]

TASK [openldap_docker : Wait until ldapi is ready inside the container] *************************************************************************************************************************************
ok: [m-nua.gironet.giro.hu]
ok: [v-nua.gironet.giro.hu]

TASK [openldap_docker : Discover the primary LDAP database entry] *******************************************************************************************************************************************
ok: [m-nua.gironet.giro.hu]
ok: [v-nua.gironet.giro.hu]

TASK [openldap_docker : Fail if no LDAP database entry was found] *******************************************************************************************************************************************
skipping: [m-nua.gironet.giro.hu]
skipping: [v-nua.gironet.giro.hu]

TASK [openldap_docker : Store current database metadata] ****************************************************************************************************************************************************
ok: [m-nua.gironet.giro.hu]
ok: [v-nua.gironet.giro.hu]

TASK [openldap_docker : Render overlay config LDIF after database discovery] ********************************************************************************************************************************
ok: [m-nua.gironet.giro.hu]
ok: [v-nua.gironet.giro.hu]

TASK [openldap_docker : Hash admin password for rootDN reset] ***********************************************************************************************************************************************
ok: [v-nua.gironet.giro.hu]
ok: [m-nua.gironet.giro.hu]

TASK [openldap_docker : Render database config update LDIF] *************************************************************************************************************************************************
changed: [m-nua.gironet.giro.hu]
changed: [v-nua.gironet.giro.hu]

TASK [openldap_docker : Update LDAP suffix, rootDN and rootPW for full_import] ******************************************************************************************************************************
changed: [m-nua.gironet.giro.hu]
changed: [v-nua.gironet.giro.hu]

TASK [openldap_docker : Apply optional migration config LDIFs] **********************************************************************************************************************************************
skipping: [m-nua.gironet.giro.hu]
skipping: [v-nua.gironet.giro.hu]

TASK [openldap_docker : Import bundled YubiKey schema idempotently] *****************************************************************************************************************************************
included: /home/mkokai/.ansible/repo/network_auth/07-AUTOMATIZACIO/set-hvf-nua-server/ansible/roles/openldap_docker/tasks/schema_import.yml for m-nua.gironet.giro.hu, v-nua.gironet.giro.hu

TASK [openldap_docker : Read current cn=config schema before bundled-yubikey] *******************************************************************************************************************************
ok: [m-nua.gironet.giro.hu]
ok: [v-nua.gironet.giro.hu]

TASK [openldap_docker : Store current schema dump for bundled-yubikey] **************************************************************************************************************************************
ok: [m-nua.gironet.giro.hu -> localhost]
ok: [v-nua.gironet.giro.hu -> localhost]

TASK [openldap_docker : Check schema import state for bundled-yubikey] **************************************************************************************************************************************
ok: [m-nua.gironet.giro.hu -> localhost]
ok: [v-nua.gironet.giro.hu -> localhost]

TASK [openldap_docker : Show schema import decision for bundled-yubikey] ************************************************************************************************************************************
ok: [m-nua.gironet.giro.hu] => {
    "msg": [
        "schema_name=yubikey-otp",
        "schema_entries_total=1",
        "schema_entries_missing=1",
        "schema_entries_present=0",
        "schema_entries_conflict=0",
        "definition_total=12",
        "definition_hits=0",
        "missing_schema_names=yubikey-otp",
        "present_schema_names=",
        "conflict_schema_names=",
        "state=missing"
    ]
}
ok: [v-nua.gironet.giro.hu] => {
    "msg": [
        "schema_name=yubikey-otp",
        "schema_entries_total=1",
        "schema_entries_missing=1",
        "schema_entries_present=0",
        "schema_entries_conflict=0",
        "definition_total=12",
        "definition_hits=0",
        "missing_schema_names=yubikey-otp",
        "present_schema_names=",
        "conflict_schema_names=",
        "state=missing"
    ]
}

TASK [openldap_docker : Fail on conflicting schema definitions for bundled-yubikey] *************************************************************************************************************************
skipping: [m-nua.gironet.giro.hu]
skipping: [v-nua.gironet.giro.hu]

TASK [openldap_docker : Copy prepared schema import LDIF for bundled-yubikey] *******************************************************************************************************************************
ok: [v-nua.gironet.giro.hu]
ok: [m-nua.gironet.giro.hu]

TASK [openldap_docker : Import schema into cn=config for bundled-yubikey] ***********************************************************************************************************************************
fatal: [v-nua.gironet.giro.hu]: FAILED! => {"changed": true, "cmd": ["docker", "exec", "openldap-ykbind", "ldapadd", "-c", "-Q", "-Y", "EXTERNAL", "-H", "ldapi:///", "-f", "/opt/openldap/bootstrap/schema/schema-import-bundled-yubikey.ldif"], "delta": "0:00:00.116335", "end": "2026-06-02 12:56:12.855859", "msg": "non-zero return code", "rc": 80, "start": "2026-06-02 12:56:12.739524", "stderr": "ldap_add: Other (e.g., implementation specific) error (80)\n\tadditional info: olcAttributeTypes: Duplicate attributeType: \"1.3.6.1.4.1.55555.1.1\"", "stderr_lines": ["ldap_add: Other (e.g., implementation specific) error (80)", "\tadditional info: olcAttributeTypes: Duplicate attributeType: \"1.3.6.1.4.1.55555.1.1\""], "stdout": "adding new entry \"cn=yubikey-otp,cn=schema,cn=config\"\n\nadding new entry \"cn=yubikey-otp,cn=schema,cn=config\"", "stdout_lines": ["adding new entry \"cn=yubikey-otp,cn=schema,cn=config\"", "", "adding new entry \"cn=yubikey-otp,cn=schema,cn=config\""]}
fatal: [m-nua.gironet.giro.hu]: FAILED! => {"changed": true, "cmd": ["docker", "exec", "openldap-ykbind", "ldapadd", "-c", "-Q", "-Y", "EXTERNAL", "-H", "ldapi:///", "-f", "/opt/openldap/bootstrap/schema/schema-import-bundled-yubikey.ldif"], "delta": "0:00:00.111776", "end": "2026-06-02 12:56:13.234193", "msg": "non-zero return code", "rc": 80, "start": "2026-06-02 12:56:13.122417", "stderr": "ldap_add: Other (e.g., implementation specific) error (80)\n\tadditional info: olcAttributeTypes: Duplicate attributeType: \"1.3.6.1.4.1.55555.1.1\"", "stderr_lines": ["ldap_add: Other (e.g., implementation specific) error (80)", "\tadditional info: olcAttributeTypes: Duplicate attributeType: \"1.3.6.1.4.1.55555.1.1\""], "stdout": "adding new entry \"cn=yubikey-otp,cn=schema,cn=config\"\n\nadding new entry \"cn=yubikey-otp,cn=schema,cn=config\"", "stdout_lines": ["adding new entry \"cn=yubikey-otp,cn=schema,cn=config\"", "", "adding new entry \"cn=yubikey-otp,cn=schema,cn=config\""]}

PLAY RECAP **************************************************************************************************************************************************************************************************
m-nua.gironet.giro.hu      : ok=101  changed=9    unreachable=0    failed=1    skipped=51   rescued=0    ignored=0   
v-nua.gironet.giro.hu      : ok=85   changed=9    unreachable=0    failed=1    skipped=30   rescued=0    ignored=0   
