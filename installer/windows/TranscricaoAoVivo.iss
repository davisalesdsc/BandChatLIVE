; Instalador do Windows (Inno Setup 6). Compilado pelo CI:
;   ISCC /DAppVersion=0.2 /DSrcDir=<pasta com .vst3 e .exe> /DModelsDir=<pasta dos modelos> TranscricaoAoVivo.iss
; Cada modelo que existir em ModelsDir vira uma opção na instalação.

#ifndef AppVersion
  #define AppVersion "0.0"
#endif
#ifndef SrcDir
  #define SrcDir "..\..\..\out"
#endif
#ifndef ModelsDir
  #define ModelsDir "..\..\..\modelos"
#endif
#ifndef OutDir
  #define OutDir "."
#endif

#define Plugin "Transcricao ao Vivo"
#define HasRapido FileExists(ModelsDir + "\ggml-base-q5_1.bin")
#define HasEquilibrado FileExists(ModelsDir + "\ggml-small-q5_1.bin")

[Setup]
AppId={{B1A4C319-AFAD-4BF0-8DA8-CBEBE4EFAC5C}
AppName=Transcrição ao Vivo
AppVersion={#AppVersion}
AppVerName=Transcrição ao Vivo {#AppVersion}
AppPublisher=Davi Sales
DefaultDirName={autopf}\{#Plugin}
DefaultGroupName=Transcrição ao Vivo
DisableProgramGroupPage=yes
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
PrivilegesRequired=admin
MinVersion=10.0
OutputDir={#OutDir}
OutputBaseFilename=TranscricaoAoVivo-{#AppVersion}-Windows-Setup
Compression=lzma2/max
SolidCompression=yes
WizardStyle=modern
UninstallDisplayName=Transcrição ao Vivo
UninstallDisplayIcon={app}\{#Plugin}.exe
CloseApplications=yes

[Languages]
Name: "pt"; MessagesFile: "compiler:Languages\BrazilianPortuguese.isl"
Name: "en"; MessagesFile: "compiler:Default.isl"

[Types]
Name: "completa"; Description: "Completa (recomendado)"
Name: "personalizada"; Description: "Personalizada"; Flags: iscustom

[Components]
Name: "vst3"; Description: "Plugin VST3 (REAPER, Ableton, Cubase, Studio One, SuperRack, Live Professor...)"; Types: completa personalizada
Name: "app"; Description: "App Transcrição ao Vivo (usa o microfone direto, sem DAW)"; Types: completa personalizada
#if HasRapido || HasEquilibrado
Name: "modelos"; Description: "Modelos de voz (funcionam offline desde o primeiro uso)"; Types: completa personalizada
#endif
#if HasRapido
Name: "modelos\rapido"; Description: "Rápido: responde rápido, para computador mais fraco"; Types: completa personalizada
#endif
#if HasEquilibrado
Name: "modelos\equilibrado"; Description: "Equilibrado: reconhece melhor, serve na maioria dos PCs"; Types: completa personalizada
#endif

[Tasks]
Name: "atalho"; Description: "Criar atalho do app na Área de Trabalho"; Components: app; Flags: unchecked

[InstallDelete]
; apaga a versão anterior do plugin antes de copiar (arquivo que sumiu numa versão nova não fica)
Type: filesandordirs; Name: "{commoncf64}\VST3\{#Plugin}.vst3"; Components: vst3

[Files]
Source: "{#SrcDir}\{#Plugin}.vst3\*"; DestDir: "{commoncf64}\VST3\{#Plugin}.vst3"; Components: vst3; Flags: ignoreversion recursesubdirs createallsubdirs
Source: "{#SrcDir}\{#Plugin}.exe"; DestDir: "{app}"; Components: app; Flags: ignoreversion
Source: "{#SrcDir}\LEIA-ME.md"; DestDir: "{app}"; Flags: ignoreversion skipifsourcedoesntexist
#if HasRapido
Source: "{#ModelsDir}\ggml-base-q5_1.bin"; DestDir: "{commonappdata}\TranscricaoAoVivo\modelos"; Components: modelos\rapido; Flags: ignoreversion
#endif
#if HasEquilibrado
Source: "{#ModelsDir}\ggml-small-q5_1.bin"; DestDir: "{commonappdata}\TranscricaoAoVivo\modelos"; Components: modelos\equilibrado; Flags: ignoreversion
#endif

[Icons]
Name: "{autoprograms}\Transcrição ao Vivo"; Filename: "{app}\{#Plugin}.exe"; Components: app
Name: "{autodesktop}\Transcrição ao Vivo"; Filename: "{app}\{#Plugin}.exe"; Components: app; Tasks: atalho

[UninstallDelete]
Type: filesandordirs; Name: "{commoncf64}\VST3\{#Plugin}.vst3"

[Messages]
pt.FinishedLabel=Instalado! Abra o seu programa (REAPER, SuperRack, Ableton...) e peça para procurar plugins de novo. Depois coloque "Transcrição ao Vivo" no canal do microfone.%n%nQuer mais precisão? No plugin, abra AVANÇADO e baixe o modelo "Mais preciso".

[Code]
// O motor usa instruções AVX2 (todo PC de 2013 em diante). Sem elas o plugin
// não transcreve: melhor avisar aqui do que deixar a pessoa descobrir no show.
function IsProcessorFeaturePresent(Feature: Cardinal): Cardinal;
  external 'IsProcessorFeaturePresent@kernel32.dll stdcall';

function InitializeSetup(): Boolean;
begin
  Result := True;
  if IsProcessorFeaturePresent(40) = 0 then  { PF_AVX2_INSTRUCTIONS_AVAILABLE }
    Result := SuppressibleMsgBox(
      'Não foi possível confirmar que este processador tem AVX2 (PCs de 2013 em diante).' + #13#10 +
      'Sem AVX2 o plugin abre, mas não consegue transcrever.' + #13#10#13#10 +
      'Instalar mesmo assim?', mbConfirmation, MB_YESNO, IDYES) = IDYES;
end;
