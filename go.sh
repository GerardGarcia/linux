#!/bin/bash
set -e
make M=drivers/virtio CONFIG_VIRTIO_PCI=m modules
make M=net/vmw_vsock CONFIG_VIRTIO_VSOCKETS=m modules
make M=drivers/vhost CONFIG_VHOST_VSOCK=m modules

if lsmod | grep -q vhost_vsock
then
	sudo rmmod vhost_vsock
fi
if lsmod | grep -q virtio_transport_common
then
	sudo rmmod virtio_transport_common
fi
if lsmod | grep -q vsock
then
	sudo rmmod vsock
fi
if lsmod | grep -q vhost
then
	sudo rmmod vhost
fi
sudo insmod drivers/vhost/vhost.ko dyndbg==p
sudo insmod net/vmw_vsock/vsock.ko dyndbg==p
sudo insmod net/vmw_vsock/virtio_transport_common.ko dyndbg==p
sudo insmod drivers/vhost/vhost_vsock.ko dyndbg==p

usr/gen_init_cpio initramfs/cpio-list | gzip >initramfs.gz
kill_nc_vsock() {
	sudo pkill nc-vsock
}
trap kill_nc_vsock EXIT
sudo gdb -x ~/.gdbinit --ex r \
	--args ~/qemu/x86_64-softmmu/qemu-system-x86_64 \
	-enable-kvm \
	-device vhost-vsock-pci,id=vhost-vsock-pci0 \
	-kernel arch/x86_64/boot/bzImage \
	-initrd initramfs.gz \
	-append 'console=ttyS0' \
	-nographic
