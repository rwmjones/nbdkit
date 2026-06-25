# THIS FILE WAS AUTO-GENERATED
#
#  $ lcitool manifest ci/manifest.yml
#
# https://gitlab.com/libvirt/libvirt-ci

FROM registry.fedoraproject.org/fedora:rawhide

RUN dnf --quiet update -y --nogpgcheck fedora-gpg-keys && \
    dnf --quiet install -y nosync && \
    printf '#!/bin/sh\n\
if test -d /usr/lib64\n\
then\n\
    export LD_PRELOAD=/usr/lib64/nosync/nosync.so\n\
else\n\
    export LD_PRELOAD=/usr/lib/nosync/nosync.so\n\
fi\n\
exec "$@"\n' > /usr/bin/nosync && \
    chmod +x /usr/bin/nosync && \
    nosync dnf --quiet distro-sync -y && \
    nosync dnf --quiet install -y \
                       autoconf \
                       automake \
                       bash \
                       bash-completion-devel \
                       bzip2 \
                       bzip2-devel \
                       ca-certificates \
                       cargo \
                       ccache \
                       clang \
                       clippy \
                       e2fsprogs \
                       expect \
                       gawk \
                       gcc \
                       gcc-c++ \
                       genisoimage \
                       git \
                       glibc-langpack-en \
                       glibc-utils \
                       gnutls-devel \
                       golang \
                       gzip \
                       iproute \
                       jq \
                       libcurl-devel \
                       libguestfs-devel \
                       libnbd-devel \
                       libselinux-devel \
                       libssh-devel \
                       libtool \
                       libtorrent-devel \
                       libvirt-devel \
                       libzstd-devel \
                       lua-devel \
                       make \
                       ocaml \
                       perl-ExtUtils-Embed \
                       perl-Pod-Simple \
                       perl-base \
                       perl-devel \
                       perl-podlators \
                       pkgconfig \
                       python3 \
                       python3-boto3 \
                       python3-devel \
                       python3-flake8 \
                       python3-libnbd \
                       python3-packaging \
                       qemu-img \
                       rust \
                       socat \
                       systemtap-sdt-dtrace \
                       tar \
                       tcl-devel \
                       util-linux \
                       xorriso \
                       xz \
                       xz-devel \
                       zlib-devel && \
    nosync dnf --quiet autoremove -y && \
    nosync dnf --quiet clean all -y && \
    rm -f /usr/lib*/python3*/EXTERNALLY-MANAGED && \
    rpm -qa | sort > /packages.txt && \
    mkdir -p /usr/libexec/ccache-wrappers && \
    ln -s /usr/bin/ccache /usr/libexec/ccache-wrappers/c++ && \
    ln -s /usr/bin/ccache /usr/libexec/ccache-wrappers/cc && \
    ln -s /usr/bin/ccache /usr/libexec/ccache-wrappers/clang && \
    ln -s /usr/bin/ccache /usr/libexec/ccache-wrappers/g++ && \
    ln -s /usr/bin/ccache /usr/libexec/ccache-wrappers/gcc

ENV CCACHE_WRAPPERSDIR="/usr/libexec/ccache-wrappers"
ENV LANG="en_US.UTF-8"
ENV MAKE="/usr/bin/make"
ENV PYTHON="/usr/bin/python3"
