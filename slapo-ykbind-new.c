#define _GNU_SOURCE
#include "portable.h"

#include <ac/stdlib.h>
#include <ac/string.h>
#include <ac/time.h>

#include <ctype.h>
#include <openssl/crypto.h>
#include <openssl/evp.h>
#include <stdint.h>

#include "slap.h"

#ifndef LDAP_CONTROL_ASSERT
#define LDAP_CONTROL_ASSERT "1.3.6.1.1.12"
#endif

#define YKBIND_OTP_LENGTH 44
#define YKBIND_PUBLIC_ID_LENGTH 12
#define YKBIND_AES_KEY_BYTES 16
#define YKBIND_PRIVATE_UID_BYTES 6
#define YKBIND_CRC_OK_RESIDUAL 0xF0B8U
#define YKBIND_CHAIN_GRACE_SECONDS 60

typedef struct ykbind_ticket {
        unsigned char private_uid[YKBIND_PRIVATE_UID_BYTES];
        uint16_t use_ctr;
        uint32_t timestamp;
        uint8_t session_ctr;
        uint16_t rnd;
        uint16_t crc;
} ykbind_ticket;

typedef struct ykbind_opctx {
        slap_overinst *on;
        struct berval orig_cred;
        struct berval stripped_cred;
        struct berval req_ndn;
        struct berval req_dn;
        struct berval otp;
        char legacy_counter[7];
        char legacy_timestamp[7];
        unsigned long use_ctr;
        unsigned long session_ctr;
        unsigned long timestamp;
        int chained_auth;
} ykbind_opctx;

typedef struct ykbind_exop_ctx {
        slap_overinst *on;
        struct berval orig_reqdata;
        struct berval new_reqdata;
        struct berval req_ndn;
        struct berval req_dn;
        char legacy_counter[7];
        char legacy_timestamp[7];
        unsigned long use_ctr;
        unsigned long session_ctr;
        unsigned long timestamp;
        int chained_auth;
        int allocated_target;
} ykbind_exop_ctx;

typedef struct ykbind_passwd_req {
        struct berval user_dn;
        struct berval old_passwd;
        struct berval new_passwd;
        int has_user_dn;
        int has_old_passwd;
        int has_new_passwd;
} ykbind_passwd_req;

static slap_overinst ykbind;

static const char *ykbind_attr_enabled[] = {
        "yubiKeyEnabled",
        NULL
};

static const char *ykbind_attr_public_id[] = {
        "yubiKeyPublicId",
        NULL
};

static const char *ykbind_attr_private_uid[] = {
        "yubiKeyPrivateUid",
        "YKkeyID",
        NULL
};

static const char *ykbind_attr_aes_key[] = {
        "yubiKeyAesKey",
        "YKaesKey",
        NULL
};

static const char *ykbind_attr_last_use_ctr[] = {
        "yubiKeyLastUseCtr",
        NULL
};

static const char *ykbind_attr_last_session_ctr[] = {
        "yubiKeyLastSessionCtr",
        NULL
};

static const char *ykbind_attr_last_timestamp[] = {
        "yubiKeyLastTimestamp",
        NULL
};

static const char *ykbind_attr_legacy_timestamp[] = {
        "yubiKeyLegacyTimestamp",
        "YKsessionTimestamp",
        NULL
};

static const char *ykbind_attr_legacy_counter[] = {
        "yubiKeyLastCounter",
        "YKkeyCounter",
        NULL
};

static const char *ykbind_attr_modify_timestamp[] = {
"modifyTimestamp",
NULL
};

static AttributeDescription *ad_yk_last_use_ctr;
static AttributeDescription *ad_yk_last_session_ctr;
static AttributeDescription *ad_yk_last_timestamp;
static AttributeDescription *ad_yk_legacy_counter;
static AttributeDescription *ad_yk_legacy_timestamp;

static int ykbind_modhex_decode( const char *input, ber_len_t len, unsigned char *out, ber_len_t out_len );
static int ykbind_hex_decode( const char *src, size_t src_len, unsigned char *dst, size_t dst_len );
static void ykbind_hex_encode( const unsigned char *src, size_t len, char *dst );
static uint16_t ykbind_crc16( const unsigned char *data, size_t len );
static int ykbind_aes_decrypt( const unsigned char *key, const unsigned char *ciphertext, unsigned char *plaintext );
static void ykbind_parse_ticket( const unsigned char *plain, ykbind_ticket *ticket );
static const struct berval *ykbind_first_value( Entry *e, const char **names );
static int ykbind_parse_modify_timestamp( Entry *e, time_t *out );
static int ykbind_load_previous_state( Entry *e, unsigned long *use_ctr, unsigned long *session_ctr, unsigned long *timestamp );
static void ykbind_format_legacy_state( const ykbind_ticket *ticket, char *counter_out, char *timestamp_out );
static void ykbind_zero_free_cred( struct berval *bv );

static void
ykbind_set_backend( Operation *op_target, Operation *op, slap_overinst *on )
{
        BackendInfo *bi = op->o_bd->bd_info;

        if ( bi && bi->bi_type &&
                ( bi->bi_type == ykbind.on_bi.bi_type ||
                strcmp( bi->bi_type, ykbind.on_bi.bi_type ) == 0 ) )
        {
                op_target->o_bd->bd_info = (BackendInfo *)on->on_info;
        }
}

static int
ykbind_entry_get( Operation *op, slap_overinst *on, Entry **e )
{
        BackendInfo *bi = op->o_bd->bd_info;
        int rc;

        *e = NULL;
        ykbind_set_backend( op, op, on );
        rc = be_entry_get_rw( op, &op->o_req_ndn, NULL, NULL, 0, e );
        op->o_bd->bd_info = bi;

        return rc;
}

static void
ykbind_entry_release( Operation *op, slap_overinst *on, Entry *e )
{
        BackendInfo *bi;

        if ( e == NULL ) {
                return;
        }

        bi = op->o_bd->bd_info;
        ykbind_set_backend( op, op, on );
        be_entry_release_rw( op, e, 0 );
        op->o_bd->bd_info = bi;
}

static int
ykbind_modhex_value( char c )
{
        switch ( c ) {
        case 'c': return 0x0;
        case 'b': return 0x1;
        case 'd': return 0x2;
        case 'e': return 0x3;
        case 'f': return 0x4;
        case 'g': return 0x5;
        case 'h': return 0x6;
        case 'i': return 0x7;
        case 'j': return 0x8;
        case 'k': return 0x9;
        case 'l': return 0xA;
        case 'n': return 0xB;
        case 'r': return 0xC;
        case 't': return 0xD;
        case 'u': return 0xE;
        case 'v': return 0xF;
        default:
                return -1;
        }
}

