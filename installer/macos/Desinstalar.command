#!/bin/bash
# Remove o BandChatLIVE do Mac (plugins VST3/AU, app e modelos do instalador).
fim() { echo; read -n 1 -s -r -p "Pressione qualquer tecla para fechar..."; echo; exit "${1:-0}"; }
clear
echo "=============================================="
echo "  Desinstalar BandChatLIVE"
echo "=============================================="
echo
echo "Isto remove os plugins VST3 e AU, o app e os modelos de voz instalados."
read -r -p "Continuar? [s/N] " r
case "$r" in s|S) ;; *) echo "Cancelado."; fim 0 ;; esac

echo
echo "O macOS vai pedir a sua senha de administrador."
sudo rm -rf "/Library/Audio/Plug-Ins/VST3/BandChatLIVE.vst3" \
            "/Library/Audio/Plug-Ins/Components/BandChatLIVE.component" \
            "/Applications/BandChatLIVE.app" \
            "/Library/Application Support/BandChatLIVE" || fim 1
for id in $(pkgutil --pkgs | grep '^com\.davisales\.bandchatlive'); do
  sudo pkgutil --forget "$id" >/dev/null
done
killall -9 AudioComponentRegistrar 2>/dev/null

USERDIR="$HOME/Library/Application Support/BandChatLIVE"
if [ -d "$USERDIR" ]; then
  echo
  read -r -p "Apagar também os modelos baixados e o histórico de transcrições? [s/N] " l
  if [ "$l" = "s" ] || [ "$l" = "S" ]; then
    rm -rf "$USERDIR"
  else
    echo "Mantido em: $USERDIR"
  fi
fi
echo
echo "Removido."
fim 0
