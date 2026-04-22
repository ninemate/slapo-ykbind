FROM debian:trixie AS builder

ARG http_proxy=http://proxy.example.net:3128
ARG https_proxy=http://proxy.example.net:3128
ARG no_proxy=localhost,127.0.0.1
ARG HTTP_PROXY=http://proxy.example.net:3128
ARG HTTPS_PROXY=http://proxy.example.net:3128
ARG NO_PROXY=localhost,127.0.0.1
ARG LDAP_MODULE_BUILD_ENABLED=true

ENV DEBIAN_FRONTEND=noninteractive
ENV http_proxy=${http_proxy}
ENV https_proxy=${https_proxy}
ENV no_proxy=${no_proxy}
ENV HTTP_PROXY=${HTTP_PROXY}
ENV HTTPS_PROXY=${HTTPS_PROXY}
ENV NO_PROXY=${NO_PROXY}

COPY noCheckValidUntil /etc/apt/apt.conf.d/noCheckValidUntil
COPY module/ /usr/src/ykbind/

RUN set -eux; \
    sed -i 's/^Types: deb$/Types: deb deb-src/' /etc/apt/sources.list.d/debian.sources; \
    apt-get -y update; \
    apt-get -y upgrade --fix-missing; \
    apt-get -y install build-essential ca-certificates devscripts dpkg-dev libldap-dev libssl-dev patch; \
    apt-get -y build-dep openldap; \
    mkdir -p /build-out/usr/lib/ldap; \
    if [ "${LDAP_MODULE_BUILD_ENABLED}" = "true" ]; then \
      cd /usr/src; \
      apt-get source openldap; \
      srcdir="$(find /usr/src -maxdepth 1 -mindepth 1 -type d -name 'openldap-*' | sort | head -n 1)"; \
      [ -n "${srcdir}" ]; \
      if [ -f /usr/src/ykbind/patches/openldap-2.6.10-sasl-getconfpath-guard.patch ]; then \
        patch -d "${srcdir}" -p1 < /usr/src/ykbind/patches/openldap-2.6.10-sasl-getconfpath-guard.patch || true; \
      fi; \
      cd "${srcdir}"; \
      ./configure; \
      cd /usr/src/ykbind; \
      make OPENLDAP_SRC="${srcdir}" OPENLDAP_BUILD="${srcdir}"; \
      install -D -m 0755 ykbind.so /build-out/usr/lib/ldap/ykbind.so; \
    fi

FROM debian:trixie

ARG http_proxy=http://proxy.example.net:3128
ARG https_proxy=http://proxy.example.net:3128
ARG no_proxy=localhost,127.0.0.1
ARG HTTP_PROXY=http://proxy.example.net:3128
ARG HTTPS_PROXY=http://proxy.example.net:3128
ARG NO_PROXY=localhost,127.0.0.1

ENV DEBIAN_FRONTEND=noninteractive
ENV http_proxy=${http_proxy}
ENV https_proxy=${https_proxy}
ENV no_proxy=${no_proxy}
ENV HTTP_PROXY=${HTTP_PROXY}
ENV HTTPS_PROXY=${HTTPS_PROXY}
ENV NO_PROXY=${NO_PROXY}

COPY noCheckValidUntil /etc/apt/apt.conf.d/noCheckValidUntil
COPY syslog-ng.conf /etc/syslog-ng/syslog-ng.conf
COPY entrypoint.sh /entrypoint.sh
COPY healthcheck.sh /healthcheck.sh

RUN set -eux; \
    echo "slapd slapd/no_configuration boolean true" | debconf-set-selections; \
    apt-get -y update; \
    apt-get -y upgrade --fix-missing; \
    apt-get -y install telnet tcpdump vim net-tools procps openssh-client; \
    apt-get -y install slapd ldap-utils syslog-ng logrotate ca-certificates; \
    rm -rf /var/lib/apt/lists/*; \
    mkdir -p /opt/openldap/bootstrap /var/log/slapd; \
    chmod +x /entrypoint.sh /healthcheck.sh

COPY --from=builder /build-out/usr/lib/ldap/ /usr/lib/ldap/

EXPOSE 389 636
VOLUME ["/var/lib/ldap", "/etc/ldap/slapd.d", "/var/log/slapd"]

HEALTHCHECK --interval=30s --timeout=10s --retries=5 CMD ["/healthcheck.sh"]

ENTRYPOINT ["/entrypoint.sh"]