static int
ykbind_split_password_otp(
        Operation *op,
        const struct berval *cred,
        struct berval *password,
        struct berval *otp,
        const char **reason,
        ber_len_t *bad_offset )
{
        ber_len_t i;

        password->bv_val = NULL;
        password->bv_len = 0;
        otp->bv_val = NULL;
        otp->bv_len = 0;

        if ( reason != NULL ) {
                *reason = "unknown";
        }

        if ( bad_offset != NULL ) {
                *bad_offset = (ber_len_t)-1;
        }

        if ( cred == NULL || BER_BVISNULL( cred ) || cred->bv_val == NULL ) {
                if ( reason != NULL ) {
                        *reason = "missing credentials";
                }
                return LDAP_INVALID_CREDENTIALS;
        }

        if ( cred->bv_len <= YKBIND_OTP_LENGTH ) {
                if ( reason != NULL ) {
                        *reason = "credential too short for password+OTP";
                }
                return LDAP_INVALID_CREDENTIALS;
        }

        otp->bv_len = YKBIND_OTP_LENGTH;
        otp->bv_val = cred->bv_val + cred->bv_len - YKBIND_OTP_LENGTH;

        for ( i = 0; i < otp->bv_len; i++ ) {
                if ( ykbind_modhex_value( otp->bv_val[i] ) < 0 ) {
                        if ( reason != NULL ) {
                                *reason = "OTP suffix contains non-modhex characters";
                        }
                        if ( bad_offset != NULL ) {
                                *bad_offset = i;
                        }
                        return LDAP_INVALID_CREDENTIALS;
                }
        }

        password->bv_len = cred->bv_len - otp->bv_len;
        password->bv_val = ch_malloc( password->bv_len + 1 );
        if ( password->bv_val == NULL ) {
                if ( reason != NULL ) {
                        *reason = "unable to allocate password buffer";
                }
                return LDAP_OTHER;
        }

        memcpy( password->bv_val, cred->bv_val, password->bv_len );
        password->bv_val[password->bv_len] = '\0';

        Debug( LDAP_DEBUG_TRACE,
                "%s: bind credential len=%lu, password len=%lu, otp len=%lu for %s\n",
                ykbind.on_bi.bi_type,
                (unsigned long)cred->bv_len,
                (unsigned long)password->bv_len,
                (unsigned long)otp->bv_len,
                op->o_req_ndn.bv_val );

        return LDAP_SUCCESS;
}

typedef struct ykbind_validate_otp_result {
        struct berval password;
        ykbind_ticket ticket;
        unsigned long old_use_ctr;
        unsigned long old_session_ctr;
        unsigned long old_timestamp;
        int chained_auth;
        char legacy_counter[7];
        char legacy_timestamp[7];
} ykbind_validate_otp_result;

static int
ykbind_validate_otp(
        Operation *op,
        slap_overinst *on,
        Entry *e,
        const struct berval *cred,
        ykbind_validate_otp_result *result )
{
        const struct berval *public_id_bv;
        const struct berval *private_uid_bv;
        const struct berval *aes_key_bv;
        struct berval otp = BER_BVNULL;
        struct berval public_id = BER_BVNULL;
        struct berval ciphertext_modhex = BER_BVNULL;
        unsigned char ciphertext[YKBIND_AES_KEY_BYTES];
        unsigned char plaintext[YKBIND_AES_KEY_BYTES];
        unsigned char aes_key[YKBIND_AES_KEY_BYTES];
        time_t last_modify_time = 0;
        time_t now_time = 0;
        char private_uid_hex[(YKBIND_PRIVATE_UID_BYTES * 2) + 1];
        const char *otp_reason = NULL;
        ber_len_t otp_bad_offset = (ber_len_t)-1;
        int rc;

        memset( result, 0, sizeof(*result) );
        result->password.bv_val = NULL;
        result->password.bv_len = 0;

        public_id_bv = ykbind_first_value( e, ykbind_attr_public_id );
        private_uid_bv = ykbind_first_value( e, ykbind_attr_private_uid );
        aes_key_bv = ykbind_first_value( e, ykbind_attr_aes_key );

        if ( private_uid_bv == NULL || aes_key_bv == NULL ) {
                Debug( LDAP_DEBUG_ANY,
                        "%s: YubiKey enabled but secret material missing on %s\n",
                        ykbind.on_bi.bi_type, op->o_req_ndn.bv_val );
                return LDAP_INVALID_CREDENTIALS;
        }

        rc = ykbind_split_password_otp( op, cred, &result->password, &otp,
                &otp_reason, &otp_bad_offset );
        if ( rc != LDAP_SUCCESS ) {
                Debug( LDAP_DEBUG_ANY,
                        "%s: invalid OTP suffix for %s: %s (credential len=%lu, bad offset=%ld)\n",
                        ykbind.on_bi.bi_type,
                        op->o_req_ndn.bv_val,
                        otp_reason ? otp_reason : "unknown",
                        (unsigned long)cred->bv_len,
                        (long)otp_bad_offset );
                return LDAP_INVALID_CREDENTIALS;
        }

        public_id.bv_val = otp.bv_val;
        public_id.bv_len = YKBIND_PUBLIC_ID_LENGTH;
        ciphertext_modhex.bv_val = otp.bv_val + YKBIND_PUBLIC_ID_LENGTH;
        ciphertext_modhex.bv_len = otp.bv_len - YKBIND_PUBLIC_ID_LENGTH;

        if ( public_id_bv != NULL && public_id_bv->bv_len > 0 &&
                ( public_id_bv->bv_len != public_id.bv_len ||
                ber_bvstrcasecmp( public_id_bv, &public_id ) != 0 ) )
        {
                Debug( LDAP_DEBUG_ANY,
                        "%s: public ID mismatch on %s\n",
                        ykbind.on_bi.bi_type, op->o_req_ndn.bv_val );
                ykbind_zero_free_cred( &result->password );
                return LDAP_INVALID_CREDENTIALS;
        }

        rc = ykbind_hex_decode( aes_key_bv->bv_val, aes_key_bv->bv_len,
                aes_key, sizeof(aes_key) );
        if ( rc != LDAP_SUCCESS ) {
                Debug( LDAP_DEBUG_ANY,
                        "%s: invalid AES key syntax on %s\n",
                        ykbind.on_bi.bi_type, op->o_req_ndn.bv_val );
                ykbind_zero_free_cred( &result->password );
                return LDAP_INVALID_CREDENTIALS;
        }

        rc = ykbind_modhex_decode( ciphertext_modhex.bv_val,
                ciphertext_modhex.bv_len, ciphertext, sizeof(ciphertext) );
        if ( rc != LDAP_SUCCESS ) {
                Debug( LDAP_DEBUG_ANY,
                        "%s: failed to decode OTP ciphertext for %s\n",
                        ykbind.on_bi.bi_type, op->o_req_ndn.bv_val );
                ykbind_zero_free_cred( &result->password );
                return LDAP_INVALID_CREDENTIALS;
        }

        rc = ykbind_aes_decrypt( aes_key, ciphertext, plaintext );
        OPENSSL_cleanse( aes_key, sizeof(aes_key) );
        if ( rc != LDAP_SUCCESS ) {
                Debug( LDAP_DEBUG_ANY,
                        "%s: AES decrypt failed for %s\n",
                        ykbind.on_bi.bi_type, op->o_req_ndn.bv_val );
                ykbind_zero_free_cred( &result->password );
                return LDAP_INVALID_CREDENTIALS;
        }

        if ( ykbind_crc16( plaintext, sizeof(plaintext) ) != YKBIND_CRC_OK_RESIDUAL ) {
                Debug( LDAP_DEBUG_ANY,
                        "%s: OTP CRC check failed for %s\n",
                        ykbind.on_bi.bi_type, op->o_req_ndn.bv_val );
                OPENSSL_cleanse( plaintext, sizeof(plaintext) );
                ykbind_zero_free_cred( &result->password );
                return LDAP_INVALID_CREDENTIALS;
        }

        ykbind_parse_ticket( plaintext, &result->ticket );
        OPENSSL_cleanse( plaintext, sizeof(plaintext) );
        ykbind_hex_encode( result->ticket.private_uid, sizeof(result->ticket.private_uid), private_uid_hex );

        if ( private_uid_bv->bv_len != strlen( private_uid_hex ) ||
                strncasecmp( private_uid_bv->bv_val, private_uid_hex, strlen( private_uid_hex ) ) != 0 )
        {
                Debug( LDAP_DEBUG_ANY,
                        "%s: private UID mismatch on %s\n",
                        ykbind.on_bi.bi_type, op->o_req_ndn.bv_val );
                ykbind_zero_free_cred( &result->password );
                return LDAP_INVALID_CREDENTIALS;
        }

        rc = ykbind_load_previous_state( e, &result->old_use_ctr, &result->old_session_ctr, &result->old_timestamp );
        if ( rc != LDAP_SUCCESS ) {
                Debug( LDAP_DEBUG_ANY,
                        "%s: stored replay state is invalid on %s\n",
                        ykbind.on_bi.bi_type, op->o_req_ndn.bv_val );
                ykbind_zero_free_cred( &result->password );
                return LDAP_INVALID_CREDENTIALS;
        }

        now_time = time( NULL );

        if ( result->ticket.use_ctr == result->old_use_ctr &&
                result->ticket.session_ctr == result->old_session_ctr &&
                result->ticket.timestamp == result->old_timestamp )
        {
                rc = ykbind_parse_modify_timestamp( e, &last_modify_time );
                if ( rc == LDAP_SUCCESS &&
                        last_modify_time > 0 &&
                        now_time >= last_modify_time &&
                        ( now_time - last_modify_time ) <= YKBIND_CHAIN_GRACE_SECONDS )
                {
                        result->chained_auth = 1;
                        Debug( LDAP_DEBUG_TRACE,
                        "%s: accepting chained OTP auth for %s within %d sec grace window\n",
                        ykbind.on_bi.bi_type,
                        op->o_req_ndn.bv_val,
                        YKBIND_CHAIN_GRACE_SECONDS );
                }
        }

        if ( !result->chained_auth &&
                ( result->ticket.use_ctr < result->old_use_ctr ||
                ( result->ticket.use_ctr == result->old_use_ctr &&
                result->ticket.session_ctr <= result->old_session_ctr ) ) )
        {
                Debug( LDAP_DEBUG_ANY,
                        "%s: replay detected on %s (stored=%lu/%lu/%lu new=%u/%u/%u)\n",
                        ykbind.on_bi.bi_type,
                        op->o_req_ndn.bv_val,
                        result->old_use_ctr,
                        result->old_session_ctr,
                        result->old_timestamp,
                        (unsigned int)result->ticket.use_ctr,
                        (unsigned int)result->ticket.session_ctr,
                        (unsigned int)result->ticket.timestamp );
                ykbind_zero_free_cred( &result->password );
                return LDAP_INVALID_CREDENTIALS;
        }

        ykbind_format_legacy_state( &result->ticket, result->legacy_counter, result->legacy_timestamp );

        return LDAP_SUCCESS;
}

