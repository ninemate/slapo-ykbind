# OpenLDAP YubiKey OTP overlay Debianhoz

Ez a repository a meglévő 389 DS plugin logikájából kiindulva egy **OpenLDAP overlay** modult ad, amely egyszerű bind esetén a beküldött credentialt:

`<password><otp>`

alakban várja, a végéről levágja a 44 karakteres YubiKey OTP-t, azt helyben validálja, majd a maradék statikus jelszót átadja a slapd normál jelszóellenőrzésének.

## Auth flow

1. A kliens simple bindot küld a teljes `<password><otp>` credentiallel.
2. Az overlay a bind DN entryjét kiolvassa.
3. Ha a userhez YubiKey van rendelve, az overlay:
   - leválasztja az OTP-t,
   - dekódolja az AES ticketet,
   - ellenőrzi a private UID-t,
   - ellenőrzi a CRC-t,
   - ellenőrzi a replay állapotot.
4. Ha ez sikeres, az overlay a bind credentialt lecseréli a statikus jelszóra.
5. Az alatta lévő OpenLDAP backend a meglévő `userPassword` alapján autentikál.
6. Sikeres bind után az overlay belső modify-val frissíti a YubiKey replay mezőket.

## Adatmodell

Az új schema elsődleges mezői:

- `yubiKeyEnabled`
- `yubiKeyPublicId`
- `yubiKeyPrivateUid`
- `yubiKeyAesKey`
- `yubiKeyLastUseCtr`
- `yubiKeyLastSessionCtr`
- `yubiKeyLastTimestamp`
- `yubiKeyLastCounter`

A schema kompatibilitási aliasokat is ad a régi 389 DS mezőkhöz:

- `YKkeyID`
- `YKaesKey`
- `YKkeyCounter`
- `YKsessionTimestamp`

Ezért meglévő 389 DS-ből hozott attribútumnevekkel is működhet a migráció.

Megjegyzés: a schema fájlban szereplő `1.3.6.1.4.1.55555.*` OID-ok mintaként szerepelnek. Éles környezetben célszerű saját enterprise OID alá tenni őket.

## Build Debianon

OpenLDAP overlay építéshez nem elég a kliens oldali `libldap-dev`, mert a modul belső `slapd` fejléceket használ. Emiatt a modult **azonos verziójú OpenLDAP forrásfához** kell fordítani, mint ami a célgépen fut.

### Szükséges csomagok

Debian Bookwormon legalább:

- `build-essential`
- `libssl-dev`
- `libldap-dev`
- `slapd`
- `ldap-utils`
- `dpkg-dev`
- `apt-src` vagy `deb-src` engedélyezett APT source

### Forrás előkészítése

```bash
sudo apt-get update
sudo apt-get install build-essential libssl-dev libldap-dev slapd ldap-utils dpkg-dev
apt-get source openldap
cd openldap-*
./configure
cd /path/to/this/repo
make OPENLDAP_SRC=/path/to/openldap-* OPENLDAP_BUILD=/path/to/openldap-*
```

Ha a célrendszer más OpenLDAP verziót futtat, mindig ahhoz pontosan illeszkedő forrással fordíts.

### Telepítés

```bash
sudo install -d /usr/lib/ldap
sudo install -m 0755 ykbind.so /usr/lib/ldap/ykbind.so
```

Megjegyzés: Debianon az OpenLDAP modulkönyvtár jellemzően `/usr/lib/ldap`.

## Schema betöltés `cn=config` alatt

1. Töltsd be a schema LDIF-et:

```bash
ldapadd -Y EXTERNAL -H ldapi:/// -f schema/yubikey-otp.ldif
```

2. Töltsd be a modult:

```bash
ldapmodify -Y EXTERNAL -H ldapi:/// -f examples/module-load.ldif
```

Ha a szerveren még nincs beállítva a modulkönyvtár:

```bash
ldapmodify -Y EXTERNAL -H ldapi:/// -f examples/module-path-and-load.ldif
```

3. Add hozzá az overlayt a megfelelő adatbázishoz:

