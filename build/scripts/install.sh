#!/bin/bash
set -e

echo "==> Nsdock: instalando binários no dispositivo"

ARCH=$(uname -m)
case "$ARCH" in
  armv7l|armv8l) NSDOCK_ARCH="arm" ;;
  aarch64|arm64) NSDOCK_ARCH="arm64" ;;
  *) echo "Arquitetura não suportada: $ARCH"; exit 1 ;;
esac

echo "-> Arquitetura detectada: $NSDOCK_ARCH"

SRC_DIR="dist/$NSDOCK_ARCH"
DEST_DIR="/usr/local/bin"

if [ ! -d "$SRC_DIR" ]; then
  echo "Pasta $SRC_DIR não encontrada. Baixe os binários do GitHub Actions (artifact nsdock-$NSDOCK_ARCH) e coloque em $SRC_DIR antes de rodar este script."
  exit 1
fi

echo "-> Copiando binários para $DEST_DIR"
cp "$SRC_DIR"/runc "$DEST_DIR"/ 2>/dev/null || true
cp "$SRC_DIR"/containerd "$DEST_DIR"/ 2>/dev/null || true
cp "$SRC_DIR"/containerd-shim-runc-v2 "$DEST_DIR"/ 2>/dev/null || true
cp "$SRC_DIR"/dockerd "$DEST_DIR"/ 2>/dev/null || true
cp "$SRC_DIR"/docker "$DEST_DIR"/ 2>/dev/null || true
cp "$SRC_DIR"/buildkitd "$DEST_DIR"/ 2>/dev/null || true
cp "$SRC_DIR"/buildctl "$DEST_DIR"/ 2>/dev/null || true

chmod +x "$DEST_DIR"/runc "$DEST_DIR"/containerd "$DEST_DIR"/containerd-shim-runc-v2 \
         "$DEST_DIR"/dockerd "$DEST_DIR"/docker "$DEST_DIR"/buildkitd "$DEST_DIR"/buildctl 2>/dev/null || true

echo "-> Criando diretório de storage (rootfs-runtime)"
mkdir -p /var/lib/docker

echo "==> Instalação concluída."
echo "Rode 'dockerd &' pra iniciar o daemon, depois 'docker version' pra testar."
