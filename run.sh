#!/bin/bash
cd "$(dirname "$0")" || exit 1

# --- Journal horodate dans logs/ ---
mkdir -p logs
STAMP=$(date +%d-%m-%Y_%Hh%M)
LOG="logs/${STAMP}.log"
n=2
while [ -e "$LOG" ]; do          # deja un journal cette minute -> on numerote
    LOG="logs/${STAMP}_$n.log"
    n=$((n + 1))
done

# Tout ce qui suit est affiche ET enregistre dans le journal.
exec > >(tee -a "$LOG") 2>&1
echo "Journal : $LOG"

echo ""
echo "=== Compilation des shaders ==="
cd shaders || exit 1
for f in *.vert *.frag; do
    [ -e "$f" ] || continue
    case "$f" in
        shader.vert) out="vert.spv" ;;
        shader.frag) out="frag.spv" ;;
        *)           out="${f%.*}_${f##*.}.spv" ;;
    esac
    echo "  $f -> $out"
    if ! glslc "$f" -o "$out"; then
        cd ..
        echo ""
        echo "!!! ERREUR de compilation du shader : $f"
        read -p "Entree pour fermer"
        exit 1
    fi
done
cd ..

echo ""
echo "=== Compilation du moteur ==="
if ! cmake --build build; then
    echo ""
    echo "!!! ERREUR de compilation"
    read -p "Entree pour fermer"
    exit 1
fi

echo ""
echo "=== Lancement ==="
./build/bin/game "$1"
code=$?

echo ""
echo "=== Termine (code de sortie : $code) ==="
[ $code -ne 0 ] && echo "!!! Le moteur s'est arrete anormalement."
echo "Journal complet : $LOG"
read -p "Entree pour fermer"