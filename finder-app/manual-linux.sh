#!/bin/bash
# Script outline to install and build kernel.
# Author: Siddhant Jajoo. Updated by Dylan.

set -e
set -u

OUTDIR=/tmp/aeld
KERNEL_REPO=git://git.kernel.org/pub/scm/linux/kernel/git/stable/linux-stable.git
KERNEL_VERSION=v5.15.163
BUSYBOX_VERSION=1_33_1
FINDER_APP_DIR=$(realpath $(dirname $0))
ARCH=arm64
CROSS_COMPILE=aarch64-linux-gnu-

if [ $# -lt 1 ]; then
    echo "Using default directory ${OUTDIR} for output"
else
    OUTDIR=$1
    echo "Using passed directory ${OUTDIR} for output"
fi
echo "OUTDIR is set to: ${OUTDIR}"
mkdir -p ${OUTDIR}
mkdir -p ${OUTDIR}/rootfs/home
if ! command -v "${CROSS_COMPILE}gcc" &>/dev/null; then
    echo " ${CROSS_COMPILE}gcc not found. Installing..."
    sudo apt-get update
    sudo apt-get install -y gcc-aarch64-linux-gnu g++-aarch64-linux-gnu
fi

cd "$OUTDIR"
if [ ! -d "${OUTDIR}/linux-stable" ]; then
    echo "CLONING GIT LINUX STABLE VERSION ${KERNEL_VERSION} IN ${OUTDIR}"
    git clone ${KERNEL_REPO} --depth 1 --single-branch --branch ${KERNEL_VERSION}
fi

if [ ! -e ${OUTDIR}/linux-stable/arch/${ARCH}/boot/Image ]; then
    cd linux-stable
    echo "Checking out version ${KERNEL_VERSION}"
    git checkout ${KERNEL_VERSION}

    make ARCH=${ARCH} CROSS_COMPILE=${CROSS_COMPILE} mrproper
    make ARCH=${ARCH} CROSS_COMPILE=${CROSS_COMPILE} defconfig
    make -j$(nproc) ARCH=${ARCH} CROSS_COMPILE=${CROSS_COMPILE} all
    cp arch/${ARCH}/boot/Image ${OUTDIR}/
fi

echo "Creating the staging directory for the root filesystem"
cd "$OUTDIR"
if [ -d "${OUTDIR}/rootfs" ]; then
    echo "Deleting rootfs directory at ${OUTDIR}/rootfs and starting over"
    sudo rm -rf ${OUTDIR}/rootfs
fi

mkdir rootfs
cd rootfs
mkdir -p bin dev etc home lib lib64 proc sbin sys tmp usr var
mkdir -p usr/bin usr/lib usr/sbin
mkdir -p var/log

rm -rf "${OUTDIR}/busybox"

CLONE_ATTEMPTS=3
for i in $(seq 1 $CLONE_ATTEMPTS); do
    echo "📦 Attempt $i: Cloning BusyBox..."
    git clone https://busybox.net/git/busybox.git "${OUTDIR}/busybox" && break
    echo "⚠️ Clone attempt $i failed. Retrying in 5s..."
    rm -rf "${OUTDIR}/busybox"
    sleep 5
done

if [ ! -d "${OUTDIR}/busybox" ]; then
    echo "❌ Failed to clone BusyBox after $CLONE_ATTEMPTS attempts."
    exit 1
fi

cd "${OUTDIR}/busybox"

git checkout ${BUSYBOX_VERSION}
make distclean
make defconfig
sed -i 's/# CONFIG_STATIC is not set/CONFIG_STATIC=y/' .config
make ARCH=${ARCH} CROSS_COMPILE=${CROSS_COMPILE} -j$(nproc)
make CONFIG_PREFIX=${OUTDIR}/rootfs ARCH=${ARCH} CROSS_COMPILE=${CROSS_COMPILE} install

echo "Library dependencies:"
${CROSS_COMPILE}readelf -a ${OUTDIR}/rootfs/bin/busybox | grep "program interpreter" || true
${CROSS_COMPILE}readelf -a ${OUTDIR}/rootfs/bin/busybox | grep "Shared library" || true

SYSROOT=$(${CROSS_COMPILE}gcc -print-sysroot)

# Helper to copy a required library by filename pattern
copy_lib() {
    local LIBNAME=$1
    local DESTDIR=$2
    LIBFILE=$(find "$SYSROOT" -name "$LIBNAME" | head -n 1)
    if [ -z "$LIBFILE" ]; then
        echo "ERROR: Could not find $LIBNAME in sysroot!"
        exit 1
    fi
    echo "Copying $LIBFILE to $DESTDIR"
    cp -a "$LIBFILE" "$DESTDIR"
}

# Copy required libraries
echo "Copying library dependencies..."
mkdir -p ${OUTDIR}/rootfs/lib
mkdir -p ${OUTDIR}/rootfs/lib64
copy_lib "ld-linux-aarch64.so.1" "${OUTDIR}/rootfs/lib/"
copy_lib "libc.so.*" "${OUTDIR}/rootfs/lib64/"
copy_lib "libm.so.*" "${OUTDIR}/rootfs/lib64/"
copy_lib "libresolv.so.*" "${OUTDIR}/rootfs/lib64/"

# Make device nodes
sudo mknod -m 666 ${OUTDIR}/rootfs/dev/null c 1 3
sudo mknod -m 622 ${OUTDIR}/rootfs/dev/console c 5 1

# Build the writer utility
cd ${FINDER_APP_DIR}
make clean
rm -f writer *.o
make CROSS_COMPILE=${CROSS_COMPILE}

if [ ! -f writer ]; then
    echo "ERROR: writer binary not built!"
    exit 1
fi

# Copy finder scripts and data
echo "Copying finder scripts and files to rootfs/home"
cp finder.sh finder-test.sh autorun-qemu.sh writer.sh writer ${OUTDIR}/rootfs/home/
cp -rL conf ${OUTDIR}/rootfs/home/
#mkdir -p ${OUTDIR}/rootfs/home/conf
#cp ../conf/assignment.txt ../conf/username.txt ${OUTDIR}/rootfs/home/conf/
sed -i 's|\.\./conf|conf|g' ${OUTDIR}/rootfs/home/finder-test.sh

# Set ownership
sudo chown -R root:root ${OUTDIR}/rootfs

# Create initramfs
echo "Creating initramfs.cpio.gz"
cd ${OUTDIR}/rootfs
sudo find . | cpio -H newc -ov --owner root:root > ${OUTDIR}/initramfs.cpio
gzip -f ${OUTDIR}/initramfs.cpio

echo "✅ Build complete. Initramfs and kernel Image are ready."