static int
ykbind_modhex_decode( const char *input, ber_len_t len, unsigned char *out, ber_len_t out_len )
{
        ber_len_t i;

        if ( len % 2 != 0 || out_len != len / 2 ) {
                return LDAP_PROTOCOL_ERROR;
        }

        for ( i = 0; i < len; i += 2 ) {
                int hi = ykbind_modhex_value( input[i] );
                int lo = ykbind_modhex_value( input[i + 1] );

                if ( hi < 0 || lo < 0 ) {
                        return LDAP_INVALID_SYNTAX;
                }

                out[i / 2] = (unsigned char)((hi << 4) | lo);
        }

        return LDAP_SUCCESS;
}

static void
ykbind_hex_encode( const unsigned char *src, size_t len, char *dst )
{
        static const char hexdigits[] = "0123456789abcdef";
        size_t i;

        for ( i = 0; i < len; i++ ) {
                dst[(i * 2)] = hexdigits[(src[i] >> 4) & 0xF];
                dst[(i * 2) + 1] = hexdigits[src[i] & 0xF];
        }

        dst[len * 2] = '\0';
}

static int
ykbind_hex_decode( const char *src, size_t src_len, unsigned char *dst, size_t dst_len )
{
        size_t i;

        if ( src_len != dst_len * 2 ) {
                return LDAP_INVALID_SYNTAX;
        }

        for ( i = 0; i < src_len; i += 2 ) {
                unsigned char hi;
                unsigned char lo;

                if ( !isxdigit( (unsigned char)src[i] ) ||
                        !isxdigit( (unsigned char)src[i + 1] ) )
                {
                        return LDAP_INVALID_SYNTAX;
                }

                hi = (unsigned char)(isdigit( (unsigned char)src[i] ) ?
                        (src[i] - '0') : (tolower( (unsigned char)src[i] ) - 'a' + 10));
                lo = (unsigned char)(isdigit( (unsigned char)src[i + 1] ) ?
                        (src[i + 1] - '0') : (tolower( (unsigned char)src[i + 1] ) - 'a' + 10));
                dst[i / 2] = (unsigned char)((hi << 4) | lo);
        }

        return LDAP_SUCCESS;
}

static uint16_t
ykbind_crc16( const unsigned char *data, size_t len )
{
        uint16_t crc = 0xFFFFU;
        size_t i;
        int bit;

        for ( i = 0; i < len; i++ ) {
                crc ^= data[i];
                for ( bit = 0; bit < 8; bit++ ) {
                        if ( crc & 1 ) {
                                crc = (uint16_t)((crc >> 1) ^ 0x8408U);
                        } else {
                                crc >>= 1;
                        }
                }
        }

        return crc;
}

static int
ykbind_aes_decrypt( const unsigned char *key, const unsigned char *ciphertext, unsigned char *plaintext )
{
        EVP_CIPHER_CTX *ctx;
        int out_len = 0;
        int final_len = 0;
        int rc = LDAP_OTHER;

        ctx = EVP_CIPHER_CTX_new();
        if ( ctx == NULL ) {
                return rc;
        }

        if ( EVP_DecryptInit_ex( ctx, EVP_aes_128_ecb(), NULL, key, NULL ) != 1 ) {
                goto done;
        }

        EVP_CIPHER_CTX_set_padding( ctx, 0 );

        if ( EVP_DecryptUpdate( ctx, plaintext, &out_len, ciphertext, YKBIND_AES_KEY_BYTES ) != 1 ) {
                goto done;
        }

        if ( out_len != YKBIND_AES_KEY_BYTES ) {
                goto done;
        }

        if ( EVP_DecryptFinal_ex( ctx, plaintext + out_len, &final_len ) != 1 ) {
                goto done;
        }

        if ( out_len + final_len != YKBIND_AES_KEY_BYTES ) {
                goto done;
        }

        rc = LDAP_SUCCESS;

done:
        EVP_CIPHER_CTX_free( ctx );
        return rc;
}

