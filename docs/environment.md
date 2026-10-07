# Development environment

## Host

Recorded from the WSL2 host before guest setup on 2026-10-06 (Asia/Taipei):

| Item | Observed |
|---|---|
| Hostname | WSL2 host (local identifier omitted) |
| OS | Ubuntu 22.04.5 LTS |
| Architecture | `x86_64` |
| CPU | Intel Core i5-10400, 6 cores / 12 logical CPUs |
| Kernel | `6.6.87.2-microsoft-standard-WSL2` |
| Memory / free disk at audit | 7.7 GiB visible to WSL / 921 GiB free |
| QEMU | 6.2.0 (`Debian 1:6.2+dfsg-2ubuntu6.31`) |
| QEMU EDU model | Available as `-device edu` |
| KVM | `/dev/kvm` present; user is in `kvm` group |
| GCC | 11.4.0 |
| GNU Make | 4.3 |
| Python | `/usr/bin/python3` |
| SSH/SCP, curl, wget | Available |
| `qemu-img` | Available |
| `cloud-localds` / `genisoimage` | Not installed system-wide. Ubuntu Jammy `cloud-image-utils` 0.32-22-g45fe84a5-0ubuntu1 and `genisoimage` 9:1.1.11-3.2ubuntu1 were unpacked under ignored `.vm/host-tools`; seed image creation succeeded. On a fresh setup, `make vm` fetches these packages with `apt-get download` and extracts them locally without changing the host installation. |
| SSH forwarding port | 2222 was free and is used by the guest |
| Ubuntu guest image SHA-256 | `012d81fade7e8ff4428fc35dfeee711d66d1f47d5ea9c5c8ced96a85b959835c`; matched the Jammy `SHA256SUMS` entry |

The WSL2 kernel is only the development host. Driver builds that target a kernel and all module loading, MMIO, IRQ, and DMA verification belong inside the QEMU guest. The scripts never insert this module into the WSL2 kernel.

## Guest

Recorded in `evidence/guest_environment.txt` after a successful first boot and provisioning:

| Item | Observed |
|---|---|
| OS | Ubuntu 22.04.5 LTS (Jammy Jellyfish) |
| Architecture / kernel | x86_64; Linux `5.15.0-198-generic` |
| Compiler / Make | GCC 11.4.0; GNU Make 4.3 |
| EDU device | `00:01.0`, PCI ID `1234:11e8`, revision 10 |
| Kernel build headers | `/lib/modules/5.15.0-198-generic/build` exists |
| Guest resources | 2 vCPU and 2 GiB RAM, configured by QEMU |

The exact host command output is in `evidence/host_environment.txt`; the pre-driver PCI listing is in `evidence/lspci_before_driver.txt`.