Az `{1}mdb` részt igazítsd a saját `olcDatabase` sorszámodhoz.
Az overlayt azon a backend adatbázison kell aktiválni, ahol a felhasználói entryk és a `userPassword` attribútum található. Ha több auth adatbázisod van, mindegyiken külön fel kell venni.

```bash
ldapadd -Y EXTERNAL -H ldapi:/// -f examples/overlay-config.ldif
```

4. Ellenőrizd a konfigurációt:

```bash
slaptest -u -F /etc/ldap/slapd.d
systemctl restart slapd
journalctl -u slapd -n 100 --no-pager
```

## Példa user entry

```ldif
dn: uid=alice,ou=People,dc=example,dc=com
objectClass: inetOrgPerson
objectClass: yubiKeyTokenAux
uid: alice
sn: Example
cn: Alice Example
userPassword: {SSHA}...
yubiKeyEnabled: TRUE
yubiKeyPublicId: cccjgjgkhcbb
yubiKeyPrivateUid: 001122334455
yubiKeyAesKey: 8899aabbccddeeff0011223344556677
yubiKeyLastUseCtr: 0
yubiKeyLastSessionCtr: 0
yubiKeyLastTimestamp: 0
yubiKeyLastCounter: 000000
YKsessionTimestamp: 000000
```

## ACL ajánlás

Az AES kulcsot csak az admin/rootdn és a slapd belső folyamatai láthassák:

```ldif
olcAccess: to attrs=yubiKeyAesKey,yubiKeyPrivateUid,yubiKeyLastUseCtr,yubiKeyLastSessionCtr,yubiKeyLastTimestamp,yubiKeyLastCounter,YKsessionTimestamp
  by dn.exact="cn=admin,dc=example,dc=com" manage
  by * none
```

## Tesztelés

### Helyes jelszó + helyes OTP

```bash
ldapwhoami -x \
  -D "uid=alice,ou=People,dc=example,dc=com" \
  -w 'MyStaticPasswordccccccdefghdefghdefghdefghdefghdefghjk'
```

Várt eredmény: sikeres bind.

### Helyes jelszó + hibás OTP

```bash
ldapwhoami -x \
  -D "uid=alice,ou=People,dc=example,dc=com" \
  -w 'MyStaticPasswordccccccdefghdefghdefghdefghdefghdefghjj'
```

Várt eredmény: `Invalid credentials`.

### Hibás jelszó + helyes OTP

Ugyanaz az OTP, de rossz statikus prefix.

Várt eredmény: `Invalid credentials`.

### Újrahasznált OTP

Egy már sikeresen elfogadott OTP-t küldj be újra ugyanazzal a felhasználóval.

Várt eredmény: `Invalid credentials`.

### Rossz OTP formátum

- nem 44 karakter
- nem modhex karakterekből áll

Várt eredmény: `Invalid credentials`.

### Túl rövid credential

Ha a credential hossza `<= 44`, nincs benne statikus jelszó prefix.

Várt eredmény: `Invalid credentials`.

## Linux login flow

Ez a megoldás akkor működik jól, ha a Linux kliens stack a felhasználó által beírt teljes jelszót LDAP simple bind credentialként továbbítja. Ilyenkor a felhasználó UX:

`normál_jelszó + yubikey_otp`

Példa:

`CorrectHorseBatteryStapleccccccdefghdefghdefghdefghdefghdefghjk`

Ha a kliensoldali PAM komponens a jelszót módosítja, feldarabolja, vagy nem simple bindot használ, az overlay nem fog tudni helyesen dolgozni.

## Ismert korlátok

1. Az OpenLDAP overlay modul belső `slapd` fejlécekre épül, ezért verzióazonos OpenLDAP forrással kell fordítani.
2. A replay állapot frissítése sikeres bind után belső modify-val történik, ezért az entrynek írhatónak kell lennie a slapd belső művelet számára.
3. Nagyon szoros, egyszerre történő párhuzamos bindoknál ugyanazzal az OTP-vel lehet versenyhelyzet, mert a replay állapot ellenőrzése és frissítése nem backend-szintű compare-and-swap tranzakció.
4. A megoldás YubiKey OTP-t kezel, nem OATH-HOTP/TOTP módot.
5. A schema példa OID-okkal érkezik; élesítés előtt saját OID-ra érdemes átírni.
