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

Ez a repo jelenleg a crash-javított változatot tartalmazza:

- backend-konzisztens entry release
- robusztusabb OTP suffix-leválasztás
- részletesebb hibalogok
- OpenLDAP 2.6 build-kompatibilitási javítás

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

OpenLDAP `2.5.13` alatt a folyamat ugyanaz. A fontos szabály:

- a `2.5.13`-as `slapd`-hez `2.5.13`-as OpenLDAP forrással fordíts
- a modul `cn=config` oldalon ugyanúgy tölthető be
- meglévő adatbázis struktúrát nem kell átalakítani, csak schema + overlay + user attribútumok kerülnek hozzá

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

## Meglévő OpenLDAP 2.5.13 adatbázishoz hozzáadás

Ha már van működő `cn=config`-os OpenLDAP adatbázisod, a tipikus sorrend:

1. fordítsd le a modult a pontos `2.5.13` forrásfához
2. töltsd be a schema-t
3. töltsd be a modult
4. add hozzá az overlayt a megfelelő `olcDatabase={N}...` adatbázishoz
5. add hozzá az ACL-t a YubiKey secret mezők védelmére
6. userenként add hozzá a `yubiKeyTokenAux` objectClass-t és a YubiKey attribútumokat

Az adatbázis sorszámát így tudod megkeresni:

```bash
ldapsearch -Q -Y EXTERNAL -H ldapi:/// -LLL -b cn=config '(olcDatabase=*)' dn olcDatabase olcSuffix
```

Ha például a suffixed `dc=nodomain`, és azt az `olcDatabase={1}mdb,cn=config` kezeli, akkor az overlayt és az ACL-t azon az adatbázison kell módosítani.

## ACL minta

Példa meglévő adatbázisra:

```bash
ldapmodify -Q -Y EXTERNAL -H ldapi:/// -f examples/acl-yubikey-secrets.ldif
```

Az [examples/acl-yubikey-secrets.ldif](/home/username/Documents/yubik/examples/acl-yubikey-secrets.ldif) fájlban az `olcDatabase={1}mdb` és az admin DN mintaérték, ezt a saját adatbázisodra kell átírni.

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

## Userenkénti bekapcsolás meglévő usereknél

A legegyszerűbb, ha nem új entryt hozol létre, hanem a meglévő userre `modify` művelettel ráteszed a YubiKey objectClass-t és attribútumokat.

Minta:

```bash
ldapmodify -x -D "cn=admin,dc=example,dc=com" -W -f examples/user-enable-template.ldif
```

A fájl:

- [examples/user-enable-template.ldif](/home/username/Documents/yubik/examples/user-enable-template.ldif)

Mit kell benne személyre szabni:

- `dn`
- `yubiKeyPublicId`
- `yubiKeyPrivateUid`
- `yubiKeyAesKey`

Ha egy usernél átmenetileg ki akarod kapcsolni, de az adatokat megtartanád:

```bash
ldapmodify -x -D "cn=admin,dc=example,dc=com" -W -f examples/user-disable-template.ldif
```

Ha teljesen le akarod venni a YubiKey mezőket:

```bash
ldapmodify -x -D "cn=admin,dc=example,dc=com" -W -f examples/user-remove-template.ldif
```

Kapcsolódó minták:

- [examples/user-disable-template.ldif](/home/username/Documents/yubik/examples/user-disable-template.ldif)
- [examples/user-remove-template.ldif](/home/username/Documents/yubik/examples/user-remove-template.ldif)

## Bulk kulcsfeltöltés meglévő userekhez

A repo tartalmaz egy egyszerű, semicolon-delimited CSV→LDIF generátort meglévő userekhez.

CSV minta:

- [examples/bulk-users-template.csv](/home/username/Documents/yubik/examples/bulk-users-template.csv)

Oszlopok:

- `dn`
- `public_id`
- `private_uid_hex`
- `aes_key_hex`
- `enabled`

Fontos: a minta **pontosvesszővel** (`;`) elválasztott formátumot használ, mert a DN mező önmagában is vesszőket tartalmaz.

Generálás:

```bash
chmod +x tools/generate-yubikey-ldif.sh
tools/generate-yubikey-ldif.sh examples/bulk-users-template.csv > bulk-yubikey-users.ldif
```

Betöltés:

```bash
ldapmodify -x -D "cn=admin,dc=example,dc=com" -W -f bulk-yubikey-users.ldif
```

A script:

- [tools/generate-yubikey-ldif.sh](/home/username/Documents/yubik/tools/generate-yubikey-ldif.sh)

A script minden usernél:

- hozzáadja a `yubiKeyTokenAux` objectClass-t
- beállítja a `yubiKeyEnabled` flaget
- feltölti a `publicId`, `privateUid`, `aesKey` mezőket
- nullázza a replay mezőket

Példa saját CSV-re:

```csv
dn;public_id;private_uid_hex;aes_key_hex;enabled
uid=alice,ou=people,dc=example,dc=org;cccccccccccb;001122334455;8899aabbccddeeff0011223344556677;TRUE
uid=test1,ou=people,dc=example,dc=org;cccccccccccb;001122334455;8899aabbccddeeff0011223344556677;FALSE
```

## Gyors ellenőrzések bulk után

```bash
ldapsearch -x -LLL -b "ou=people,dc=example,dc=com" '(objectClass=yubiKeyTokenAux)' dn yubiKeyEnabled yubiKeyPublicId
```

Egy konkrét user secret mezőinek admin oldali ellenőrzése:

```bash
ldapsearch -x -LLL -D "cn=admin,dc=example,dc=com" -W \
  -b "uid=alice,ou=People,dc=example,dc=com" '(objectClass=*)' \
  dn yubiKeyEnabled yubiKeyPublicId yubiKeyPrivateUid yubiKeyAesKey
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

Megjegyzés: ezt valóban csak friss, érvényes OTP-vel lehet lefedni. Dummy modhex stringgel az overlay az OTP hibáján fog elhasalni még a statikus jelszó ellenőrzése előtt.

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

### További negatív tesztek

- rossz public ID, de 44 karakteres modhex suffix
- helyes public ID, de CRC-hibás OTP
- hibás karakter a suffix közepén vagy végén
- csak OTP, statikus jelszó nélkül
- hosszú statikus jelszó + rossz suffix

Várt eredmény minden esetben: `Invalid credentials`, a `slapd` folyamat nem eshet el.

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
6. A bulk import script egyszerű, pontosvesszővel elválasztott admin inputra készült; idézőjeles/escaped CSV mezőket nem kezel.
