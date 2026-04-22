# Meglévő kód elemzése

## Mit csinál a jelenlegi `yubikey_validate.c`

A repositoryban lévő C fájl valójában egy **389 Directory Server / RHDS pre-bind plugin**, nem OpenLDAP modul. A plugin a `SLAPI_PLUGIN_PRE_BIND_FN` hookra ül rá, tehát még a normál jelszóellenőrzés előtt fut le.

Fő lépések:

1. Kiolvassa a bind DN-hez tartozó LDAP entryt.
2. A felhasználó entryjéből ezeket a mezőket olvassa:
   - `YKkeyID`
   - `YKaesKey`
   - `YKkeyCounter`
   - `YKsessionTimestamp`
   - `modifyTimestamp`
3. A bind credential utolsó **44 karakterét** OTP-nek tekinti.
4. A credential prefixét statikus jelszónak tekinti.
5. Az OTP első 12 karakterét figyelmen kívül hagyja, a maradék 32 modhex karaktert AES-128-cal dekódolja.
6. A dekódolt ticketből ellenőrzi:
   - a titkos, dekódolt 6 bájtos UID-t (`YKkeyID`-hez hasonlítja),
   - egy kombinált számlálóértéket (`useCtr + sessionCtr`),
   - egy 24 bites timestamp mezőt.
7. Ha az OTP elfogadható, belső LDAP modify-val frissíti:
   - `YKkeyCounter`
   - `YKsessionTimestamp`
8. Végül a bind credentialt lecseréli a levágott statikus jelszóra, és a 389 DS normál jelszóellenőrzése már ezt validálja.

## Jelenlegi logika és adatfolyam

### Hook

- 389 DS `pre-bind` plugin
- tehát még az LDAP beépített password check előtt fut

### Password + OTP kezelés

- formátum: `<password><otp>`
- az OTP fixen 44 karakteresnek van véve
- nincs külön separator

### YubiKey mezők

- `YKkeyID`: a dekódolt privát UID hex formája
- `YKaesKey`: 16 bájtos AES kulcs hex stringként
- `YKkeyCounter`: 16 bites use counter + 8 bites session counter, összefűzve hexben
- `YKsessionTimestamp`: 24 bites timestamp hexben
- `modifyTimestamp`: a directory által vezetett módosítási idő

### Replay kezelés a meglévő kódban

- elutasítja, ha az új kombinált counter kisebb a tároltnál
- ha a counter megegyezik, egy 15 perces `modifyTimestamp` alapú láncolt elfogadást enged
- ha a use counter azonos, de a session/timestamp eltér, idődelta alapú heurisztikát használ

Ez a rész nem elég szigorú: a meglévő plugin **nem valódi egyszer-használatú** replay-védelmet ad.

### Kriptográfiai ellenőrzés

- van AES dekódolás
- van dekódolt privát UID ellenőrzés
- **nincs CRC-ellenőrzés**
- az OTP public ID részét a kód nem validálja

### Dependency-k

- 389 DS plugin API (`slapi-plugin.h`)
- OpenSSL (`AES_decrypt`)
- belső LDAP modify a directory szerver felé
- nincs PAM dependency
- nincs külső validator hívás

## Debian/OpenLDAP-ra átültetésnél mi vihető át 1:1-ben

Átvehető:

- a `<password><otp>` szerveroldali szétválasztás
- a per-user AES kulcsos, belső YubiKey OTP dekódolás
- a bind előtti OTP ellenőrzés
- a sikeres OTP után csak a statikus jelszó továbbadása a beépített password checknek
- a számláló/timestamp visszaírás LDAP-ba

Nem vihető át 1:1-ben:

- a 389 DS SLAPI hook
- a `Slapi_PBlock` / `slapi_search_internal_*` / `slapi_modify_internal_*` API
- a 15 perces chained-auth replay kivétel

## Az új overlayben bevezetett fontos javítások

- OpenLDAP `bi_op_bind` overlay hook
- modhex formátumellenőrzés
- YubiKey ticket CRC ellenőrzés
- opcionális public ID egyezés ellenőrzése
- külön tárolt `useCtr` és `sessionCtr`
- szigorú replay védelem: csak akkor sikeres, ha
  - a statikus jelszó helyes,
  - az OTP alakilag helyes,
  - az AES/CRC/UID ellenőrzés helyes,
  - az OTP számlálói nagyobbak a legutóbb elfogadottnál
