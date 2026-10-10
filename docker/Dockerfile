FROM ubuntu:24.04

ENV DEBIAN_FRONTEND=noninteractive

RUN apt-get update \
    && apt-get install -y --no-install-recommends \
        build-essential cmake valgrind pkg-config libsqlite3-dev libavahi-client-dev \
        libgtk-4-dev autoconf automake libtool bison flex git \
        dbus avahi-daemon libnss-mdns \
    && rm -rf /var/lib/apt/lists/*