static void
ykbind_parse_ticket( const unsigned char *plain, ykbind_ticket *ticket )
{
        memcpy( ticket->private_uid, plain, YKBIND_PRIVATE_UID_BYTES );
        ticket->use_ctr = (uint16_t)(plain[6] | ((uint16_t)plain[7] << 8));
        ticket->timestamp = (uint32_t)(plain[8] |
                ((uint32_t)plain[9] << 8) |
                ((uint32_t)plain[10] << 16));
        ticket->session_ctr = plain[11];
        ticket->rnd = (uint16_t)(plain[12] | ((uint16_t)plain[13] << 8));
        ticket->crc = (uint16_t)(plain[14] | ((uint16_t)plain[15] << 8));
}

static Attribute *
ykbind_find_attr( Entry *e, const char **names )
{
        Attribute *a;
        int i;

        for ( a = e->e_attrs; a; a = a->a_next ) {
                for ( i = 0; names[i] != NULL; i++ ) {
                        if ( strcasecmp( a->a_desc->ad_cname.bv_val, names[i] ) == 0 ) {
                                return a;
                        }
                }
        }

        return NULL;
}

static const struct berval *
ykbind_first_value( Entry *e, const char **names )
{
        Attribute *a = ykbind_find_attr( e, names );

        if ( a == NULL || a->a_numvals == 0 ) {
                return NULL;
        }

        return &a->a_nvals[0];
}

static int
ykbind_attr_is_true( Entry *e, const char **names, int *present )
{
        const struct berval *value = ykbind_first_value( e, names );
        static const struct berval bv_true = BER_BVC( "TRUE" );
        static const struct berval bv_yes = BER_BVC( "YES" );
        static const struct berval bv_on = BER_BVC( "ON" );
        static const struct berval bv_one = BER_BVC( "1" );

        if ( present != NULL ) {
                *present = (value != NULL);
        }

        if ( value == NULL ) {
                return 0;
        }

        if ( ber_bvstrcasecmp( value, &bv_true ) == 0 ||
                ber_bvstrcasecmp( value, &bv_yes ) == 0 ||
                ber_bvstrcasecmp( value, &bv_on ) == 0 ||
                ber_bvstrcasecmp( value, &bv_one ) == 0 )
        {
                return 1;
        }

        return 0;
}

static int
ykbind_parse_ulong_bv( const struct berval *value, int base, unsigned long *out )
{
        char buf[64];
        char *end = NULL;
        unsigned long tmp;

        if ( value == NULL || value->bv_len == 0 || out == NULL ||
                value->bv_len >= sizeof(buf) )
        {
                return LDAP_INVALID_SYNTAX;
        }

        memcpy( buf, value->bv_val, value->bv_len );
        buf[value->bv_len] = '\0';

        tmp = strtoul( buf, &end, base );
        if ( end == buf || (ber_len_t)(end - buf) != value->bv_len ) {
                return LDAP_INVALID_SYNTAX;
        }

        *out = tmp;
        return LDAP_SUCCESS;
}

static int
ykbind_parse_modify_timestamp( Entry *e, time_t *out )
{
const struct berval *value;
char buf[32];
struct tm tmv;

if ( out == NULL ) {
return LDAP_OTHER;
}

*out = 0;

value = ykbind_first_value( e, ykbind_attr_modify_timestamp );
if ( value == NULL || value->bv_len == 0 ||
value->bv_len >= sizeof(buf) )
{
return LDAP_NO_SUCH_ATTRIBUTE;
}

memset( &tmv, 0, sizeof(tmv) );
memcpy( buf, value->bv_val, value->bv_len );
buf[value->bv_len] = '\0';

if ( strptime( buf, "%Y%m%d%H%M%SZ", &tmv ) == NULL ) {
return LDAP_INVALID_SYNTAX;
}

*out = timegm( &tmv );
if ( *out == (time_t)-1 ) {
return LDAP_INVALID_SYNTAX;
}

return LDAP_SUCCESS;
}

static int
ykbind_load_previous_state(
        Entry *e,
        unsigned long *old_use_ctr,
        unsigned long *old_session_ctr,
        unsigned long *old_timestamp )
{
        const struct berval *value;
        int rc;

        *old_use_ctr = 0;
        *old_session_ctr = 0;
        *old_timestamp = 0;

        value = ykbind_first_value( e, ykbind_attr_last_use_ctr );
        if ( value != NULL ) {
                rc = ykbind_parse_ulong_bv( value, 10, old_use_ctr );
                if ( rc != LDAP_SUCCESS ) {
                        return rc;
                }
        }

        value = ykbind_first_value( e, ykbind_attr_last_session_ctr );
        if ( value != NULL ) {
                rc = ykbind_parse_ulong_bv( value, 10, old_session_ctr );
                if ( rc != LDAP_SUCCESS ) {
                        return rc;
                }
        }

        value = ykbind_first_value( e, ykbind_attr_last_timestamp );
        if ( value != NULL ) {
                rc = ykbind_parse_ulong_bv( value, 10, old_timestamp );
                if ( rc != LDAP_SUCCESS ) {
                        return rc;
                }
        }

        if ( *old_timestamp == 0 ) {
                value = ykbind_first_value( e, ykbind_attr_legacy_timestamp );
                if ( value != NULL ) {
                        rc = ykbind_parse_ulong_bv( value, 16, old_timestamp );
                        if ( rc != LDAP_SUCCESS ) {
                                return rc;
                        }
                }
        }

        if ( *old_use_ctr == 0 && *old_session_ctr == 0 ) {
                value = ykbind_first_value( e, ykbind_attr_legacy_counter );
                if ( value != NULL ) {
                        unsigned long combined = 0;

                        rc = ykbind_parse_ulong_bv( value, 16, &combined );
                        if ( rc != LDAP_SUCCESS ) {
                                return rc;
                        }

                        *old_use_ctr = (combined >> 8) & 0xFFFFUL;
                        *old_session_ctr = combined & 0xFFUL;
                }
        }

        return LDAP_SUCCESS;
}

static void
ykbind_format_legacy_state(
        const ykbind_ticket *ticket,
        char legacy_counter[7],
        char legacy_timestamp[7] )
{
        snprintf( legacy_counter, 7, "%04x%02x",
                (unsigned int)ticket->use_ctr,
                (unsigned int)ticket->session_ctr );
        snprintf( legacy_timestamp, 7, "%06x",
                (unsigned int)(ticket->timestamp & 0xFFFFFFU) );
}

static int
ykbind_resolve_ads( void )
{
        const char *text = NULL;
        int rc;

        if ( ad_yk_last_use_ctr == NULL ) {
                rc = slap_str2ad( "yubiKeyLastUseCtr", &ad_yk_last_use_ctr, &text );
                if ( rc != LDAP_SUCCESS ) return rc;
        }

        if ( ad_yk_last_session_ctr == NULL ) {
                rc = slap_str2ad( "yubiKeyLastSessionCtr", &ad_yk_last_session_ctr, &text );
                if ( rc != LDAP_SUCCESS ) return rc;
        }

        if ( ad_yk_last_timestamp == NULL ) {
                rc = slap_str2ad( "yubiKeyLastTimestamp", &ad_yk_last_timestamp, &text );
                if ( rc != LDAP_SUCCESS ) return rc;
        }

        if ( ad_yk_legacy_counter == NULL ) {
                rc = slap_str2ad( "yubiKeyLastCounter", &ad_yk_legacy_counter, &text );
                if ( rc != LDAP_SUCCESS ) return rc;
        }

        if ( ad_yk_legacy_timestamp == NULL ) {
                rc = slap_str2ad( "YKsessionTimestamp", &ad_yk_legacy_timestamp, &text );
                if ( rc != LDAP_SUCCESS ) return rc;
        }

        return LDAP_SUCCESS;
}

