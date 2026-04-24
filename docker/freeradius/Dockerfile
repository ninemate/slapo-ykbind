ARG BASE_IMAGE=debian:trixie
ARG RADIUS_RUNTIME_USER=radius-runtime
ARG RADIUS_RUNTIME_GROUP=radius-runtime
ARG RADIUS_RUNTIME_UID=11060
ARG RADIUS_RUNTIME_GID=11060

FROM ${BASE_IMAGE}

ARG http_proxy
ARG https_proxy
ARG no_proxy=localhost,127.0.0.1
ARG HTTP_PROXY
ARG HTTPS_PROXY
ARG NO_PROXY=localhost,127.0.0.1
ARG RADIUS_RUNTIME_USER=radius-runtime
ARG RADIUS_RUNTIME_GROUP=radius-runtime
ARG RADIUS_RUNTIME_UID=11060
ARG RADIUS_RUNTIME_GID=11060

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
    apt-get -y update; \
    apt-get -y install --no-install-recommends \
      ca-certificates \
      freeradius \
      freeradius-ldap \
      freeradius-utils \
      ldap-utils \
      procps; \
    rm -rf /var/lib/apt/lists/*; \
    groupadd --gid "${RADIUS_RUNTIME_GID}" "${RADIUS_RUNTIME_GROUP}"; \
    useradd --uid "${RADIUS_RUNTIME_UID}" --gid "${RADIUS_RUNTIME_GID}" --home-dir /nonexistent --shell /usr/sbin/nologin --no-create-home "${RADIUS_RUNTIME_USER}"; \
    chown "${RADIUS_RUNTIME_UID}:${RADIUS_RUNTIME_GID}" /etc/freeradius /etc/freeradius/3.0; \
    chmod 0755 /etc/freeradius /etc/freeradius/3.0; \
    chgrp -R "${RADIUS_RUNTIME_GID}" /etc/freeradius/3.0; \
    chmod -R g+rX /etc/freeradius/3.0; \
    if [ -d /etc/ssl/private ]; then \
      chgrp "${RADIUS_RUNTIME_GID}" /etc/ssl/private; \
      chmod 0710 /etc/ssl/private; \
    fi; \
    if [ -f /etc/ssl/private/ssl-cert-snakeoil.key ]; then \
      chgrp "${RADIUS_RUNTIME_GID}" /etc/ssl/private/ssl-cert-snakeoil.key; \
      chmod 0640 /etc/ssl/private/ssl-cert-snakeoil.key; \
    fi; \
    install -d -m 0755 -o "${RADIUS_RUNTIME_UID}" -g "${RADIUS_RUNTIME_GID}" \
      /etc/freeradius/3.0 \
      /var/log/freeradius \
      /var/run/freeradius; \
    chmod +x /entrypoint.sh /healthcheck.sh

EXPOSE 1812/udp 1813/udp
VOLUME ["/var/log/freeradius", "/var/run/freeradius"]

HEALTHCHECK --interval=30s --timeout=10s --retries=5 CMD ["/healthcheck.sh"]

USER ${RADIUS_RUNTIME_UID}:${RADIUS_RUNTIME_GID}

ENTRYPOINT ["/entrypoint.sh"]
