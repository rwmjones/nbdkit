# THIS FILE WAS AUTO-GENERATED
#
#  $ lcitool manifest ci/manifest.yml
#
# https://gitlab.com/libvirt/libvirt-ci

FROM registry.opensuse.org/opensuse/leap:15.6

RUN zypper update -y && \
    zypper addrepo -fc https://download.opensuse.org/update/leap/15.6/backports/openSUSE:Backports:SLE-15-SP6:Update.repo && \
    zypper install -y \
           autoconf \
           automake \
           awk \
           bash \
           bash-completion-devel \
           bzip2 \
           ca-certificates \
           cargo \
           ccache \
           clang \
           e2fsprogs \
           expect \
           gcc \
           gcc-c++ \
           git \
           glibc-locale \
           go \
           gzip \
           iproute2 \
           jq \
           libbz2-devel \
           libcurl-devel \
           libgnutls-devel \
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
           mkisofs \
           ocaml \
           perl \
           perl-base \
           pkgconfig \
           python3-base \
           python3-boto3 \
           python3-devel \
           python3-flake8 \
           python3-libnbd \
           python3-packaging \
           qemu-tools \
           rust \
           socat \
           systemtap-sdt-devel \
           tar \
           tcl-devel \
           util-linux \
           xorriso \
           xz \
           xz-devel \
           zlib-devel && \
    zypper clean --all && \
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