static int
ykbind_state_modify( Operation *op, ykbind_opctx *ctx )
{
        Operation op2 = *op;
        op2.o_msgid = 0;
        SlapReply rs2 = { REP_RESULT };
        BackendInfo *bi = op2.o_bd->bd_info;
        Modifications m_use = {0};
        Modifications m_sess = {0};
        Modifications m_ts = {0};
        Modifications m_legacy_ctr = {0};
        Modifications m_legacy_ts = {0};
        struct berval use_vals[2] = { BER_BVNULL, BER_BVNULL };
        struct berval sess_vals[2] = { BER_BVNULL, BER_BVNULL };
        struct berval ts_vals[2] = { BER_BVNULL, BER_BVNULL };
        struct berval legacy_ctr_vals[2] = { BER_BVNULL, BER_BVNULL };
        struct berval legacy_ts_vals[2] = { BER_BVNULL, BER_BVNULL };
        char use_buf[32];
        char sess_buf[32];
        char ts_buf[32];
        int rc;

        rc = ykbind_resolve_ads();
        if ( rc != LDAP_SUCCESS ) {
                Debug( LDAP_DEBUG_ANY,
                        "%s: schema attributes are not available yet\n",
                        ykbind.on_bi.bi_type );
                return rc;
        }

        if ( ctx->chained_auth ) {
                Debug( LDAP_DEBUG_TRACE,
                        "%s: chained auth accepted, not updating replay state for %s\n",
                        ykbind.on_bi.bi_type,
                        ctx->req_ndn.bv_val ? ctx->req_ndn.bv_val : "(unknown)" );
                return LDAP_SUCCESS;
        }


        snprintf( use_buf, sizeof(use_buf), "%lu", ctx->use_ctr );
        snprintf( sess_buf, sizeof(sess_buf), "%lu", ctx->session_ctr );
        snprintf( ts_buf, sizeof(ts_buf), "%lu", ctx->timestamp );

        ber_str2bv( use_buf, 0, 0, &use_vals[0] );
        ber_str2bv( sess_buf, 0, 0, &sess_vals[0] );
        ber_str2bv( ts_buf, 0, 0, &ts_vals[0] );
        ber_str2bv( ctx->legacy_counter, 0, 0, &legacy_ctr_vals[0] );
        ber_str2bv( ctx->legacy_timestamp, 0, 0, &legacy_ts_vals[0] );

        m_use.sml_op = LDAP_MOD_REPLACE;
        m_use.sml_flags = SLAP_MOD_INTERNAL;
        m_use.sml_desc = ad_yk_last_use_ctr;
        m_use.sml_values = use_vals;
        m_use.sml_nvalues = NULL;
        m_use.sml_numvals = 1;
        m_use.sml_next = &m_sess;

        m_sess.sml_op = LDAP_MOD_REPLACE;
        m_sess.sml_flags = SLAP_MOD_INTERNAL;
        m_sess.sml_desc = ad_yk_last_session_ctr;
        m_sess.sml_values = sess_vals;
        m_sess.sml_nvalues = NULL;
        m_sess.sml_numvals = 1;
        m_sess.sml_next = &m_ts;

        m_ts.sml_op = LDAP_MOD_REPLACE;
        m_ts.sml_flags = SLAP_MOD_INTERNAL;
        m_ts.sml_desc = ad_yk_last_timestamp;
        m_ts.sml_values = ts_vals;
        m_ts.sml_nvalues = NULL;
        m_ts.sml_numvals = 1;
        m_ts.sml_next = &m_legacy_ctr;

        m_legacy_ctr.sml_op = LDAP_MOD_REPLACE;
        m_legacy_ctr.sml_flags = SLAP_MOD_INTERNAL;
        m_legacy_ctr.sml_desc = ad_yk_legacy_counter;
        m_legacy_ctr.sml_values = legacy_ctr_vals;
        m_legacy_ctr.sml_nvalues = NULL;
        m_legacy_ctr.sml_numvals = 1;
        m_legacy_ctr.sml_next = &m_legacy_ts;

        m_legacy_ts.sml_op = LDAP_MOD_REPLACE;
        m_legacy_ts.sml_flags = SLAP_MOD_INTERNAL;
        m_legacy_ts.sml_desc = ad_yk_legacy_timestamp;
        m_legacy_ts.sml_values = legacy_ts_vals;
        m_legacy_ts.sml_nvalues = NULL;
        m_legacy_ts.sml_numvals = 1;
        m_legacy_ts.sml_next = NULL;

        op2.o_tag = LDAP_REQ_MODIFY;
        op2.o_callback = NULL;
        op2.orm_modlist = &m_use;
        op2.o_req_dn = ctx->req_dn;
        op2.o_req_ndn = ctx->req_ndn;
        op2.o_dn = op->o_bd->be_rootdn;
        op2.o_ndn = op->o_bd->be_rootndn;

        ykbind_set_backend( &op2, op, ctx->on );

        if ( op->o_bd->be_modify == NULL ) {
                op2.o_bd->bd_info = bi;
                return LDAP_UNWILLING_TO_PERFORM;
        }

        rc = op->o_bd->be_modify( &op2, &rs2 );
        op2.o_bd->bd_info = bi;

        if ( rc == SLAP_CB_CONTINUE ) {
                rc = rs2.sr_err;
        }

        if ( rc == LDAP_SUCCESS ) {
                rc = rs2.sr_err;
        }

        return rc;
}

static void
ykbind_zero_free_cred( struct berval *bv )
{
        if ( bv->bv_val != NULL ) {
                OPENSSL_cleanse( bv->bv_val, bv->bv_len );
                ch_free( bv->bv_val );
                bv->bv_val = NULL;
                bv->bv_len = 0;
        }
}

static int
ykbind_op_cleanup( Operation *op, SlapReply *rs )
{
        slap_callback *cb = op->o_callback;
        ykbind_opctx *ctx;

        (void)rs;

        if ( cb == NULL ) {
                return 0;
        }

        ctx = (ykbind_opctx *)cb->sc_private;
        op->o_callback = cb->sc_next;

        if ( ctx != NULL ) {
                op->orb_cred = ctx->orig_cred;
                ykbind_zero_free_cred( &ctx->stripped_cred );
                ch_free( ctx );
        }

        ch_free( cb );
        return 0;
}

static int
ykbind_bind_response( Operation *op, SlapReply *rs )
{
        ykbind_opctx *ctx = op->o_callback != NULL ?
                (ykbind_opctx *)op->o_callback->sc_private : NULL;
        int rc;

        if ( ctx == NULL ) {
                return SLAP_CB_CONTINUE;
        }

        if ( rs->sr_type != REP_RESULT || rs->sr_err != LDAP_SUCCESS ) {
                return SLAP_CB_CONTINUE;
        }

        rc = ykbind_state_modify( op, ctx );
        if ( rc != LDAP_SUCCESS ) {
                Debug( LDAP_DEBUG_ANY,
                        "%s: failed to persist YubiKey replay state for %s (%d)\n",
                        ykbind.on_bi.bi_type,
                        ctx->req_ndn.bv_val ? ctx->req_ndn.bv_val : "(unknown)",
                        rc );
                return SLAP_CB_CONTINUE;
        }

        return SLAP_CB_CONTINUE;
}

