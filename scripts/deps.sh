#!/data/data/com.termux/files/usr/bin/bash
# deps.sh -- instala tudo que o nsdock precisa pra compilar e rodar,
# via pkg (Termux). Rode isso ANTES de build.sh e install.sh.

set -e

echo "=== nsdock deps ==="
echo "Atualizando índices de pacotes..."
pkg update -y

echo ""
echo "Instalando toolchain de compilação..."
pkg install -y clang make binutils

echo ""
echo "Instalando libcurl (usado em image/pull.c pra falar com o Docker Registry)..."
pkg install -y libcurl

echo ""
echo "Instalando tar (usado em image/layer.c pra extrair camadas .tar.gz)..."
pkg install -y tar

echo ""
echo "Instalando iproute2 (usado em network/veth.c e network/bridge.c)..."
pkg install -y iproute2

echo ""
echo "Instalando root-repo + tsu (acesso root de dentro do Termux)..."
pkg install -y root-repo
pkg install -y tsu

echo ""
echo "=== Verificando instalação ==="

check_bin() {
    if command -v "$1" > /dev/null 2>&1; then
        echo "  OK  $1 -> $(command -v "$1")"
    else
        echo "  FALTA  $1"
    fi
}

check_bin clang
check_bin make
check_bin tar
check_bin ip
check_bin su

echo ""
if [ -f "$PREFIX/include/curl/curl.h" ]; then
    echo "  OK  curl/curl.h encontrado"
else
    echo "  FALTA  curl/curl.h -- build.sh vai falhar sem isso"
fi

echo ""
echo "=== Deps concluído ==="
echo "Próximo passo: ./scripts/build.sh"
