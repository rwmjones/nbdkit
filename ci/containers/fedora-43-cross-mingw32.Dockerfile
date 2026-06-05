# THIS FILE WAS AUTO-GENERATED
#
#  $ lcitool manifest ci/manifest.yml
#
# https://gitlab.com/libvirt/libvirt-ci

FROM registry.fedoraproject.org/fedora:43

RUN dnf --quiet install -y nosync && \
    printf '#!/bin/sh\n\
if test -d /usr/lib64\n\
then\n\
    export LD_PRELOAD=/usr/lib64/nosync/nosync.so\n\
else\n\
    export LD_PRELOAD=/usr/lib/nosync/nosync.so\n\
fi\n\
exec "$@"\n' > /usr/bin/nosync && \
    chmod +x /usr/bin/nosync && \
    nosync dnf --quiet update -y && \
    nosync dnf --quiet install -y \
                       autoconf \
                       automake \
                       bash \
                       bash-completion-devel \
                       bzip2 \
                       ca-certificates \
                       cargo \
                       ccache \
                       clippy \
                       e2fsprogs \
                       expect \
                       gawk \
                       genisoimage \
                       git \
                       glibc-langpack-en \
                       golang \
                       gzip \
                       iproute \
                       jq \
                       libnbd-devel \
                       libtool \
                       lua-devel \
                       make \
                       ocaml \
                       perl-ExtUtils-Embed \
                       perl-Pod-Simple \
                       perl-base \
                       perl-devel \
                       perl-podlators \
                       python3 \
                       python3-boto3 \
                       python3-devel \
                       python3-flake8 \
                       python3-libnbd \
                       python3-packaging \
                       qemu-img \
                       rust \
                       socat \
                       tar \
                       tcl-devel \
                       util-linux \
                       xorriso \
                       xz && \
    nosync dnf --quiet autoremove -y && \
    nosync dnf --quiet clean all -y && \
    rm -f /usr/lib*/python3*/EXTERNALLY-MANAGED

ENV CCACHE_WRAPPERSDIR="/usr/libexec/ccache-wrappers"
ENV LANG="en_US.UTF-8"
ENV MAKE="/usr/bin/make"
ENV PYTHON="/usr/bin/python3"

RUN nosync dnf --quiet install -y \
                       mingw32-bzip2 \
                       mingw32-curl \
                       mingw32-gcc \
                       mingw32-gcc-c++ \
                       mingw32-gnutls \
                       mingw32-libvirt \
                       mingw32-pkg-config && \
    nosync dnf --quiet clean all -y && \
    rpm -qa | sort > /packages.txt && \
    mkdir -p /usr/libexec/ccache-wrappers && \
    ln -s /usr/bin/ccache /usr/libexec/ccache-wrappers/i686-w64-mingw32-c++ && \
    ln -s /usr/bin/ccache /usr/libexec/ccache-wrappers/i686-w64-mingw32-cc && \
    ln -s /usr/bin/ccache /usr/libexec/ccache-wrappers/i686-w64-mingw32-g++ && \
    ln -s /usr/bin/ccache /usr/libexec/ccache-wrappers/i686-w64-mingw32-gcc

ENV ABI="i686-w64-mingw32"
ENV CONFIGURE_OPTS="--host=i686-w64-mingw32"
