# THIS FILE WAS AUTO-GENERATED
#
#  $ lcitool manifest ci/manifest.yml
#
# https://gitlab.com/libvirt/libvirt-ci

function install_buildenv() {
    zypper update -y
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
           lua54-devel \
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
           systemtap-dtrace \
           tar \
           tcl-devel \
           util-linux \
           xorriso \
           xz \
           xz-devel \
           zlib-devel
    rm -f /usr/lib*/python3*/EXTERNALLY-MANAGED
    rpm -qa | sort > /packages.txt
    mkdir -p /usr/libexec/ccache-wrappers
    ln -s /usr/bin/ccache /usr/libexec/ccache-wrappers/c++
    ln -s /usr/bin/ccache /usr/libexec/ccache-wrappers/cc
    ln -s /usr/bin/ccache /usr/libexec/ccache-wrappers/clang
    ln -s /usr/bin/ccache /usr/libexec/ccache-wrappers/g++
    ln -s /usr/bin/ccache /usr/libexec/ccache-wrappers/gcc
}

export CCACHE_WRAPPERSDIR="/usr/libexec/ccache-wrappers"
export LANG="en_US.UTF-8"
export MAKE="/usr/bin/make"
export PYTHON="/usr/bin/python3"
