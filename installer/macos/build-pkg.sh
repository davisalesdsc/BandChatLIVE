#!/bin/bash
# Monta o instalador .pkg do macOS (um arquivo só, com escolha de componentes).
# Um instalador por tipo de Mac: Apple Silicon (arm64) ou Intel (x86_64).
#
#   build-pkg.sh <pasta_com_os_bundles> <pasta_dos_modelos> <versao> <arm64|x86_64> <saida.pkg>
#
# <pasta_com_os_bundles> precisa ter (da arquitetura indicada, já assinados):
#     BandChatLIVE.vst3   BandChatLIVE.component   BandChatLIVE.app
# <pasta_dos_modelos> pode ter ggml-base-q5_1.bin e/ou ggml-small-q5_1.bin
# (cada um que existir vira uma opção no instalador).
#
# Assinatura opcional: com MAC_INSTALLER_IDENTITY="Developer ID Installer: ..."
# no ambiente, o .pkg sai assinado (necessário para notarizar).
set -euo pipefail

BUNDLES="$(cd "$1" && pwd)"
MODELS=""
if [ -d "$2" ]; then MODELS="$(cd "$2" && pwd)"; fi
VERSION="$3"
ARCH="$4"
OUT="$5"
case "$ARCH" in
  arm64)  MAC="Apple Silicon"; OUTRO="Intel";         QUER_ARM=true ;;
  x86_64) MAC="Intel";         OUTRO="Apple Silicon"; QUER_ARM=false ;;
  *) echo "arquitetura inválida: $ARCH (use arm64 ou x86_64)"; exit 1 ;;