static int
ykbind_simple_bind( Operation *op, SlapReply *rs )
{
        slap_overinst *on = (slap_overinst *)op->o_bd->bd_info;
        Entry *e = NULL;
        const struct berval *private_uid_bv;
        const struct berval *aes_key_bv;
        ykbind_opctx *ctx = NULL;
        slap_callback *cb = NULL;
        ykbind_validate_otp_result val_result;
        int enabled_present = 0;
        int require_otp = 0;
        int rc;
        static const struct berval bv_nokey = BER_BVC( "NOKEY" );

        if ( op->orb_method != LDAP_AUTH_SIMPLE ) {
                return SLAP_CB_CONTINUE;
        }

        if ( BER_BVISEMPTY( &op->o_req_ndn ) ) {
                return SLAP_CB_CONTINUE;
        }

        rc = ykbind_entry_get( op, on, &e );
        if ( rc != LDAP_SUCCESS || e == NULL ) {
                return SLAP_CB_CONTINUE;
        }

        private_uid_bv = ykbind_first_value( e, ykbind_attr_private_uid );
        aes_key_bv = ykbind_first_value( e, ykbind_attr_aes_key );

        if ( ykbind_attr_is_true( e, ykbind_attr_enabled, &enabled_present ) ) {
                require_otp = 1;
        } else if ( enabled_present ) {
                require_otp = 0;
        } else if ( private_uid_bv != NULL && aes_key_bv != NULL &&
                ber_bvstrcasecmp( private_uid_bv, &bv_nokey ) != 0 )
        {
                require_otp = 1;
        }

        if ( !require_otp ) {
                ykbind_entry_release( op, on, e );
                return SLAP_CB_CONTINUE;
        }

        rc = ykbind_validate_otp( op, on, e, &op->orb_cred, &val_result );
        if ( rc != LDAP_SUCCESS ) {
                ykbind_entry_release( op, on, e );
                rs->sr_err = LDAP_INVALID_CREDENTIALS;
                rs->sr_text = "invalid credentials";
                send_ldap_result( op, rs );
                return rs->sr_err;
        }

        ctx = ch_calloc( 1, sizeof(ykbind_opctx) );
        cb = ch_calloc( 1, sizeof(slap_callback) );
        if ( ctx == NULL || cb == NULL ) {
                ykbind_zero_free_cred( &val_result.password );
                ykbind_entry_release( op, on, e );
                if ( cb != NULL ) {
                        ch_free( cb );
                }
                if ( ctx != NULL ) {
                        ch_free( ctx );
                }
                rs->sr_err = LDAP_INVALID_CREDENTIALS;
                rs->sr_text = "invalid credentials";
                send_ldap_result( op, rs );
                return rs->sr_err;
        }

        ctx->on = on;
        ctx->orig_cred = op->orb_cred;
        ctx->stripped_cred = val_result.password;
        ctx->req_ndn = op->o_req_ndn;
        ctx->req_dn = op->o_req_dn;
        ctx->use_ctr = val_result.ticket.use_ctr;
        ctx->session_ctr = val_result.ticket.session_ctr;
        ctx->timestamp = val_result.ticket.timestamp;
        ctx->chained_auth = val_result.chained_auth;
        memcpy( ctx->legacy_counter, val_result.legacy_counter, 7 );
        memcpy( ctx->legacy_timestamp, val_result.legacy_timestamp, 7 );

        cb->sc_next = op->o_callback;
        cb->sc_response = ykbind_bind_response;
        cb->sc_cleanup = ykbind_op_cleanup;
        cb->sc_private = ctx;
        op->o_callback = cb;
        op->orb_cred = val_result.password;
        val_result.password.bv_val = NULL;
        val_result.password.bv_len = 0;

        ykbind_entry_release( op, on, e );
        return SLAP_CB_CONTINUE;
}

#ifndef LDAP_EXOP_PASSWD_MODIFY
#define LDAP_EXOP_PASSWD_MODIFY "1.3.6.1.4.1.4203.1.11.1"
#endif

static void
ykbind_passwd_req_cleanup( ykbind_passwd_req *req )
{
        if ( req->has_user_dn && req->user_dn.bv_val != NULL ) {
                ch_free( req->user_dn.bv_val );
                req->user_dn.bv_val = NULL;
                req->user_dn.bv_len = 0;
        }
        if ( req->has_old_passwd && req->old_passwd.bv_val != NULL ) {
                OPENSSL_cleanse( req->old_passwd.bv_val, req->old_passwd.bv_len );
                ch_free( req->old_passwd.bv_val );
                req->old_passwd.bv_val = NULL;
                req->old_passwd.bv_len = 0;
        }
        if ( req->has_new_passwd && req->new_passwd.bv_val != NULL ) {
                OPENSSL_cleanse( req->new_passwd.bv_val, req->new_passwd.bv_len );
                ch_free( req->new_passwd.bv_val );
                req->new_passwd.bv_val = NULL;
                req->new_passwd.bv_len = 0;
        }
}

static int
ykbind_parse_passwd_modify_request(
        Operation *op,
        ykbind_passwd_req *req )
{
        BerElementBuffer berbuf;
        BerElement *ber = (BerElement *)&berbuf;
        ber_tag_t tag;
        ber_len_t len;
        int rc;

        memset( req, 0, sizeof(*req) );

        if ( op->ore_reqdata == NULL || op->ore_reqdata->bv_val == NULL || op->ore_reqdata->bv_len == 0 ) {
                return LDAP_PROTOCOL_ERROR;
        }

        ber_init2( ber, op->ore_reqdata, LBER_USE_DER );

        tag = ber_scanf( ber, "{" );
        if ( tag == LBER_ERROR ) {
                return LDAP_PROTOCOL_ERROR;
        }

        for ( ;; ) {
                tag = ber_peek_tag( ber, &len );
                if ( tag == LBER_DEFAULT ) {
                        break;
                }

                if ( tag == LBER_ERROR ) {
                        ykbind_passwd_req_cleanup( req );
                        return LDAP_PROTOCOL_ERROR;
                }

                switch ( tag ) {
                case 0x80U: {
                        struct berval bv;
                        tag = ber_scanf( ber, "m", &bv );
                        if ( tag == LBER_ERROR ) {
                                ykbind_passwd_req_cleanup( req );
                                return LDAP_PROTOCOL_ERROR;
                        }
                        req->user_dn.bv_val = ch_malloc( bv.bv_len + 1 );
                        if ( req->user_dn.bv_val == NULL ) {
                                ykbind_passwd_req_cleanup( req );
                                return LDAP_OTHER;
                        }
                        memcpy( req->user_dn.bv_val, bv.bv_val, bv.bv_len );
                        req->user_dn.bv_val[bv.bv_len] = '\0';
                        req->user_dn.bv_len = bv.bv_len;
                        req->has_user_dn = 1;
                        break;
                }
                case 0x81U: {
                        struct berval bv;
                        tag = ber_scanf( ber, "m", &bv );
                        if ( tag == LBER_ERROR ) {
                                ykbind_passwd_req_cleanup( req );
                                return LDAP_PROTOCOL_ERROR;
                        }
                        req->old_passwd.bv_val = ch_malloc( bv.bv_len + 1 );
                        if ( req->old_passwd.bv_val == NULL ) {
                                ykbind_passwd_req_cleanup( req );
                                return LDAP_OTHER;
                        }
                        memcpy( req->old_passwd.bv_val, bv.bv_val, bv.bv_len );
                        req->old_passwd.bv_val[bv.bv_len] = '\0';
                        req->old_passwd.bv_len = bv.bv_len;
                        req->has_old_passwd = 1;
                        break;
                }
                case 0x82U: {
                        struct berval bv;
                        tag = ber_scanf( ber, "m", &bv );
                        if ( tag == LBER_ERROR ) {
                                ykbind_passwd_req_cleanup( req );
                                return LDAP_PROTOCOL_ERROR;
                        }
                        req->new_passwd.bv_val = ch_malloc( bv.bv_len + 1 );
                        if ( req->new_passwd.bv_val == NULL ) {
                                ykbind_passwd_req_cleanup( req );
                                return LDAP_OTHER;
                        }
                        memcpy( req->new_passwd.bv_val, bv.bv_val, bv.bv_len );
                        req->new_passwd.bv_val[bv.bv_len] = '\0';
                        req->new_passwd.bv_len = bv.bv_len;
                        req->has_new_passwd = 1;
                        break;
                }
                default:
                        tag = ber_scanf( ber, "x" );
                        if ( tag == LBER_ERROR ) {
                                ykbind_passwd_req_cleanup( req );
                                return LDAP_PROTOCOL_ERROR;
                        }
                        break;
                }
        }

        tag = ber_scanf( ber, "}" );
        if ( tag == LBER_ERROR ) {
                ykbind_passwd_req_cleanup( req );
                return LDAP_PROTOCOL_ERROR;
        }

        return LDAP_SUCCESS;
}

