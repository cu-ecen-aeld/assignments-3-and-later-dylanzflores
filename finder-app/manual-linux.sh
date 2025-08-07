#!/bin/bash
# Kernel and rootfs build script — cleaned and fixed for smooth execution

set -euo pipefail

# Default output directory, override by argument
OUTDIR=${1:-/tmp/aeld}
KERNEL_REPO=git://git.kernel.org/pub/scm/linux/kernel/git/stable/linux-stable.git
KERNEL_VERSION=v5.15.163
BUSYBOX_VERSION=1_33_1
ARCH=arm64
FINDER_APP_DIR=$(realpath "$(dirname "$0")")
CROSS_COMPILE=aarch64-linux-gnu-

echo "Using output directory: $OUTDIR"

mkdir -p "$OUTDIR"
mkdir -p "$OUTDIR/rootfs/home"

# Clone Linux kernel if not present
if [ ! -d "$OUTDIR/linux-stable" ]; then
    echo "Cloning Linux kernel $KERNEL_VERSION..."
    git clone --depth 1 --branch "$KERNEL_VERSION" "$KERNEL_REPO" "$OUTDIR/linux-stable"
fi

# Build kernel Image if not already built
if [ ! -f "$OUTDIR/linux-stable/arch/$ARCH/boot/Image" ]; then
    echo "Building Linux kernel..."
    pushd "$OUTDIR/linux-stable" > /dev/null
    git checkout "$KERNEL_VERSION"
    make ARCH=$ARCH CROSS_COMPILE=$CROSS_COMPILE mrproper
    make ARCH=$ARCH CROSS_COMPILE=$CROSS_COMPILE defconfig
    make -j$(nproc) ARCH=$ARCH CROSS_COMPILE=$CROSS_COMPILE all
    cp arch/$ARCH/boot/Image "$OUTDIR/"
    popd > /dev/null
fi

# Clean and create rootfs directory structure
if [ -d "$OUTDIR/rootfs" ]; then
    echo "Cleaning existing rootfs..."
    sudo rm -rf "$OUTDIR/rootfs"
fi

echo "Creating rootfs directory structure..."
mkdir -p "$OUTDIR/rootfs"/{bin,dev,etc,home,lib,lib64,proc,sbin,sys,tmp,usr,var}
mkdir -p "$OUTDIR/rootfs/usr"/{bin,lib,sbin}
mkdir -p "$OUTDIR/rootfs/var/log"

# Clone and build BusyBox
if [ -d "$OUTDIR/busybox" ]; then
    rm -rf "$OUTDIR/busybox"
fi

echo "Cloning and building BusyBox $BUSYBOX_VERSION..."
git clone git://busybox.net/busybox.git "$OUTDIR/busybox"
pushd "$OUTDIR/busybox" > /dev/null
git checkout "$BUSYBOX_VERSION"
make distclean
make defconfig
sed -i 's/# CONFIG_STATIC is not set/CONFIG_STATIC=y/' .config
make -j$(nproc) ARCH=$ARCH CROSS_COMPILE=$CROSS_COMPILE
make CONFIG_PREFIX="$OUTDIR/rootfs" ARCH=$ARCH CROSS_COMPILE=$CROSS_COMPILE install
popd > /dev/null

# Check BusyBox binary dependencies (should be none for static)
echo "BusyBox library dependencies:"
$CROSS_COMPILE"readelf" -a "$OUTDIR/rootfs/bin/busybox" | grep "program interpreter" || true
$CROSS_COMPILE"readelf" -a "$OUTDIR/rootfs/bin/busybox" | grep "Shared library" || true

# Copy required libraries (defensive, in case static build is incomplete)
SYSROOT=$($CROSS_COMPILE"gcc" -print-sysroot)

copy_lib() {
    local pattern=$1
    local dest=$2
    mkdir -p "$dest"
    local found=$(find "$SYSROOT" -name "$pattern" | head -n1)
    if [ -n "$found" ]; then
        echo "Copying $found to $dest"
        cp -a "$found" "$dest"
    else
        echo "Warning: $pattern not found in sysroot"
    fi
}

copy_lib "ld-linux-aarch64.so.1" "$OUTDIR/rootfs/lib"
copy_lib "libc.so.*" "$OUTDIR/rootfs/lib64"
copy_lib "libm.so.*" "$OUTDIR/rootfs/lib64"
copy_lib "libresolv.so.*" "$OUTDIR/rootfs/lib64"

# Create device nodes
echo "Creating device nodes..."
sudo mknod -m 666 "$OUTDIR/rootfs/dev/null" c 1 3
sudo mknod -m 622 "$OUTDIR/rootfs/dev/console" c 5 1

# Build finder utilities
echo "Building finder utilities..."
pushd "$FINDER_APP_DIR" > /dev/null
make clean
make CROSS_COMPILE=$CROSS_COMPILE

if [ ! -f writer ]; then
    echo "Error: writer utility build failed!"
    exit 1
fi
popd > /dev/null

# Copy finder scripts and files into rootfs home
echo "Copying finder scripts and config..."
cp "$FINDER_APP_DIR"/{finder.sh,finder-test.sh,autorun-qemu.sh,writer.sh,writer} "$OUTDIR/rootfs/home/"
cp -rL "$FINDER_APP_DIR/conf" "$OUTDIR/rootfs/home/"

# Fix relative paths in finder-test.sh
sed -i 's|\.\./conf|conf|g' "$OUTDIR/rootfs/home/finder-test.sh"

# Set ownership of rootfs to root
echo "Setting ownership of rootfs to root..."
sudo chown -R root:root "$OUTDIR/rootfs"

# Create initramfs archive
echo "Creating initramfs.cpio.gz..."
pushd "$OUTDIR/rootfs" > /dev/null
sudo find . | cpio -H newc -ov --owner root:root > "$OUTDIR/initramfs.cpio"
popd > /dev/null
gzip -f "$OUTDIR/initramfs.cpio"

echo "✅ Build complete!"
echo "Kernel Image: $OUTDIR/Image"
echo "Initramfs: $OUTDIR/initramfs.cpio.gz"

