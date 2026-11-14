#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
IMAGE="${AMIGA_GCC_IMAGE:-walkero/amigagccondocker:os4-gcc13-3.4.0-amd64}"

echo "=== Cross-Compiling VortexTeletext (AmigaOS 4.1 PPC) via Docker ==="
docker run --rm \
    -v "${SCRIPT_DIR}:/work" \
    -w /work \
    "${IMAGE}" \
    bash -c "make clean && make"

KYVOS_DOWNLOAD_DIR="${KYVOS_DOWNLOAD_DIR:-${HOME}/kyvos-2.2.0-linux64/amigaos4/Download}"
if [ -d "${KYVOS_DOWNLOAD_DIR}" ]; then
    TARGET_DRAWER="${KYVOS_DOWNLOAD_DIR}/VortexTeletext"
    mkdir -p "${TARGET_DRAWER}"
    cp -f "${SCRIPT_DIR}/VortexTeletext" "${TARGET_DRAWER}/VortexTeletext"
    cp -f "${SCRIPT_DIR}/VortexTeletext.stripped" "${TARGET_DRAWER}/VortexTeletext.stripped"
    echo "[+] Deployed to ${TARGET_DRAWER}"
fi

echo "[OK] Build complete: ${SCRIPT_DIR}/VortexTeletext"
ls -lh "${SCRIPT_DIR}/VortexTeletext" "${SCRIPT_DIR}/VortexTeletext.stripped"