esac
# confere que os bundles são mesmo da arquitetura do instalador
for b in "$BUNDLES"/*.vst3 "$BUNDLES"/*.component "$BUNDLES"/*.app; do
  bin="$b/Contents/MacOS/$(basename "${b%.*}")"
  if [ -f "$bin" ] && command -v lipo >/dev/null && ! lipo "$bin" -verify_arch "$ARCH" 2>/dev/null; then
    echo "ERRO: $bin não é $ARCH"; exit 1
  fi
done
HERE="$(cd "$(dirname "$0")" && pwd)"

NAME="BandChatLIVE"
ID="com.davisales.bandchatlive"
SUPPORT="/Library/Application Support/BandChatLIVE"

WORK="$(mktemp -d)"
trap 'rm -rf "$WORK"' EXIT
mkdir -p "$WORK/pkgs"

# --- componente de bundle (plugin/app): sem "relocação" ----------------------
# Por padrão o Installer procura o bundle onde o usuário o tiver movido e
# atualiza lá; para plugin isso espalha cópias. Desligamos.
bundle_pkg () {   # <arquivo_bundle> <destino> <id> <scripts|-> <saida>
  local bundle="$1" dest="$2" id="$3" scripts="$4" out="$5"
  local root="$WORK/root-$id"
  mkdir -p "$root"
  ditto "$BUNDLES/$bundle" "$root/$bundle"
  # configuração do componente escrita à mão (o "pkgbuild --analyze" nem
  # sempre reconhece .vst3/.component como bundle e devolve lista vazia)
  cat > "$WORK/$id.plist" <<PLIST
<?xml version="1.0" encoding="UTF-8"?>
<!DOCTYPE plist PUBLIC "-//Apple//DTD PLIST 1.0//EN" "http://www.apple.com/DTDs/PropertyList-1.0.dtd">
<plist version="1.0">
<array>
  <dict>
    <key>RootRelativeBundlePath</key><string>$bundle</string>
    <key>BundleIsRelocatable</key><false/>
    <key>BundleIsVersionChecked</key><false/>
    <key>BundleHasStrictIdentifier</key><false/>
    <key>BundleOverwriteAction</key><string>upgrade</string>
  </dict>
</array>
</plist>
PLIST
  plutil -lint "$WORK/$id.plist" >/dev/null
  local extra=()
  [ "$scripts" != "-" ] && extra=(--scripts "$scripts")
  pkgbuild --root "$root" --component-plist "$WORK/$id.plist" \
           --identifier "$id" --version "$VERSION" --install-location "$dest" \
           ${extra[@]+"${extra[@]}"} "$WORK/pkgs/$out" >/dev/null
  echo "  componente: $out ($bundle -> $dest)"
}

# scripts: apagam a versão anterior antes de copiar (arquivo que sumiu numa
# versão nova não fica para trás) e, no AU, avisam o macOS do plugin novo
mkscripts () {   # <nome> <caminho_a_apagar> [au]
  local d="$WORK/scripts-$1"
  mkdir -p "$d"
  cat > "$d/preinstall" <<EOF
#!/bin/bash
rm -rf "$2"
exit 0
EOF
  if [ "${3:-}" = "au" ]; then
    cat > "$d/postinstall" <<'EOF'
#!/bin/bash
# o Logic/GarageBand/MainStage só enxergam AU novo depois disto
killall -9 AudioComponentRegistrar 2>/dev/null
exit 0
EOF
  fi
  chmod +x "$d"/*
  echo "$d"
}

echo "Montando o instalador $NAME $VERSION"
bundle_pkg "$NAME.vst3"      "/Library/Audio/Plug-Ins/VST3"       "$ID.vst3" \
           "$(mkscripts vst3 "/Library/Audio/Plug-Ins/VST3/$NAME.vst3")" vst3.pkg
bundle_pkg "$NAME.component" "/Library/Audio/Plug-Ins/Components" "$ID.au" \
           "$(mkscripts au "/Library/Audio/Plug-Ins/Components/$NAME.component" au)" au.pkg
bundle_pkg "$NAME.app"       "/Applications"                      "$ID.app" \
           "$(mkscripts app "/Applications/$NAME.app")" app.pkg

# --- suporte: desinstalador + LEIA-ME (sempre instalado, invisível) ----------
mkdir -p "$WORK/root-suporte"
cp "$HERE/Desinstalar.command" "$WORK/root-suporte/Desinstalar BandChatLIVE.command"
chmod +x "$WORK/root-suporte/Desinstalar BandChatLIVE.command"
cp "$HERE/../../README.md" "$WORK/root-suporte/LEIA-ME.md"
pkgbuild --root "$WORK/root-suporte" --identifier "$ID.suporte" --version "$VERSION" \
         --install-location "$SUPPORT" "$WORK/pkgs/suporte.pkg" >/dev/null
echo "  componente: suporte.pkg (desinstalador -> $SUPPORT)"

# --- modelos de voz (opcionais, para todos os usuários) -----------------------
MODEL_CHOICES=""
MODEL_LINES=""
MODEL_REFS=""
add_model () {   # <arquivo> <id> <titulo> <descricao> <marcado:true|false>
  local file="$1" id="$2"
  if [ -z "$MODELS" ] || [ ! -f "$MODELS/$file" ]; then echo "  (modelo $file não encontrado: opção omitida)"; return 0; fi
  # cabeçalho ggml ("lmgg"): evita empacotar página de erro no lugar do modelo
  [ "$(head -c 4 "$MODELS/$file")" = "lmgg" ] || { echo "ERRO: $file não é um modelo ggml válido"; exit 1; }
  mkdir -p "$WORK/root-$id"
  cp "$MODELS/$file" "$WORK/root-$id/"
  pkgbuild --root "$WORK/root-$id" --identifier "$ID.modelo.$id" --version "$VERSION" \
           --install-location "$SUPPORT/modelos" "$WORK/pkgs/modelo-$id.pkg" >/dev/null
  local mb
  mb=$(( $(stat -f%z "$MODELS/$file") / 1048576 ))
  MODEL_LINES+="      <line choice=\"m_$id\"/>"$'\n'
  MODEL_CHOICES+="$(printf '  <choice id="m_%s" title="%s (%s MB)" description="%s" start_selected="%s">\n    <pkg-ref id="%s.modelo.%s"/>\n  </choice>' \
                    "$id" "$3" "$mb" "$4" "$5" "$ID" "$id")"$'\n'
  MODEL_REFS+="  <pkg-ref id=\"$ID.modelo.$id\" version=\"$VERSION\" onConclusion=\"none\">modelo-$id.pkg</pkg-ref>"$'\n'
  echo "  componente: modelo-$id.pkg ($file, $mb MB)"
}
# Rápido: q8_0 no Intel (mais rápido em CPU, ver benchmark), q5_1 no Apple Silicon
if [ -n "$MODELS" ] && [ -f "$MODELS/ggml-base-q8_0.bin" ]; then RAPIDO=ggml-base-q8_0.bin; else RAPIDO=ggml-base-q5_1.bin; fi
add_model "$RAPIDO"           rapido      "Modelo Rápido"      "Responde rápido, também em Mac mais antigo." true
add_model ggml-small-q5_1.bin equilibrado "Modelo Equilibrado" "Reconhece melhor; precisa de um Mac mais forte." true

MODELS_OUTLINE=""
if [ -n "$MODEL_LINES" ]; then
  MODELS_OUTLINE="    <line choice=\"modelos\">
$MODEL_LINES    </line>"
  MODEL_CHOICES="  <choice id=\"modelos\" title=\"Modelos de voz\" description=\"Já instalados, para funcionar offline desde o primeiro uso. O modelo 'Mais preciso' (550 MB) pode ser baixado dentro do plugin.\"/>
$MODEL_CHOICES"
fi

# --- Distribution: o que a tela do instalador mostra ---------------------------
cat > "$WORK/Distribution.xml" <<EOF
<?xml version="1.0" encoding="utf-8"?>
<installer-gui-script minSpecVersion="2">
  <title>BandChatLIVE $VERSION para Mac $MAC</title>
  <organization>com.davisales</organization>
  <welcome file="welcome.html" mime-type="text/html"/>
  <conclusion file="conclusion.html" mime-type="text/html"/>
  <options customize="allow" require-scripts="false" hostArchitectures="arm64,x86_64"/>
  <installation-check script="checarMac()"/>
  <script><![CDATA[
  function checarMac() {
    var appleSilicon = (system.sysctl('hw.optional.arm64') == 1);
    if (appleSilicon != $QUER_ARM) {
      my.result.type = 'Fatal';
      my.result.title = 'Instalador errado para este Mac';
      my.result.message = 'Este instalador é para Mac $MAC, e este Mac é $OUTRO. Baixe o instalador "$OUTRO" na mesma página.';
      return false;
    }
    return true;
  }
  ]]></script>
  <domains enable_localSystem="true" enable_anywhere="false" enable_currentUserHome="false"/>
  <volume-check>
    <allowed-os-versions><os-version min="11.0"/></allowed-os-versions>
  </volume-check>
  <choices-outline>
    <line choice="vst3"/>
    <line choice="au"/>
    <line choice="app"/>
$MODELS_OUTLINE
    <line choice="suporte"/>
  </choices-outline>
  <choice id="vst3" title="Plugin VST3" description="Para REAPER, Ableton Live, Cubase, Studio One, SuperRack Performer, Live Professor e outros.">
    <pkg-ref id="$ID.vst3"/>
  </choice>
  <choice id="au" title="Plugin AU (Audio Unit)" description="Para Logic Pro, MainStage e GarageBand.">
    <pkg-ref id="$ID.au"/>
  </choice>
  <choice id="app" title="App BandChatLIVE" description="Usa o microfone direto, sem precisar de DAW.">
    <pkg-ref id="$ID.app"/>
  </choice>
$MODEL_CHOICES  <choice id="suporte" title="Desinstalador" visible="false">
    <pkg-ref id="$ID.suporte"/>
  </choice>
  <pkg-ref id="$ID.vst3" version="$VERSION" onConclusion="none">vst3.pkg</pkg-ref>
  <pkg-ref id="$ID.au" version="$VERSION" onConclusion="none">au.pkg</pkg-ref>
  <pkg-ref id="$ID.app" version="$VERSION" onConclusion="none">app.pkg</pkg-ref>
  <pkg-ref id="$ID.suporte" version="$VERSION" onConclusion="none">suporte.pkg</pkg-ref>
$MODEL_REFS</installer-gui-script>
EOF
xmllint --noout "$WORK/Distribution.xml"

# --- telas (boas-vindas / conclusão) com a versão ------------------------------
mkdir -p "$WORK/resources"
for f in welcome.html conclusion.html; do
  sed -e "s/@VERSAO@/$VERSION/g" -e "s/@MAC@/$MAC/g" "$HERE/resources/$f" > "$WORK/resources/$f"
done

SIGN=()
if [ -n "${MAC_INSTALLER_IDENTITY:-}" ]; then
  SIGN=(--sign "$MAC_INSTALLER_IDENTITY")
  echo "  assinando com: $MAC_INSTALLER_IDENTITY"
fi
productbuild --distribution "$WORK/Distribution.xml" --resources "$WORK/resources" \
             --package-path "$WORK/pkgs" ${SIGN[@]+"${SIGN[@]}"} "$OUT"
echo "Pronto: $OUT ($(( $(stat -f%z "$OUT") / 1048576 )) MB)"
