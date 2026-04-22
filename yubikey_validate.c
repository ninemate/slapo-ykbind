// yubikey_bind_plugin.c - 389 Directory Server Pre-Bind Plugin for YubiKey OTP Validation

// REQUIRED PACKAGES: openssl-devel gcc

// EXTERNAL DEPENDENCY: 389-ds-base-devel (https://access.redhat.com/downloads/content/rhel---9/x86_64/11063/389-ds-base-devel/2.6.1-8.el9_6/x86_64/fd431d51/package)

// COMPILATION: gcc -fPIC -shared -o yubikey-bind-plugin.so yubikey-bind-plugin.c $(pkg-config --cflags --libs 389-ds-base-plugin) -lssl -lcrypto

// COMPILATION-NEW: gcc -fPIC -shared -o yubikey-bind-plugin.so yubikey-bind-plugin.c $(pkg-config --cflags --libs dirsrv svrcore) -lssl -lcrypto

#define _GNU_SOURCE

 

#include "slapi-plugin.h"

#include <string.h>

#include <stdlib.h>

#include <stdio.h>

#include <time.h>

 

#include <openssl/aes.h>

#include <openssl/evp.h>

 

#ifndef SLAPI_PLUGIN_COMPONENT_ID

#define SLAPI_PLUGIN_COMPONENT_ID 0x0800  // This is the internal ID used by RHDS/389ds

#endif

 

#define ATTR_KEYID          "YKkeyID"

#define ATTR_AESKEY         "YKaesKey"

#define ATTR_COUNTER        "YKkeyCounter"

#define ATTR_TIMESTAMP      "YKsessionTimestamp"

#define ATTR_MODIFY_TIME    "modifyTimestamp"

#define OTP_LENGTH          44

 

static Slapi_PluginDesc plugin_desc = {

    "yubikey-bind-plugin",   // plugin name

    "GIRO",                  // vendor

    "1.0",                   // version

    "YubiKey OTP Plugin"     // description

};

 

static void *yubikey_plugin_identity = NULL;

 

static void modhex_to_hex(const char* modhex, char* hex_out) {

    const char* modhex_chars = "cbdefghijklnrtuv";

    const char* hex_chars = "0123456789abcdef";

    size_t len = strlen(modhex);

    for (size_t i = 0; i < len; ++i) {

        const char* p = strchr(modhex_chars, modhex[i]);

        hex_out[i] = p ? hex_chars[p - modhex_chars] : '0';

    }

    hex_out[len] = '\0';

}

 

char* decrypt_yubikey_otp(const char* otp, const char* aes_key) {

    slapi_log_error(SLAPI_LOG_PLUGIN, "yubikey_bind", "Inside decrypt_yubikey_otp()\n");

 

    char ciphertext_hex[33] = {0};

    modhex_to_hex(otp + 12, ciphertext_hex);

 

    unsigned char key[16] = {0};

    for (int i = 0; i < 16; ++i) {

        sscanf(aes_key + i*2, "%2hhx", &key[i]);

    }

 

    unsigned char cipher_bytes[16];

    for (int i = 0; i < 16; ++i) {

        sscanf(ciphertext_hex + i*2, "%2hhx", &cipher_bytes[i]);

    }

 

    AES_KEY dec_key;

    AES_set_decrypt_key(key, 128, &dec_key);

 

    unsigned char plain[16];

    AES_decrypt(cipher_bytes, plain, &dec_key);

 

    char* result = malloc(33);

    for (int i = 0; i < 16; ++i) {

        sprintf(result + i*2, "%02x", plain[i]);

    }

    result[32] = '\0';

    return result;

}

 

