# Transcrição ao Vivo: plugin VST3 / AU / app

Você fala no microfone e o texto aparece na tela, com horário, em tempo real e **100% offline**.
O plugin funciona em qualquer programa que aceite VST3 ou AU (REAPER, Ableton Live, Logic,
MainStage, SuperRack Performer V14+, Live Professor, Cubase, Studio One...). Também existe um
**app separado** que escuta o microfone direto, sem precisar de DAW.

O áudio passa **intacto** pelo plugin: ele só escuta.

## Instalar

**macOS** (Apple Silicon ou Intel, macOS 11+): dê dois cliques em `TranscricaoAoVivo-x.y-macOS.pkg`.
**Windows** (10/11, 64 bits): dê dois cliques em `TranscricaoAoVivo-x.y-Windows-Setup.exe`.

O instalador coloca tudo no lugar certo. Em *Personalizar* dá para escolher:

| Componente | macOS | Windows |
|---|---|---|
| Plugin VST3 (REAPER, Ableton, Cubase, Studio One, SuperRack, Live Professor...) | sim | sim |
| Plugin AU (Logic, MainStage, GarageBand) | sim | não existe AU no Windows |
| App sem DAW (usa o microfone direto) | sim | sim |
| Modelos de voz Rápido e Equilibrado (funciona offline desde o primeiro uso) | sim | sim |

Depois peça ao seu programa para **procurar plugins de novo** (REAPER: Options > Preferences >
Plug-ins > VST > Re-scan; Logic: basta reabrir).

> **Formato VST (VST2):** não é oferecido. A Steinberg encerrou a licença do VST2 em 2018 e não
> permite mais que plugins novos usem esse formato. Todos os programas atuais aceitam VST3.

> **Enquanto o instalador não for assinado pela Apple/Microsoft:**
> - macOS: se aparecer "não pode ser aberto porque é de um desenvolvedor não identificado",
>   abra Ajustes do Sistema > Privacidade e Segurança e clique em **Abrir Mesmo Assim**.
> - Windows: se aparecer "O Windows protegeu o computador", clique em **Mais informações** >
>   **Executar assim mesmo**.

**Desinstalar:** no Mac, dê dois cliques em *Desinstalar Transcricao ao Vivo* na pasta
`/Library/Application Support/TranscricaoAoVivo`. No Windows, use Configurações > Aplicativos.

## O que tem na tela

- **Status**: CAPTANDO / FALA DETECTADA / PAUSADO, e o modelo em uso (`+ VAD` = detector de voz
  neural ligado, `+ GPU` = usando a placa de vídeo do Mac).
- **Medidor** com a linha branca do **Gate**.
- **INICIAR / PAUSAR**: é o parâmetro *Transcrever*. Dá para **automatizar ou mapear em MIDI**
  no DAW, por exemplo para desligar durante as músicas e ligar entre elas.
- **Texto parcial**: enquanto a pessoa fala, o texto aparece apagado com "…" e vira a frase
  final no mesmo lugar.
- **JANELA ⇱**: abre a transcrição numa janela separada, que pode ficar **sempre na frente** e
  continua aberta mesmo fechando a janela do plugin.
- **COPIAR / LIMPAR / ABRIR LOGS**: o histórico de cada dia fica num `.txt`.

## AVANÇADO

| Ajuste | Para quê |
|---|---|
| Destacar palavras | Frases com essas palavras aparecem em vermelho (ex.: fone, retorno). |
| Modo | **Automático** começa no melhor modelo que a máquina aguenta e alivia sozinho se atrasar. **Leve** responde mais rápido. **Preciso** nunca rebaixa. |
| Idioma | Português, English, Español ou detectar. |
| Gate | Nível mínimo de fala. Deixe um pouco acima do vazamento do palco. |
| Sensibilidade | Detector de voz: mais alto pega fala mais baixa, mas deixa passar mais coisa. |
| Fim de frase | Silêncio que fecha a frase (700 ms). Para quem fala pausado, aumente. |
| Parciais | Texto enquanto a pessoa fala. Desligue em computador fraco. |
| Vocabulário | Nomes e termos que ajudam o reconhecimento. Mantenha curto. |
| Correções | Conserta palavras no texto final: `david => Davi ; in ear => in-ear` |
| Nome do canal | Aparece antes de cada frase no histórico (útil com vários canais). |

