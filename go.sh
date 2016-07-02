#!/bin/bash
set -e

mode="$1"

rmmod_nofail() {
	if lsmod | grep -q "$1"
	then
		sudo rmmod "$1" || true
	fi
}

vhost_start() {
	echo "Loading modules"
	sudo insmod net/vmw_vsock/vsock.ko dyndbg==p
	sudo insmod net/vmw_vsock/virtio_transport_common.ko dyndbg==p
	sudo insmod drivers/vhost/vhost_vsock.ko dyndbg==p
    # sudo insmod drivers/net/vsockmon.ko dyndbg==p
}

vhost_stop() {
    echo "Unloading modules"
    # rmmod_nofail vsockmon
	rmmod_nofail vhost_vsock
	rmmod_nofail virtio_transport_common
	rmmod_nofail vsock
	rmmod_nofail vhost
}

vsockmon_start() {
    echo "Starting vsockmon devices"
    sudo ip link add type vsockmon
    sudo ip link set vsockmon0 up
}

vsockmon_stop() {
    echo "Stopping vsockmon devices"
    sudo ip link set vsockmon0 down || true
    sudo ip link del dev vsockmon0 || true
}

build() {
    echo "Building initramfs"
    sed "s/\$KVER/$(uname -r)/g" initramfs/cpio-list | usr/gen_init_cpio - | gzip >initramfs.gz
    # echo "Building vsockmon"
    # gcc vsockmon.c -o vsockmon -I usr/include
    gcc stress_vsock.c -o stress_vsock -lpthread
}

# Run from outer VM
build_modules() {
    echo "Building modules"
    # make M=drivers/net/ CONFIG_VSOCKMON=m modules
    make M=drivers/vhost/ CONFIG_VHOST_VOSCK=m modules
    make M=net/vmw_vsock CONFIG_VIRTIO_VSOCKETS=m modules
}

kill_nc_vsock() {
    echo "Killing nc-vsock"
    sudo pkill nc-vsock
}

while getopts 'mbrl' flag; do
	case "$flag" in
        m)
            build_modules
            ;;
        b)
            build
            ;;
        r)
            vhost_stop
            vhost_start
            ;;
        l)
            # trap kill_nc_vsock EXIT
            LAUNCH="1"
            ;;
		*) exit 1
            ;;
	esac
done

if [[ -n "$LAUNCH" ]]; then
    tmux new-session -c . -d \
        sudo ~/qemu/x86_64-softmmu/qemu-system-x86_64 \
            -m 512 \
            -enable-kvm \
            -device vhost-vsock-pci,id=vhost-vsock-pci0,guest-cid=3 \
            -kernel arch/x86_64/boot/bzImage \
            -initrd initramfs.gz \
            -append "console=ttyS0" \
            -nographic
    tmux split-window -v \
        sudo bash
    tmux -2 attach-session -d
fi