static void cleanup_plugin(LDAPMod ***mods, char **real_pass, char **otp, char **plain, struct berval **real_cred, Slapi_Entry **entry){

    if (mods && *mods) {

        for (int i = 0; (*mods)[i] != NULL; ++i) {

            slapi_ch_free_string(&(*mods)[i]->mod_type);

            if ((*mods)[i]->mod_values) {

                for (int j = 0; (*mods)[i]->mod_values[j] != NULL; ++j) {

                    slapi_ch_free_string(&(*mods)[i]->mod_values[j]);

                }

                slapi_ch_free((void **)&(*mods)[i]->mod_values);

            }

            slapi_ch_free((void **)&(*mods)[i]);

        }

        slapi_ch_free((void **)mods);

    }

 

    if (real_pass && *real_pass) {

        free(*real_pass);

        *real_pass = NULL;

    }

 

    if (otp && *otp) {

        free(*otp);

        *otp = NULL;

    }

 

    if (plain && *plain) {

        free(*plain);

        *plain = NULL;

    }

 

    if (real_cred && *real_cred) {

        slapi_ch_free_string(&(*real_cred)->bv_val);

        slapi_ch_free((void **)real_cred);

    }

 

    if (entry && *entry) {

        slapi_entry_free(*entry);

        *entry = NULL;

    }

}

 

static LDAPMod **create_yubikey_mods(const char *session_counter, const char *session_ts) {

    LDAPMod **mods = (LDAPMod **)slapi_ch_malloc(3 * sizeof(LDAPMod *));  // 2 mods + NULL

 

    // First mod: Counter

    mods[0] = (LDAPMod *)slapi_ch_malloc(sizeof(LDAPMod));

    mods[0]->mod_op = LDAP_MOD_REPLACE;

    mods[0]->mod_type = slapi_ch_strdup(ATTR_COUNTER);

    mods[0]->mod_values = (char **)slapi_ch_malloc(2 * sizeof(char *));

    mods[0]->mod_values[0] = slapi_ch_strdup(session_counter);

    mods[0]->mod_values[1] = NULL;

 

    // Second mod: Timestamp

    mods[1] = (LDAPMod *)slapi_ch_malloc(sizeof(LDAPMod));

    mods[1]->mod_op = LDAP_MOD_REPLACE;

    mods[1]->mod_type = slapi_ch_strdup(ATTR_TIMESTAMP);

    mods[1]->mod_values = (char **)slapi_ch_malloc(2 * sizeof(char *));

    mods[1]->mod_values[0] = slapi_ch_strdup(session_ts);

    mods[1]->mod_values[1] = NULL;

 

    // Null-terminate the array

    mods[2] = NULL;

 

    return mods;

}

 

