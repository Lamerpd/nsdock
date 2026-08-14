#!/data/data/com.termux/files/usr/bin/bash
# install.sh -- prepara o ambiente pro nsdock rodar no device
# (checa root, cria diretórios de dados, instala o binário)

set -e

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BUILD_BIN="$ROOT_DIR/build/nsdock"
INSTALL_BIN="/data/local/nsdock/bin/nsdock"
DATA_ROOT="/data/local/nsdock"

echo "=== nsdock install ==="

# 1. Checa se o binário foi compilado
if [ ! -f "$BUILD_BIN" ]; then
    echo "Erro: binário não encontrado em $BUILD_BIN"
    echo "Rode ./scripts/build.sh primeiro."
    exit 1
fi

# 2. Checa acesso root (su precisa estar disponível e funcionando)
echo "Checando acesso root..."
if ! su -c "id" > /dev/null 2>&1; then
    echo "Erro: não foi possível obter acesso root via 'su'."
    echo "O nsdock precisa de um device rooted (Magisk, KernelSU, etc.)."
    exit 1
fi
echo "Root OK."

# 3. Checa suporte a overlayfs no kernel
echo "Checando suporte a overlayfs..."
if ! su -c "grep -q overlay /proc/filesystems"; then
    echo "Aviso: overlayfs não aparece em /proc/filesystems."
    echo "O nsdock pode não funcionar nesse kernel. Continuando mesmo assim..."
fi

# 4. Checa cgroups (v1 ou v2)
echo "Checando cgroups..."
if su -c "test -f /sys/fs/cgroup/cgroup.controllers"; then
    echo "cgroups v2 detectado."
elif su -c "test -d /sys/fs/cgroup/memory"; then
    echo "cgroups v1 detectado."
else
    echo "Aviso: nenhum cgroup utilizável encontrado. Limites de recursos não vão funcionar."
fi

# 5. Cria a estrutura de diretórios de dados (como root, já que /data/local exige)
echo "Criando diretórios em $DATA_ROOT..."
su -c "mkdir -p $DATA_ROOT/bin $DATA_ROOT/containers $DATA_ROOT/images"

# 6. Copia o binário compilado pro destino final e ajusta permissão
echo "Instalando binário..."
su -c "cp $BUILD_BIN $INSTALL_BIN"
su -c "chmod 755 $INSTALL_BIN"

echo ""
echo "=== Instalação concluída ==="
echo "Rode com: su -c $INSTALL_BIN run alpine"
echo ""
echo "Dica: adicione um alias no seu .bashrc do Termux pra facilitar:"
echo "  alias nsdock='su -c $INSTALL_BIN'"