static int
ykbind_rebuild_passwd_modify_request(
        Operation *op,
        ykbind_passwd_req *req,
        const struct berval *stripped_old_passwd,
        struct berval *out_bv )
{
        BerElementBuffer berbuf;
        BerElement *ber = (BerElement *)&berbuf;
        struct berval bv_out = BER_BVNULL;
        int rc;

        (void)op;

        ber_init2( ber, NULL, LBER_USE_DER );

        rc = ber_printf( ber, "{" );
        if ( rc == -1 ) {
                return LDAP_OTHER;
        }

        if ( req->has_user_dn ) {
                rc = ber_printf( ber, "to", 0x80U, req->user_dn.bv_val, req->user_dn.bv_len );
                if ( rc == -1 ) {
                        return LDAP_OTHER;
                }
        }

        if ( req->has_old_passwd ) {
                rc = ber_printf( ber, "to", 0x81U, stripped_old_passwd->bv_val, stripped_old_passwd->bv_len );
                if ( rc == -1 ) {
                        return LDAP_OTHER;
                }
        }

        if ( req->has_new_passwd ) {
                rc = ber_printf( ber, "to", 0x82U, req->new_passwd.bv_val, req->new_passwd.bv_len );
                if ( rc == -1 ) {
                        return LDAP_OTHER;
                }
        }

        rc = ber_printf( ber, "}" );
        if ( rc == -1 ) {
                return LDAP_OTHER;
        }

        rc = ber_flatten2( ber, &bv_out, 1 );
        if ( rc == -1 ) {
                return LDAP_OTHER;
        }

        out_bv->bv_val = bv_out.bv_val;
        out_bv->bv_len = bv_out.bv_len;

        ber_free_buf( ber );

        return LDAP_SUCCESS;
}

static int
ykbind_exop_cleanup( Operation *op, SlapReply *rs )
{
        slap_callback *cb = op->o_callback;
        ykbind_exop_ctx *ctx;

        (void)rs;

        if ( cb == NULL ) {
                return 0;
        }

        ctx = (ykbind_exop_ctx *)cb->sc_private;
        op->o_callback = cb->sc_next;

        if ( ctx != NULL ) {
                if ( ctx->new_reqdata.bv_val != NULL ) {
                        OPENSSL_cleanse( ctx->new_reqdata.bv_val, ctx->new_reqdata.bv_len );
                        ch_free( ctx->new_reqdata.bv_val );
                }
                if ( ctx->allocated_target ) {
                        ch_free( ctx->req_ndn.bv_val );
                        ch_free( ctx->req_dn.bv_val );
                }
                ch_free( ctx );
        }

        ch_free( cb );
        return 0;
}

static int
ykbind_exop_response( Operation *op, SlapReply *rs )
{
        ykbind_exop_ctx *ctx = op->o_callback != NULL ?
                (ykbind_exop_ctx *)op->o_callback->sc_private : NULL;
        ykbind_opctx bind_ctx;
        int rc;

        if ( ctx == NULL ) {
                return SLAP_CB_CONTINUE;
        }

        if ( rs->sr_type != REP_RESULT || rs->sr_err != LDAP_SUCCESS ) {
                return SLAP_CB_CONTINUE;
        }

        if ( ctx->chained_auth ) {
                Debug( LDAP_DEBUG_TRACE,
                        "%s: chained auth accepted, not updating replay state for %s\n",
                        ykbind.on_bi.bi_type,
                        ctx->req_ndn.bv_val ? ctx->req_ndn.bv_val : "(unknown)" );
                return SLAP_CB_CONTINUE;
        }

        memset( &bind_ctx, 0, sizeof(bind_ctx) );
        bind_ctx.on = ctx->on;
        bind_ctx.req_ndn = ctx->req_ndn;
        bind_ctx.req_dn = ctx->req_dn;
        bind_ctx.use_ctr = ctx->use_ctr;
        bind_ctx.session_ctr = ctx->session_ctr;
        bind_ctx.timestamp = ctx->timestamp;
        bind_ctx.chained_auth = ctx->chained_auth;
        memcpy( bind_ctx.legacy_counter, ctx->legacy_counter, 7 );
        memcpy( bind_ctx.legacy_timestamp, ctx->legacy_timestamp, 7 );

        rc = ykbind_state_modify( op, &bind_ctx );
        if ( rc != LDAP_SUCCESS ) {
                Debug( LDAP_DEBUG_ANY,
                        "%s: failed to persist YubiKey replay state for %s (%d)\n",
                        ykbind.on_bi.bi_type,
                        ctx->req_ndn.bv_val ? ctx->req_ndn.bv_val : "(unknown)",
                        rc );
                return SLAP_CB_CONTINUE;
        }

        return SLAP_CB_CONTINUE;
}

