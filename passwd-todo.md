# Password Change Fix — Implementation TODO

## Problem

The `ykbind` overlay only hooks `bi_op_bind`. It does not intercept the LDAP Password Modify Extended Operation (RFC 3062, OID `1.3.6.1.4.1.4203.1.11.1`). When nslcd sends a password change via this extended operation, `oldPasswd` still contains the OTP suffix. The server compares `passwordOTP` against the stored `userPassword` hash (which is just the plain password) → error 49.

The chained auth grace window in `slapo-ykbind-new.c` does not solve this because it only allows re-bind with the same OTP within 60 seconds — it does not intercept the extended operation.

## Current State

- `slapo-ykbind.c` — original overlay, bind-only
- `slapo-ykbind-new.c` — current deployed version, adds chained auth grace window (60s), still bind-only
- Neither file has a `bi_op_extended` hook

## Implementation Plan

### Step 1: Refactor OTP validation into a reusable function

Extract the core OTP validation logic from `ykbind_simple_bind()` into a standalone function that both bind and extended-op handlers can call.

```c
static int
ykbind_validate_otp(
    Operation *op,
    Entry *e,
    const struct berval *cred,
    struct berval *out_password,
    ykbind_ticket *out_ticket,
    int *out_chained_auth )
```

This function should:
- Call `ykbind_split_password_otp()` to split credential into password + OTP
- Validate modhex format
- Check public ID match
- Hex-decode AES key
- Modhex-decode ciphertext
- AES-128-ECB decrypt
- CRC-16 check
- Private UID match
- Load previous state, check replay counters
- Determine chained auth grace window
- Return the stripped password and ticket data via output params

### Step 2: Refactor `ykbind_simple_bind()` to use the new function

Replace the inline OTP validation code in `ykbind_simple_bind()` with a call to `ykbind_validate_otp()`. The bind handler should behave identically to the current implementation.

### Step 3: Implement BER parse for PasswdModifyRequestValue

The Password Modify Extended Operation carries a `PasswdModifyRequestValue` SEQUENCE:

```
PasswdModifyRequestValue ::= SEQUENCE {
    userIdentity [0] OCTET STRING OPTIONAL,
    oldPasswd    [1] OCTET STRING OPTIONAL,
    newPasswd    [2] OCTET STRING OPTIONAL
}
```

Implement a parser:

```c
typedef struct ykbind_passwd_req {
    struct berval user_dn;
    struct berval old_passwd;
    struct berval new_passwd;
    int has_user_dn;
    int has_old_passwd;
    int has_new_passwd;
} ykbind_passwd_req;

static int
ykbind_parse_passwd_modify_request(
    Operation *op,
    ykbind_passwd_req *req )
```

Use `ber_scanf()` with context tags `[0]`, `[1]`, `[2]` to parse the optional fields from `op->ore_reqdata`.

### Step 4: Implement BER rebuild with modified oldPasswd

After stripping the OTP from `oldPasswd`, rebuild the `PasswdModifyRequestValue` BER structure with the stripped password. This is needed because `op->ore_reqdata` is read-only.

```c
static int
ykbind_rebuild_passwd_modify_request(
    Operation *op,
    ykbind_passwd_req *req,
    const struct berval *stripped_old_passwd )
```

This function should:
- Allocate a new `BerElement`
- Encode the SEQUENCE with the same fields, but `oldPasswd` replaced with the stripped version
- Store the new encoded data so the next handler can use it
- The tricky part: OpenLDAP's extended op handler reads from `op->ore_reqdata`, so we may need to temporarily replace it or use a callback approach

**Key consideration:** OpenLDAP's `slapd` processes the Password Modify extended operation in `servers/slapd/passwd.c` (the `passwd_modify` function). The overlay needs to intercept before that handler runs, strip the OTP from `oldPasswd`, and let the normal handler proceed with the clean password.

### Step 5: Implement `ykbind_extended_op()`