Se o computador não acompanhar a fala, o plugin primeiro desliga os parciais, depois troca
para um modelo mais leve (no modo Automático) e, por último, entra no modo econômico. O aviso
aparece na tela.

## Limitação

O detector de voz rejeita instrumento, batida, palmas e ruído, mas **canto é voz**: com a
música rolando no mesmo microfone, ele tenta transcrever. Use o parâmetro *Transcrever* (por
automação/MIDI/snapshot) para pausar durante as músicas.

## Onde ficam os arquivos

| | macOS | Windows |
|---|---|---|
| Modelos do instalador | `/Library/Application Support/TranscricaoAoVivo/modelos/` | `C:\ProgramData\TranscricaoAoVivo\modelos\` |
| Modelos baixados no plugin | `~/Library/Application Support/TranscricaoAoVivo/modelos/` | `%APPDATA%\TranscricaoAoVivo\modelos\` |
| Histórico | `.../TranscricaoAoVivo/Logs/transcricao_AAAA-MM-DD.txt` | idem |
| Log técnico | `.../TranscricaoAoVivo/transcritor.log` | idem |

Também dá para colocar à mão qualquer modelo `ggml-*.bin` do whisper.cpp na pasta de modelos.

---

## Para desenvolvedores

C++ com [JUCE 8](https://juce.com) (interface e formatos de plugin) e
[whisper.cpp](https://github.com/ggml-org/whisper.cpp) (Whisper e detector de voz Silero),
baixados automaticamente pelo CMake.

```
Source/
  PluginProcessor.*        áudio do host -> mono -> anti-alias -> 16 kHz -> fila sem trava
  PluginEditor.*           janela do plugin
  UI/                      transcrição, janela separada, tema
  Engine/Transcriber.*     threads "ouvido" (VAD + segmentação) e "escriba" (Whisper)
  Engine/Segmenter.*       separa as frases (nível + VAD, pré-roll, parciais)
  Engine/WhisperBackend.*  modelo compartilhado entre instâncias, VAD embutido
  Engine/TextFilter.*      alucinações, repetição, correções, palavras-chave
  Engine/Models.*          pastas, escolha e download de modelos
  Common/TranscriptLog.*   histórico diário e log técnico
installer/
  macos/build-pkg.sh       monta o .pkg (VST3 + AU + app + modelos + desinstalador)
  windows/*.iss            Setup.exe (Inno Setup)
  baixar-modelos.sh        modelos que vão dentro dos instaladores
tests/
  TavTests.cpp             motor: texto, VAD determinístico, segmentação (Whisper opcional)
  HostTest.cpp             carrega o VST3 como um DAW: taxas/blocos, estado, 4 instâncias, fluxo
  checar-acentos.py        textos com acento sempre convertidos de UTF-8
  audio/teste.wav          falas + trecho de instrumento + fala longa
```

Compilar e testar (macOS: Xcode; Windows: Visual Studio 2022; CMake 3.22+):

```bash
cmake -S . -B build -DTAV_BUILD_TESTS=ON
cmake --build build --config Release
./build/TavTests_artefacts/Release/TavTests tests/audio/teste.wav
```

`TAV_DEMO=1` no ambiente troca o Whisper por um texto fixo ("[demo] fala de X s"), para testar
sem modelo.

### Gerar os instaladores

O GitHub Actions (`.github/workflows/build.yml`) faz tudo a cada push na `main`:
compila macOS (arm64 e x86_64 juntados num binário universal), Windows e Linux; monta o `.pkg` e o
`Setup.exe`; **instala de verdade** nos dois sistemas e confere (no Mac roda o `auval` da Apple no AU
instalado). Os instaladores ficam na aba **Actions**, em *Artifacts*.

Para publicar uma versão com página de download: atualize `VERSAO` e crie a tag
(`git tag v0.2 && git push --tags`). Os dois instaladores aparecem em **Releases**.

**Assinatura Apple (opcional):** com conta Apple Developer (US$ 99/ano), cadastre em
*Settings > Secrets and variables > Actions*: `MACOS_CERT_P12_BASE64` (certificados "Developer ID
Application" e "Developer ID Installer" num .p12, em base64), `MACOS_CERT_PASSWORD`, `APPLE_ID`,
`APPLE_TEAM_ID` e `APPLE_APP_PASSWORD`. Com isso o `.pkg` sai assinado e notarizado, sem aviso ao abrir.