static int
ykbind_extended_op( Operation *op, SlapReply *rs )
{
        slap_overinst *on = (slap_overinst *)op->o_bd->bd_info;
        ykbind_passwd_req req = {0};
        ykbind_validate_otp_result val_result;
        Entry *e = NULL;
        struct berval target_ndn = BER_BVNULL;
        struct berval target_dn = BER_BVNULL;
        struct berval saved_req_ndn;
        struct berval saved_req_dn;
        int require_otp = 0;
        int enabled_present = 0;
        const struct berval *private_uid_bv;
        const struct berval *aes_key_bv;
        ykbind_exop_ctx *ctx = NULL;
        slap_callback *cb = NULL;
        int rc;
        int allocated_target = 0;
        static const struct berval bv_nokey = BER_BVC( "NOKEY" );

        if ( BER_BVISEMPTY( &op->ore_reqoid ) ||
                strcmp( op->ore_reqoid.bv_val, LDAP_EXOP_PASSWD_MODIFY ) != 0 )
        {
                return SLAP_CB_CONTINUE;
        }

        rc = ykbind_parse_passwd_modify_request( op, &req );
        if ( rc != LDAP_SUCCESS ) {
                return SLAP_CB_CONTINUE;
        }

        if ( !req.has_old_passwd ) {
                ykbind_passwd_req_cleanup( &req );
                return SLAP_CB_CONTINUE;
        }

        if ( req.has_user_dn && req.user_dn.bv_val != NULL ) {
                rc = dnPrettyNormal( NULL, &req.user_dn, &target_dn, &target_ndn, NULL );
                if ( rc != LDAP_SUCCESS ) {
                        ykbind_passwd_req_cleanup( &req );
                        return SLAP_CB_CONTINUE;
                }
                allocated_target = 1;
        } else {
                target_ndn = op->o_ndn;
                target_dn = op->o_dn;
        }

        if ( BER_BVISEMPTY( &target_ndn ) ) {
                ykbind_passwd_req_cleanup( &req );
                if ( allocated_target ) {
                        ch_free( target_ndn.bv_val );
                        ch_free( target_dn.bv_val );
                }
                return SLAP_CB_CONTINUE;
        }

        saved_req_ndn = op->o_req_ndn;
        saved_req_dn = op->o_req_dn;
        op->o_req_ndn = target_ndn;
        op->o_req_dn = target_dn;

        rc = ykbind_entry_get( op, on, &e );
        if ( rc != LDAP_SUCCESS || e == NULL ) {
                op->o_req_ndn = saved_req_ndn;
                op->o_req_dn = saved_req_dn;
                ykbind_passwd_req_cleanup( &req );
                if ( allocated_target ) {
                        ch_free( target_ndn.bv_val );
                        ch_free( target_dn.bv_val );
                }
                return SLAP_CB_CONTINUE;
        }

        private_uid_bv = ykbind_first_value( e, ykbind_attr_private_uid );
        aes_key_bv = ykbind_first_value( e, ykbind_attr_aes_key );

        if ( ykbind_attr_is_true( e, ykbind_attr_enabled, &enabled_present ) ) {
                require_otp = 1;
        } else if ( enabled_present ) {
                require_otp = 0;
        } else if ( private_uid_bv != NULL && aes_key_bv != NULL &&
                ber_bvstrcasecmp( private_uid_bv, &bv_nokey ) != 0 )
        {
                require_otp = 1;
        }

        if ( !require_otp ) {
                ykbind_entry_release( op, on, e );
                op->o_req_ndn = saved_req_ndn;
                op->o_req_dn = saved_req_dn;
                ykbind_passwd_req_cleanup( &req );
                if ( allocated_target ) {
                        ch_free( target_ndn.bv_val );
                        ch_free( target_dn.bv_val );
                }
                return SLAP_CB_CONTINUE;
        }

        rc = ykbind_validate_otp( op, on, e, &req.old_passwd, &val_result );
        if ( rc != LDAP_SUCCESS ) {
                ykbind_entry_release( op, on, e );
                op->o_req_ndn = saved_req_ndn;
                op->o_req_dn = saved_req_dn;
                ykbind_passwd_req_cleanup( &req );
                if ( allocated_target ) {
                        ch_free( target_ndn.bv_val );
                        ch_free( target_dn.bv_val );
                }
                rs->sr_err = LDAP_INVALID_CREDENTIALS;
                rs->sr_text = "invalid credentials";
                send_ldap_result( op, rs );
                return rs->sr_err;
        }

        {
                struct berval old_stripped = val_result.password;
                rc = ykbind_rebuild_passwd_modify_request( op, &req, &old_stripped, &val_result.password );
                ykbind_zero_free_cred( &old_stripped );
        }
        if ( rc != LDAP_SUCCESS ) {
                ykbind_zero_free_cred( &val_result.password );
                ykbind_entry_release( op, on, e );
                op->o_req_ndn = saved_req_ndn;
                op->o_req_dn = saved_req_dn;
                ykbind_passwd_req_cleanup( &req );
                if ( allocated_target ) {
                        ch_free( target_ndn.bv_val );
                        ch_free( target_dn.bv_val );
                }
                rs->sr_err = LDAP_OTHER;
                rs->sr_text = "internal error";
                send_ldap_result( op, rs );
                return rs->sr_err;
        }

        ctx = ch_calloc( 1, sizeof(ykbind_exop_ctx) );
        cb = ch_calloc( 1, sizeof(slap_callback) );
        if ( ctx == NULL || cb == NULL ) {
                ykbind_zero_free_cred( &val_result.password );
                ykbind_entry_release( op, on, e );
                op->o_req_ndn = saved_req_ndn;
                op->o_req_dn = saved_req_dn;
                ykbind_passwd_req_cleanup( &req );
                if ( allocated_target ) {
                        ch_free( target_ndn.bv_val );
                        ch_free( target_dn.bv_val );
                }
                if ( cb != NULL ) {
                        ch_free( cb );
                }
                if ( ctx != NULL ) {
                        ch_free( ctx );
                }
                rs->sr_err = LDAP_OTHER;
                rs->sr_text = "internal error";
                send_ldap_result( op, rs );
                return rs->sr_err;
        }

        ctx->on = on;
        if ( op->ore_reqdata != NULL ) {
                ctx->orig_reqdata = *op->ore_reqdata;
        } else {
                BER_BVZERO( &ctx->orig_reqdata );
        }
        ctx->new_reqdata = val_result.password;
        ctx->req_ndn = target_ndn;
        ctx->req_dn = target_dn;
        ctx->use_ctr = val_result.ticket.use_ctr;
        ctx->session_ctr = val_result.ticket.session_ctr;
        ctx->timestamp = val_result.ticket.timestamp;
        ctx->chained_auth = val_result.chained_auth;
        ctx->allocated_target = allocated_target;
        memcpy( ctx->legacy_counter, val_result.legacy_counter, 7 );
        memcpy( ctx->legacy_timestamp, val_result.legacy_timestamp, 7 );

        cb->sc_next = op->o_callback;
        cb->sc_response = ykbind_exop_response;
        cb->sc_cleanup = ykbind_exop_cleanup;
        cb->sc_private = ctx;
        op->o_callback = cb;
        op->ore_reqdata = &ctx->new_reqdata;

        ykbind_entry_release( op, on, e );
        op->o_req_ndn = saved_req_ndn;
        op->o_req_dn = saved_req_dn;

        ykbind_passwd_req_cleanup( &req );
        val_result.password.bv_val = NULL;
        val_result.password.bv_len = 0;

        return SLAP_CB_CONTINUE;
}

static int
ykbind_db_init( BackendDB *be, ConfigReply *cr )
{
        (void)be;
        (void)cr;
        return LDAP_SUCCESS;
}

static int
ykbind_db_destroy( BackendDB *be, ConfigReply *cr )
{
        (void)be;
        (void)cr;
        return LDAP_SUCCESS;
}

static int
ykbind_initialize( void )
{
        ykbind.on_bi.bi_type = "ykbind";
        ykbind.on_bi.bi_db_init = ykbind_db_init;
        ykbind.on_bi.bi_db_destroy = ykbind_db_destroy;
        ykbind.on_bi.bi_op_bind = ykbind_simple_bind;
        ykbind.on_bi.bi_extended = ykbind_extended_op;

        return overlay_register( &ykbind );
}

int
init_module( int argc, char *argv[] )
{
        (void)argc;
        (void)argv;
        return ykbind_initialize();
}

