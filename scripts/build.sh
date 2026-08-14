#!/data/data/com.termux/files/usr/bin/bash
# build.sh -- compila o nsdock inteiro (CLI + core) num único binário

set -e

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BUILD_DIR="$ROOT_DIR/build"
OUT_BIN="$BUILD_DIR/nsdock"

echo "nsdock build -- diretório: $ROOT_DIR"

mkdir -p "$BUILD_DIR"

# todos os .c do core, network, image e cli entram na mesma compilação --
# por enquanto sem daemon separado (nsdockd.c ainda não existe/não entra aqui)
SOURCES=$(find "$ROOT_DIR/src/core" "$ROOT_DIR/src/network" "$ROOT_DIR/src/image" "$ROOT_DIR/src/cli" -name "*.c")

echo "Arquivos fonte encontrados:"
echo "$SOURCES"

CC=${CC:-cc}
CFLAGS="-Wall -Wextra -O2 -I$ROOT_DIR/include"
LDFLAGS="-lcurl"

echo ""
echo "Compilando..."

$CC $CFLAGS $SOURCES -o "$OUT_BIN" $LDFLAGS

echo ""
echo "Build concluído: $OUT_BIN"
echo "Rode com: su -c $OUT_BIN run alpine"
