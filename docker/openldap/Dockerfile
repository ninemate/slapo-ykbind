ARG BASE_IMAGE=debian:trixie

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

RUN printf '%s\n' \
  'Acquire::Retries "10";' \
  'Acquire::http::Timeout "60";' \
  'Acquire::https::Timeout "60";' \
  'Acquire::http::Pipeline-Depth "0";' \
  'Acquire::https::Pipeline-Depth "0";' \
  'Acquire::http::No-Cache "true";' \
  'Acquire::https::No-Cache "true";' \
  'Acquire::Languages "none";' \
  'Acquire::PDiffs "false";' \
  'Acquire::ForceIPv4 "true";' \
  > /etc/apt/apt.conf.d/99-proxy-safe

COPY noCheckValidUntil /etc/apt/apt.conf.d/noCheckValidUntil

RUN set -eux; \
    sed -i 's/^Types: deb$/Types: deb deb-src/' /etc/apt/sources.list.d/debian.sources; \
    apt-get -y update; \
    apt-get -y install --no-install-recommends ca-certificates && \
    apt-get -y install --no-install-recommends file && \
    apt-get -y install --no-install-recommends build-essential && \
    apt-get -y install --no-install-recommends libssl-dev && \
    apt-get -y install --no-install-recommends libsasl2-dev libldap-dev && \
    apt-get -y install --no-install-recommends \
      dpkg-dev \
      patch \
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
      if [ -f /usr/src/ykbind/patches/openldap-2.6.10-sasl-getconfpath-guard.patch ]; then \
        patch -d "${srcdir}" -p1 < /usr/src/ykbind/patches/openldap-2.6.10-sasl-getconfpath-guard.patch || true; \
      fi; \
      if [ -f "${srcdir}/build/ldap_config.h" ] || [ -f "${srcdir}/build/include/ldap_config.h" ]; then \
        builddir="${srcdir}/build"; \
      else \
        builddir="/usr/src/openldap-build"; \
        mkdir -p "${builddir}"; \
        cd "${builddir}"; \
        "${srcdir}/configure"; \        
        make depend; \
        make -C include; \
      fi; \
      if [ ! -f "${builddir}/ldap_config.h" ] && [ ! -f "${builddir}/include/ldap_config.h" ]; then \
        echo "ldap_config.h not found under ${builddir}" >&2; \
        find "${builddir}" -maxdepth 3 \( -name ldap_config.h -o -name portable.h -o -name ldap_features.h \) -print >&2 || true; \
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
    apt-get -y install --no-install-recommends telnet tcpdump vim net-tools procps openssh-client; \
    apt-get -y install --no-install-recommends slapd ldap-utils logrotate ca-certificates; \
    apt-get -y install -o Dpkg::Options::="--force-confold" syslog-ng; \
    rm -rf /var/lib/apt/lists/*; \
    mkdir -p /opt/openldap/bootstrap /var/log/slapd; \
    chmod +x /entrypoint.sh /healthcheck.sh

COPY --from=builder /build-out/usr/lib/ldap/ /usr/lib/ldap/

EXPOSE 389 636
VOLUME ["/var/lib/ldap", "/etc/ldap/slapd.d", "/var/log/slapd"]

HEALTHCHECK --interval=30s --timeout=10s --retries=5 CMD ["/healthcheck.sh"]

ENTRYPOINT ["/entrypoint.sh"]
