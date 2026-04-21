#!/usr/bin/env bash
set -euo pipefail

# --- Configuration ---
SLANG_VERSION="2026.5.2"
# We hardcode macos-aarch64 for Apple Silicon (M1/M2/M3)
SLANG_ARCH="macos-aarch64" 
# Use the exact path from your previous error log so CMake finds it
DEST_DIR="build/_deps/Slang-linux-aarch64-${SLANG_VERSION}"
TMP_ZIP="/tmp/slang.zip"

echo "Downloading Slang Compiler v${SLANG_VERSION} for macOS..."
URL="https://github.com/shader-slang/slang/releases/download/v${SLANG_VERSION}/slang-${SLANG_VERSION}-${SLANG_ARCH}.zip"

curl -L -o "${TMP_ZIP}" "${URL}"

echo "Cleaning destination: ${DEST_DIR}"
rm -rf "${DEST_DIR}"
mkdir -p "${DEST_DIR}"

echo "Extracting Slang..."
unzip -q "${TMP_ZIP}" -d "${DEST_DIR}"

# --- CRITICAL MAC STEP ---
# Remove the 'quarantine' attribute so macOS allows the binary to run
echo "Authorizing binaries (removing quarantine)..."
chmod +x "${DEST_DIR}/bin/slangc"
xattr -d com.apple.quarantine "${DEST_DIR}/bin/slangc" || true

echo "Cleaning up..."
rm "${TMP_ZIP}"

echo "Done! slangc is now at ${DEST_DIR}/bin/slangc"
"${DEST_DIR}/bin/slangc" -version
