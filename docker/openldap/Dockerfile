ARG BASE_IMAGE=debian:trixie
ARG LDAP_RUNTIME_USER=ldap-runtime
ARG LDAP_RUNTIME_GROUP=ldap-runtime
ARG LDAP_RUNTIME_UID=11050
ARG LDAP_RUNTIME_GID=11050

FROM ${BASE_IMAGE} AS builder

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

RUN set -eux; \
    sed -i 's/^Types: deb$/Types: deb deb-src/' /etc/apt/sources.list.d/debian.sources; \
    apt-get -y update; \
    apt-get -y install --no-install-recommends \
      build-essential \
      ca-certificates \
      dpkg-dev \
      file \
      libldap-dev \
      libsasl2-dev \
      libssl-dev \
      pkg-config; \
    if [ "${LDAP_MODULE_BUILD_ENABLED}" = "true" ]; then \
      cd /usr/src; \
      apt-get source openldap; \
    fi; \
    mkdir -p /build-out/usr/lib/ldap; \
    rm -rf /var/lib/apt/lists/*

COPY module/ /usr/src/ykbind/

RUN set -eux; \
    if [ "${LDAP_MODULE_BUILD_ENABLED}" = "true" ]; then \
      cd /usr/src; \
      srcdir="$(find /usr/src -maxdepth 1 -mindepth 1 -type d -name 'openldap-*' | sort | head -n 1)"; \
      [ -n "${srcdir}" ]; \
      if [ -f "${srcdir}/build/include/portable.h" ] && [ -f "${srcdir}/build/include/ldap_features.h" ]; then \
        builddir="${srcdir}/build"; \
      else \
        builddir="/usr/src/openldap-build"; \
        mkdir -p "${builddir}"; \
        cd "${builddir}"; \
        "${srcdir}/configure"; \
        make depend; \
        make -C include; \
      fi; \
      if [ ! -f "${builddir}/include/portable.h" ] || [ ! -f "${builddir}/include/ldap_features.h" ] || [ ! -f "${builddir}/include/ldap_config.h" ]; then \
        echo "OpenLDAP generated headers not found under ${builddir}/include" >&2; \
        find "${builddir}" -maxdepth 3 \( -name portable.h -o -name ldap_features.h -o -name ldap_config.h \) -print >&2 || true; \
        exit 1; \
      fi; \
      cd /usr/src/ykbind; \
      make OPENLDAP_SRC="${srcdir}" OPENLDAP_BUILD="${builddir}"; \
      install -D -m 0755 ykbind.so /build-out/usr/lib/ldap/ykbind.so; \
    fi

FROM ${BASE_IMAGE}

ARG http_proxy=http://proxy.example.net:3128
ARG https_proxy=http://proxy.example.net:3128
ARG no_proxy=localhost,127.0.0.1
ARG HTTP_PROXY=http://proxy.example.net:3128
ARG HTTPS_PROXY=http://proxy.example.net:3128
ARG NO_PROXY=localhost,127.0.0.1
ARG LDAP_RUNTIME_USER=ldap-runtime
ARG LDAP_RUNTIME_GROUP=ldap-runtime
ARG LDAP_RUNTIME_UID=11050
ARG LDAP_RUNTIME_GID=11050

ENV DEBIAN_FRONTEND=noninteractive
ENV http_proxy=${http_proxy}
ENV https_proxy=${https_proxy}
ENV no_proxy=${no_proxy}
ENV HTTP_PROXY=${HTTP_PROXY}
ENV HTTPS_PROXY=${HTTPS_PROXY}
ENV NO_PROXY=${NO_PROXY}

COPY noCheckValidUntil /etc/apt/apt.conf.d/noCheckValidUntil
COPY entrypoint.sh /entrypoint.sh
COPY healthcheck.sh /healthcheck.sh

RUN set -eux; \
    echo "slapd slapd/no_configuration boolean true" | debconf-set-selections; \
    apt-get -y update; \
    apt-get -y install --no-install-recommends telnet tcpdump vim net-tools procps openssh-client; \
    apt-get -y install --no-install-recommends slapd ldap-utils syslog-ng logrotate ca-certificates gosu; \
    rm -rf /var/lib/apt/lists/*; \
    groupadd --gid "${LDAP_RUNTIME_GID}" "${LDAP_RUNTIME_GROUP}"; \
    useradd --uid "${LDAP_RUNTIME_UID}" --gid "${LDAP_RUNTIME_GID}" --home-dir /nonexistent --shell /usr/sbin/nologin --no-create-home "${LDAP_RUNTIME_USER}"; \
    # Debian's slapd package skips creating cn=config state when no_configuration=true,
    # so runtime paths must be created explicitly instead of assuming package side effects.
    install -d -m 0755 -o "${LDAP_RUNTIME_UID}" -g "${LDAP_RUNTIME_GID}" \
      /opt/openldap/bootstrap \
      /opt/openldap/bootstrap/ldif \
      /opt/openldap/bootstrap/schema; \
    install -d -m 0750 -o "${LDAP_RUNTIME_UID}" -g "${LDAP_RUNTIME_GID}" \
      /etc/ldap/tls \
      /etc/ldap/slapd.d \
      /run/slapd \
      /var/lib/ldap \
      /var/log/slapd; \
    chmod +x /entrypoint.sh /healthcheck.sh

COPY syslog-ng.conf /etc/syslog-ng/syslog-ng.conf
COPY --from=builder /build-out/usr/lib/ldap/ /usr/lib/ldap/

EXPOSE 1389 1636
VOLUME ["/var/lib/ldap", "/etc/ldap/slapd.d", "/var/log/slapd"]

HEALTHCHECK --interval=30s --timeout=10s --retries=5 CMD ["/healthcheck.sh"]

ENTRYPOINT ["/entrypoint.sh"]