```c
static int
ykbind_extended_op( Operation *op, SlapReply *rs )
{
    slap_overinst *on = (slap_overinst *)op->o_bd->bd_info;
    ykbind_passwd_req req = {0};
    struct berval password = BER_BVNULL;
    struct berval otp = BER_BVNULL;
    Entry *e = NULL;
    int require_otp = 0;
    int rc;

    /* 1. Check OID — only handle Password Modify */
    if ( op->ore_reqdata.bv_val == NULL ||
         strcmp( op->ore_reqdata.bv_val, LDAP_EXOP_PASSWD_MODIFY ) != 0 )
    {
        return SLAP_CB_CONTINUE;
    }

    /* 2. Parse the request */
    rc = ykbind_parse_passwd_modify_request( op, &req );
    if ( rc != LDAP_SUCCESS ) {
        return SLAP_CB_CONTINUE;
    }

    /* 3. If no oldPasswd, nothing to strip — pass through */
    if ( !req.has_old_passwd ) {
        return SLAP_CB_CONTINUE;
    }

    /* 4. Determine target user DN */
    struct berval target_ndn;
    if ( req.has_user_dn && req.user_dn.bv_val != NULL ) {
        /* Use the explicitly specified userIdentity */
        rc = dnPrettyNormal( NULL, &req.user_dn, &target_ndn, NULL, NULL );
        if ( rc != LDAP_SUCCESS ) {
            return SLAP_CB_CONTINUE;
        }
    } else {
        /* Default: the bound user */
        target_ndn = op->o_ndn;
    }

    /* 5. Look up the target user entry */
    /* Need to temporarily set o_req_ndn for the overlay entry lookup */
    struct berval saved_ndn = op->o_req_ndn;
    struct berval saved_dn = op->o_req_dn;
    op->o_req_ndn = target_ndn;
    /* ... get entry ... */
    op->o_req_ndn = saved_ndn;
    op->o_req_dn = saved_dn;

    /* 6. Check if YubiKey is required for this user */
    require_otp = ykbind_check_require_otp( e );
    if ( !require_otp ) {
        ykbind_entry_release( op, on, e );
        return SLAP_CB_CONTINUE;
    }

    /* 7. Split oldPasswd into password + OTP */
    const char *reason = NULL;
    rc = ykbind_split_password_otp( op, &req.old_passwd, &password, &otp,
        &reason, NULL );
    if ( rc != LDAP_SUCCESS ) {
        ykbind_entry_release( op, on, e );
        rs->sr_err = LDAP_INVALID_CREDENTIALS;
        send_ldap_result( op, rs );
        return rs->sr_err;
    }

    /* 8. Validate OTP (reuse refactored function) */
    /* ... validate modhex, AES, CRC, UID, counters ... */
    /* On failure: deny with error 49 */

    /* 9. Replace oldPasswd in the request with the stripped password */
    rc = ykbind_rebuild_passwd_modify_request( op, &req, &password );

    /* 10. Clean up */
    ykbind_zero_free_cred( &password );
    ykbind_entry_release( op, on, e );

    /* 11. Pass to next handler with the modified request */
    return SLAP_CB_CONTINUE;
}
```

### Step 6: Register `bi_op_extended` in `ykbind_initialize()`

```c
static int
ykbind_initialize( void )
{
    ykbind.on_bi.bi_type = "ykbind";
    ykbind.on_bi.bi_db_init = ykbind_db_init;
    ykbind.on_bi.bi_db_destroy = ykbind_db_destroy;
    ykbind.on_bi.bi_op_bind = ykbind_simple_bind;
    ykbind.on_bi.bi_op_extended = ykbind_extended_op;  /* <-- ADD THIS */

    return overlay_register( &ykbind );
}
```

## Critical Implementation Details

### BER Rebuild Strategy

The hardest part is modifying `op->ore_reqdata`. Options:

**Option A: Modify `op->ore_reqdata` in-place**
- Allocate a new `struct berval` with the rebuilt BER data
- Save the original `op->ore_reqdata`
- Replace it with the new data
- Install a callback to restore the original after the operation completes
- **Risk:** Must ensure the new data lives long enough and is freed properly

**Option B: Use a response callback**
- Install a `sc_response` callback (like the bind flow does)
- In the callback, after the operation succeeds, update replay state
- The actual `oldPasswd` modification must happen before the handler runs
- **This is the same pattern used for bind**

**Option C: Direct `op->ore_reqdata` replacement**
- Parse the original BER
- Build a new BER with stripped `oldPasswd`
- Set `op->ore_reqdata` to the new BER data
- Install cleanup callback to free the new data
- **Most straightforward but requires careful memory management**

**Recommended: Option C** — it mirrors how the bind flow replaces `op->orb_cred`.

### OpenLDAP Header Requirements

The following constants/functions are needed:
- `LDAP_EXOP_PASSWD_MODIFY` — defined in `ldap.h` as `"1.3.6.1.4.1.4203.1.11.1"`
- `ber_scanf()` with context tags — from `lber.h`
- `ber_alloc_t()`, `ber_init2()`, `ber_printf()` — from `lber.h`
- `op->ore_reqdata` — the extended operation request data (check exact field name in the OpenLDAP version used)

### OpenLDAP Version Compatibility

The Docker image uses Debian Trixie's `libldap-dev`. Verify:
- The exact field name for extended operation request data in the `Operation` struct
- Whether `LDAP_EXOP_PASSWD_MODIFY` is defined in the headers
- The `slap_overinst` struct layout

Check at build time:
```bash
grep -r "ore_reqdata\|oer_data\|exop" /usr/include/ldap.h
grep -r "bi_op_extended" /usr/src/openldap-*/servers/slapd/slap.h
```

### Replay State After Password Change

After a successful password change with OTP validation:
- Update the replay counters (same as bind flow)
- The user's OTP counters are now consumed
- The new password is set, but the old OTP is invalid going forward
- This is correct behavior — the YubiKey generates new OTPs automatically

### Edge Cases

