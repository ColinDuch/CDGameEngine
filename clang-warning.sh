#!/usr/bin/env bash
# Recompile tout le moteur avec Clang et resume ses avertissements.
# Clang voit des choses que GCC ignore, notamment les membres de classe
# jamais utilises (-Wunused-private-field).
#
# Usage :
#   ./clang-warning.sh          # recompile tout et resume
#   ./clang-warning.sh --quick  # ne recompile que ce qui a change
#
# Le build vit dans build-clang/ : il est jetable et n'affecte pas ./run.sh

set -u

ROOT="$(cd "$(dirname "$0")" && pwd)"
cd "$ROOT"

BUILD_DIR="build-clang"
LOG_DIR="logs"
STAMP="$(date +%d-%m-%Y_%Hh%M)"
LOG="$LOG_DIR/clang_$STAMP.log"

mkdir -p "$LOG_DIR"

if ! command -v clang++ > /dev/null; then
    echo "clang++ introuvable.  sudo pacman -S --needed clang"
    read -r -p "Entree pour fermer"
    exit 1
fi

# --- Configuration (rapide si le cache existe deja) ---
if [ ! -d "$BUILD_DIR" ]; then
    echo "=== Premiere configuration (telechargement de GLFW et GLM) ==="
    CXX=clang++ cmake -S . -B "$BUILD_DIR" 2>&1 | tee -a "$LOG"
fi

# --- Compilation ---
CLEAN="--clean-first"
if [ "${1:-}" = "--quick" ]; then
    CLEAN=""
    echo "=== Compilation incrementale (seuls les fichiers modifies) ==="
else
    echo "=== Compilation complete : tous les avertissements seront reemis ==="
fi

cmake --build "$BUILD_DIR" $CLEAN 2>&1 | tee -a "$LOG"

# --- Analyse : on ecarte les dependances, on ne garde que ton code ---
MINE="$(grep "warning:" "$LOG" | grep -v third_party | grep -v _deps)"

echo
echo "==================== RESUME ===================="

if [ -z "$MINE" ]; then
    echo "Aucun avertissement dans ton code. "
else
    TOTAL="$(printf '%s\n' "$MINE" | wc -l)"
    echo "$TOTAL avertissement(s) dans engine/ et game/"
    echo
    echo "--- Par categorie ---"
    printf '%s\n' "$MINE" | grep -o "\[-W[a-z-]*\]" | sort | uniq -c | sort -rn
    echo
    echo "--- Par fichier ---"
    printf '%s\n' "$MINE" | grep -o "^[^:]*" | xargs -r -n1 basename | sort | uniq -c | sort -rn
    echo
    echo "--- Code mort (a traiter en premier : suppression sans risque) ---"
    DEAD="$(printf '%s\n' "$MINE" | grep -E "unused-private-field|unused-variable|unused-function")"
    if [ -z "$DEAD" ]; then
        echo "aucun"
    else
        printf '%s\n' "$DEAD"
    fi
fi

echo "==============================================="
echo "Journal complet : $LOG"
read -r -p "Entree pour fermer"
