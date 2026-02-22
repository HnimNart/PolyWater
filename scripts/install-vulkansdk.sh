#!/usr/bin/env bash
set -euo pipefail

SDK_VERSION="1.4.335.0"
SDK_URL="https://sdk.lunarg.com/sdk/download/${SDK_VERSION}/linux/vulkansdk-linux-x86_64-${SDK_VERSION}.tar.xz"
TMP_FILE="/tmp/vulkansdk-${SDK_VERSION}.tar.xz"
DEST_DIR="ext/vulkansdk"

echo "Downloading Vulkan SDK ${SDK_VERSION}..."
echo "URL: ${SDK_URL}"
curl -L -o "${TMP_FILE}" "${SDK_URL}"

echo "Preparing destination directory: ${DEST_DIR}"
rm -rf "${DEST_DIR}"
mkdir -p "${DEST_DIR}"

echo "Extracting SDK..."
tar -xJf "${TMP_FILE}" -C "${DEST_DIR}" --strip-components=1

echo "Cleaning up..."
rm -f "${TMP_FILE}"

echo "Vulkan SDK ${SDK_VERSION} installed into ${DEST_DIR}"

