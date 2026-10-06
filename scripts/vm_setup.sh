#!/usr/bin/env bash
set -euo pipefail
source "$(dirname "$0")/lib.sh"

BASE_DIR="$VM_DIR/base"
IMAGE_DIR="$VM_DIR/images"
SEED_DIR="$VM_DIR/cloud-init"
RUN_DIR="$VM_DIR/run"
BASE_IMAGE="$BASE_DIR/jammy-server-cloudimg-amd64.img"
GUEST_IMAGE="$IMAGE_DIR/edupci-guest.qcow2"
SEED_IMAGE="$SEED_DIR/seed.iso"
IMAGE_URL="https://cloud-images.ubuntu.com/jammy/current/jammy-server-cloudimg-amd64.img"
LOCAL_HOST_TOOLS="$VM_DIR/host-tools/usr/bin"

# Keep host package changes optional. If either cloud-init ISO helper is missing,
# download the Ubuntu packages and unpack them under ignored .vm/host-tools.
ensure_cloud_image_tools() {
  if command -v cloud-localds >/dev/null && command -v genisoimage >/dev/null; then
    return
  fi

  for tool in apt-get dpkg-deb; do
    command -v "$tool" >/dev/null || {
      echo "Missing $tool; install cloud-image-utils and genisoimage manually." >&2
      exit 1
    }
  done

  local download_dir="$VM_DIR/host-package-cache"
  mkdir -p "$download_dir" "$VM_DIR/host-tools"
  rm -f "$download_dir"/*.deb
  echo 'Fetching cloud-image-utils and genisoimage into project-local tools.'
  (cd "$download_dir" && apt-get download cloud-image-utils genisoimage)

  local package
  local packages=("$download_dir"/*.deb)
  [[ -e "${packages[0]}" ]] || { echo 'Host helper packages were not downloaded.' >&2; exit 1; }
  for package in "${packages[@]}"; do
    dpkg-deb -x "$package" "$VM_DIR/host-tools"
  done
  export PATH="$LOCAL_HOST_TOOLS:$PATH"

  command -v cloud-localds >/dev/null && command -v genisoimage >/dev/null || {
    echo 'cloud-localds or genisoimage is still unavailable after package extraction.' >&2
    exit 1
  }
}

ensure_cloud_image_tools

for tool in qemu-img qemu-system-x86_64 ssh-keygen curl; do
  command -v "$tool" >/dev/null || { echo "Required host tool missing: $tool" >&2; exit 1; }
done
[[ -r /dev/kvm && -w /dev/kvm ]] || { echo 'KVM is not accessible to this user.' >&2; exit 1; }
mkdir -p "$BASE_DIR" "$IMAGE_DIR" "$SEED_DIR" "$VM_DIR/keys" "$RUN_DIR" "$VM_DIR/logs" "$ROOT_DIR/evidence"

if [[ ! -f "$KEY_FILE" ]]; then
  ssh-keygen -q -t ed25519 -N '' -C 'edupci-qemu-guest' -f "$KEY_FILE"
  chmod 600 "$KEY_FILE"
fi
if [[ ! -f "$RUN_DIR/ssh_port" ]]; then
  selected_port=
  for candidate in $(seq 2222 2232); do
    if ! nc -z -w 1 127.0.0.1 "$candidate" >/dev/null 2>&1; then selected_port="$candidate"; break; fi
  done
  [[ -n "$selected_port" ]] || { echo 'No free localhost port in 2222-2232.' >&2; exit 1; }
  printf '%s\n' "$selected_port" > "$RUN_DIR/ssh_port"
fi

if [[ ! -f "$BASE_IMAGE" ]]; then
  curl --fail --location --retry 3 \
    --output "$BASE_DIR/SHA256SUMS.part" \
    "https://cloud-images.ubuntu.com/jammy/current/SHA256SUMS"
  mv "$BASE_DIR/SHA256SUMS.part" "$BASE_DIR/SHA256SUMS"
  expected_sha="$(awk '$2 == "*jammy-server-cloudimg-amd64.img" {print $1}' "$BASE_DIR/SHA256SUMS")"
  [[ -n "$expected_sha" ]] || { echo 'Ubuntu image checksum not found.' >&2; exit 1; }
  echo "Downloading Ubuntu 22.04 cloud image from $IMAGE_URL"
  curl --fail --location --retry 3 --output "$BASE_IMAGE.part" "$IMAGE_URL"
  actual_sha="$(sha256sum "$BASE_IMAGE.part" | cut -d' ' -f1)"
  if [[ "$actual_sha" != "$expected_sha" ]]; then
    rm -f "$BASE_IMAGE.part"
    echo 'Ubuntu cloud image checksum mismatch.' >&2
    exit 1
  fi
  mv "$BASE_IMAGE.part" "$BASE_IMAGE"
fi
if [[ ! -f "$GUEST_IMAGE" ]]; then
  qemu-img create -f qcow2 -F qcow2 -b "$BASE_IMAGE" "$GUEST_IMAGE" 20G
fi

PUBLIC_KEY="$(cat "$KEY_FILE.pub")"
cat > "$SEED_DIR/user-data" <<EOF
#cloud-config
hostname: edupci-guest
manage_etc_hosts: true
ssh_pwauth: false
users:
  - name: edupci
    gecos: EduPCI VM user
    groups: [adm, sudo]
    sudo: ALL=(ALL) NOPASSWD:ALL
    shell: /bin/bash
    lock_passwd: true
    ssh_authorized_keys:
      - $PUBLIC_KEY
EOF
cat > "$SEED_DIR/meta-data" <<'EOF'
instance-id: edupci-qemu-guest
local-hostname: edupci-guest
EOF
cloud-localds "$SEED_IMAGE" "$SEED_DIR/user-data" "$SEED_DIR/meta-data"
sha256sum "$BASE_IMAGE" > "$BASE_DIR/image.sha256"
printf 'VM files ready. SSH: edupci@127.0.0.1:%s\n' "$(vm_port)"
printf 'Base image SHA-256: %s\n' "$(cut -d' ' -f1 "$BASE_DIR/image.sha256")"
