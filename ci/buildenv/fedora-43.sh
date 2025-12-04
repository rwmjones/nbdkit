# THIS FILE WAS AUTO-GENERATED
#
#  $ lcitool manifest ci/manifest.yml
#
# https://gitlab.com/libvirt/libvirt-ci

function install_buildenv() {
    dnf update -y
    dnf install -y \
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