static int yubikey_bind(Slapi_PBlock *pb) {

    slapi_log_error(SLAPI_LOG_PLUGIN, "yubikey_bind", "Inside yubikey_bind()\n");

 

    const char *binddn = NULL;

    struct berval *cred = NULL;

    LDAPMod **mods = NULL;

    char *real_pass = NULL;

    char *otp = NULL;

    int pass_len = 0;

    char *plain = NULL;

    struct berval *real_cred = NULL;

    Slapi_Entry *entry = NULL;

    Slapi_Entry **entries = NULL;

 

    slapi_pblock_get(pb, SLAPI_BIND_CREDENTIALS, &cred);

    slapi_pblock_get(pb, SLAPI_BIND_TARGET, &binddn);

 

    if (!binddn || !cred)

        return SLAPI_BIND_SUCCESS;

 

    Slapi_PBlock *search_pb = slapi_pblock_new();

    slapi_search_internal_set_pb(search_pb, binddn, LDAP_SCOPE_BASE, "(objectClass=*)", NULL, 0, NULL, NULL, yubikey_plugin_identity, 0);

    slapi_search_internal_pb(search_pb);

    slapi_pblock_get(search_pb, SLAPI_PLUGIN_INTOP_SEARCH_ENTRIES, &entries);

    entry = slapi_entry_dup(entries[0]);

    slapi_pblock_destroy(search_pb);

 

    const char *aes_key = slapi_entry_attr_get_charptr(entry, ATTR_AESKEY);

    const char *key_id = slapi_entry_attr_get_charptr(entry, ATTR_KEYID);

    const char *old_counter = slapi_entry_attr_get_charptr(entry, ATTR_COUNTER);

    const char *old_timestamp = slapi_entry_attr_get_charptr(entry, ATTR_TIMESTAMP);

    const char *last_mod = slapi_entry_attr_get_charptr(entry, ATTR_MODIFY_TIME);

 

    slapi_log_error(SLAPI_LOG_PLUGIN, "yubikey_bind", "Bind DN: %s - aes_key: %s\n", binddn, aes_key);

    slapi_log_error(SLAPI_LOG_PLUGIN, "yubikey_bind", "Bind DN: %s - key_id: %s\n", binddn, key_id);

    slapi_log_error(SLAPI_LOG_PLUGIN, "yubikey_bind", "Bind DN: %s - old_counter: %s\n", binddn, old_counter);

    slapi_log_error(SLAPI_LOG_PLUGIN, "yubikey_bind", "Bind DN: %s - old_timestamp: %s\n", binddn, old_timestamp);

    slapi_log_error(SLAPI_LOG_PLUGIN, "yubikey_bind", "Bind DN: %s - last_mod: %s\n", binddn, last_mod);

 

    if (!aes_key || !key_id || strncmp(key_id, "NOKEY", 5) == 0) {

        slapi_entry_free(entry);

        slapi_log_error(SLAPI_LOG_PLUGIN, "yubikey_bind", "Bind DN: %s - NO AES KEY OR NO KEY ID\n", binddn);

        return SLAPI_BIND_SUCCESS;

    }

 

    if (cred && cred->bv_val) {

      pass_len = (unsigned long)cred->bv_len - OTP_LENGTH;

      if (pass_len > 0){

        real_pass = strndup(cred->bv_val, pass_len);

        otp = strdup(cred->bv_val+ pass_len);

      } else {

        slapi_log_error(SLAPI_LOG_FATAL, "yubikey_bind", "Bind DN: %s - ERROR OTP is required, pass_len: %d\n", binddn, pass_len);

        goto deny;

      }

    } else {

        return SLAPI_BIND_SUCCESS;

    }

 

    plain = decrypt_yubikey_otp(otp, aes_key);

    if (!plain || strlen(plain) < 32) {

        slapi_log_error(SLAPI_LOG_FATAL, "yubikey_bind", "Decryption failed\n");

        goto deny;

    }

    slapi_log_error(SLAPI_LOG_PLUGIN, "yubikey_bind", "Bind DN: %s - plain otp: %s\n", binddn, plain);

 

    char session_key[13] = {0};

    strncpy(session_key, plain, 12);

    if (strncmp(session_key, key_id, 12) != 0) {

        slapi_log_error(SLAPI_LOG_FATAL, "yubikey_bind", "Session key mismatch\n");

        goto deny;

    }

    slapi_log_error(SLAPI_LOG_PLUGIN, "yubikey_bind", "Bind DN: %s - session_key: %s\n", binddn, session_key);

 

    char session_counter[7] = {0};

    snprintf(session_counter, sizeof(session_counter), "%c%c%c%c%c%c", plain[14], plain[15], plain[12], plain[13], plain[22], plain[23]);

 

    if (old_counter && strcmp(session_counter, old_counter) < 0) {

        slapi_log_error(SLAPI_LOG_FATAL, "yubikey_bind", "Replay attack detected\n");

        goto deny;

    }

    slapi_log_error(SLAPI_LOG_PLUGIN, "yubikey_bind", "Bind DN: %s - session_counter: %s\n", binddn, session_counter);

 

    int auth_chain = (old_counter && strcmp(session_counter, old_counter) == 0);

 

    if (auth_chain && last_mod) {

        struct tm tm;

        strptime(last_mod, "%Y%m%d%H%M%SZ", &tm);

        time_t last_time = timegm(&tm);

        time_t now = time(NULL);

        if (now - last_time > 900) {

            slapi_log_error(SLAPI_LOG_FATAL, "yubikey_bind", "Bind DN: %s - Chained auth expired\n", binddn);

            goto deny;

        }

        slapi_log_error(SLAPI_LOG_PLUGIN, "yubikey_bind", "Bind DN: %s - Chained auth\n", binddn);

    }

 

    char session_ts[7] = {0};

    snprintf(session_ts, sizeof(session_ts), "%c%c%c%c%c%c", plain[20], plain[21], plain[18], plain[19], plain[16], plain[17]);

    slapi_log_error(SLAPI_LOG_PLUGIN, "yubikey_bind", "Bind DN: %s - session_ts: %s\n", binddn, session_ts);


    if (!auth_chain && old_timestamp && old_counter && strncmp(session_counter, old_counter, 4) == 0) {

        int old_ts = (int)strtol(old_timestamp, NULL, 16);

        int new_ts = (int)strtol(session_ts, NULL, 16);

        int delta_ts = (new_ts-old_ts)/8;

 

        struct tm tm;

        strptime(last_mod, "%Y%m%d%H%M%SZ", &tm);

        time_t last_time = timegm(&tm);

        time_t now = time(NULL);

        int delta_session = now - last_time;

 

        //TODO  ldap parallel modification eg. lsc

        if (delta_session < delta_ts && abs(delta_session-delta_ts) > 100) {

            slapi_log_error(SLAPI_LOG_FATAL, "yubikey_bind", "Bind DN: %s - Time difference is too high; deltaTimestamp: %d; deltaSession: %d\n", binddn, delta_ts, delta_session);

            //goto deny;

        }

    }

 

    slapi_log_error(SLAPI_LOG_PLUGIN, "yubikey_bind", "Bind DN: %s - OTP is valid\n", binddn);

 

    //Modify ldap values

    mods = create_yubikey_mods(session_counter, session_ts);

    Slapi_PBlock *mod_pb = slapi_pblock_new();

    slapi_modify_internal_set_pb(mod_pb, binddn, mods, NULL, NULL, yubikey_plugin_identity, 0);

    int rc = slapi_modify_internal_pb(mod_pb);

    slapi_log_error(SLAPI_LOG_PLUGIN, "yubikey_bind", "Bind DN: %s - Counter modification rc: %d\n", binddn, rc);

 

    //Modify pass, cut otp

    real_cred = (struct berval *)slapi_ch_malloc(sizeof(struct berval));

    real_cred->bv_val = slapi_ch_strdup(real_pass);

    real_cred->bv_len = strlen(real_pass);

    slapi_pblock_set(pb, SLAPI_BIND_CREDENTIALS, real_cred);

 

    cleanup_plugin(&mods, &real_pass, &otp, &plain, NULL, &entry);

    return SLAPI_BIND_SUCCESS;

 

deny:

    cleanup_plugin(&mods, &real_pass, &otp, &plain, &real_cred, &entry);

    int result_code = LDAP_INVALID_CREDENTIALS;

    slapi_pblock_set(pb, SLAPI_RESULT_CODE, &result_code);

    return LDAP_INVALID_CREDENTIALS;

}

 

int slapi_plugin_init(Slapi_PBlock *pb) {

    slapi_log_error(SLAPI_LOG_PLUGIN, "yubikey_bind", "Initializing YubiKey plugin\n");

    slapi_pblock_set(pb, SLAPI_PLUGIN_VERSION, SLAPI_PLUGIN_CURRENT_VERSION);

    slapi_pblock_set(pb, SLAPI_PLUGIN_DESCRIPTION, &plugin_desc);

    slapi_pblock_set(pb, SLAPI_PLUGIN_PRE_BIND_FN, (void *)yubikey_bind);

    slapi_pblock_get(pb, SLAPI_PLUGIN_IDENTITY, &yubikey_plugin_identity);

 

    if (yubikey_plugin_identity == NULL) {

      slapi_log_error(SLAPI_LOG_PLUGIN, "yubikey_plugin", "No identity present in pblock\n");

    }

 

    return 0;

}
