# Deploy TODO

## 1. Image contains baked-in yubikey schema

A `full_import` a schema importnál elbukik: a yubikey OID-k már a bootstrap után is bent vannak a `cn=config`-ban (`cn={4}yubikey-otp`). Ez azt jelenti, hogy a használt Docker image tartalmazza a schemát az entrypoint/bootstrap által.

**Megoldási lehetőségek:**

- **a)** Rebuildeli az image-et a jelenlegi Dockerfile-ból (ami NEM importálja a schemát), exportálni tar-ba, bundle-t frissíteni, és újra `full_import`
- **b)** Kitörölni a schemát a `cn=config`-ból, majd `full_import` újrafuttatása (ldd az előző üzenetet)

## 2. Check-schema-ldif-state.sh bug

A script `state=missing`-et ad vissza amikor a schema OID-k már bent vannak a `cn=config`-ban, csak `cn={4}yubikey-otp` néven. Ezért próbál ldapadd-olni → duplicate error.

**Valószínű ok:** a script `extract_oids` függvénye vagy az `unfold_ldif` nem dolgozza fel helyesen a bent lévő schema dump-ot.

## 3. Bundle deploy: control node Docker

Két módosítás készült a bundle deploy támogatásához Docker nélküli control node-on:

- `local_build.yml` — Docker check-ek átugrása ha `ldap_skip_local_build=true && ldap_skip_local_save=true`
- `radius_local_build.yml` — ugyanez a RADIUS oldalra

## 4. Systemd hardening

A compose systemd unit-ból kivettük a runc-val ütköző hardening direktívákat:

- `MemoryDenyWriteExecute=yes`
- `PrivateDevices=yes`
- `ProtectKernelModules=yes`
- `ProtectControlGroups=yes`
- `ProtectHostname=yes`
- `LockPersonality=yes`
- `SystemCallArchitectures=native`

## 5. TLS cert IP vs FQDN

Mirrormode-ban ha a TLS cert only FQDN-re érvényes, akkor az `ldap_mirrormode_nodes` listában az `fqdn` mezőt kell használni. A replikációs URI abból képződik.

## 6. RADIUS dynamic_clients ipaddr

A `radius_dynamic_client_networks` listában `ipaddr` a field neve, nem `ip`.

## 7. Adopt_existing utáni teendők

- `full_import` helyett `adopt_existing` futtatása ha a config/data már bent van
- Ha kell module/overlay beállítás, utána `maintenance` mód