1. **User without YubiKey enabled** — pass through to normal handler (no OTP stripping)
2. **No `oldPasswd` in request** — pass through (server will use the bound session's auth)
3. **`oldPasswd` too short for password+OTP** — deny with error 49
4. **OTP validation fails** — deny with error 49
5. **`userIdentity` specifies a different user** — look up THAT user's entry for YubiKey check
6. **Privileged bind DN doing password change** — the bound DN is admin, target is the user. Need to check the TARGET user's YubiKey status, not the admin's.

## Testing Plan

### Build Test

```bash
cd /opt/slapo-ykbind
docker build -t openldap-ykbind:test -f docker/openldap/Dockerfile .
```

### Functional Tests

```bash
# 1. Test bind with passwordOTP (should succeed — existing behavior)
docker exec openldap-ykbind ldapwhoami -x \
  -D "uid=testuser,ou=people,dc=example,dc=org" \
  -w 'passwordOTP' \
  -H ldap://127.0.0.1:1389

# 2. Test Password Modify Extended Op with passwordOTP as oldPasswd
#    (should succeed after fix — previously failed with error 49)
docker exec openldap-ykbind ldappasswd -x \
  -D "uid=testuser,ou=people,dc=example,dc=org" \
  -w 'passwordOTP' \
  -s 'newpassword' \
  -H ldap://127.0.0.1:1389

# 3. Test Password Modify Extended Op with plain password as oldPasswd
#    (should succeed — no OTP to strip)
docker exec openldap-ykbind ldappasswd -x \
  -D "uid=testuser,ou=people,dc=example,dc=org" \
  -w 'password' \
  -s 'newpassword' \
  -H ldap://127.0.0.1:1389

# 4. Test with wrong OTP (should fail with error 49)
docker exec openldap-ykbind ldappasswd -x \
  -D "uid=testuser,ou=people,dc=example,dc=org" \
  -w 'passwordWRONGOTP' \
  -s 'newpassword' \
  -H ldap://127.0.0.1:1389

# 5. Test with user that has YubiKey disabled
#    (should succeed — no OTP required)
docker exec openldap-ykbind ldappasswd -x \
  -D "uid=nonyubiuser,ou=people,dc=example,dc=org" \
  -w 'password' \
  -s 'newpassword' \
  -H ldap://127.0.0.1:1389
```

### Client-Side Test

```bash
# On a Linux client with nslcd + PAM
passwd
# Enter: passwordOTP (current password with OTP)
# Enter: newpassword (new password)
# Enter: newpassword (confirm)
# Expected: Success
```

### Debug Logging

Enable slapd debug logging during testing:
```bash
docker exec openldap-ykbind ldapmodify -Q -Y EXTERNAL -H ldapi:/// <<EOF
dn: cn=config
changetype: modify
replace: olcLogLevel
olcLogLevel: stats bind acl
EOF
```

Check logs:
```bash
docker logs -f openldap-ykbind 2>&1 | grep -E '(ykbind|BIND|PASSWD|RESULT|err=)'
```

## Files to Modify

| File | Change |
|------|--------|
| `slapo-ykbind-new.c` | Add `ykbind_extended_op()`, `ykbind_parse_passwd_modify_request()`, `ykbind_rebuild_passwd_modify_request()`, refactor `ykbind_simple_bind()` to use shared OTP validation, register `bi_op_extended` |
| `Makefile` | No changes needed (same build process) |
| `Dockerfile` | No changes needed (same build context) |

## Deployment Steps (After Implementation)

1. Build new Docker image with updated overlay
2. Export image as tar archive
3. Transfer to target host
4. Stop the running container
5. Load new image
6. Start container with new image
7. Verify overlay is loaded: `docker exec openldap-ykbind ldapsearch -Q -Y EXTERNAL -H ldapi:/// -b "cn=config" "(olcModuleLoad=ykbind.so)"`
8. Run functional tests
9. Test from a real Linux client with `passwd`

## Alternative Approaches (If BER Rebuild Is Too Complex)

### Alternative A: Client-side `pam_exec.so` script

On each Linux client, replace the PAM password stack with a custom script that:
1. Receives `PAM_OLDAUTHTOK` (passwordOTP) and `PAM_AUTHTOK` (new password)
2. Strips the OTP from the old password
3. Uses `ldapmodify` with a privileged bind DN to change `userPassword` directly

**Pros:** No server-side code changes
**Cons:** Requires privileged bind DN on every client, more attack surface

### Alternative B: Disable OTP requirement for password change

Add an LDAP ACL that allows password modification without OTP by using a specific bind DN or mechanism.

**Pros:** Simple
**Cons:** Weakens security — OTP is bypassed for password changes

### Alternative C: nslcd `binddn`/`bindpw` with `pam_password exop`

Configure nslcd to use a privileged bind DN. The server-side overlay would need to detect that the bind is privileged and skip OTP validation for the extended operation.

**Pros:** Minimal client changes
**Cons:** Requires admin credentials on clients, OTP bypass for password changes
