# ArmSwing

**Balanço natural dos braços no rastreamento de corpo inteiro do VRChat.**

[English](README.md) | [日本語](README.ja.md) | [한국어](README.ko.md) | [简体中文](README.zh-CN.md) | [Español](README.es.md) | Português (BR) | [Русский](README.ru.md)

No VRChat com rastreamento de corpo inteiro (FBT), andar com o analógico
deixa os braços duros enquanto o corpo desliza. O ArmSwing entrega seus braços
abaixados à própria animação de caminhada do VRChat enquanto você anda, então
eles balançam naturalmente. Levante uma mão, ou segure o grip ou o gatilho
dela, e esse braço volta a seguir o controle no mesmo frame.

## Requisitos

- Windows 10 (versão 1809 ou mais recente) ou Windows 11, com SteamVR
- VRChat (versão da Steam) com **o rastreamento de corpo inteiro calibrado**:
  trackers, SlimeVR, Standable ou qualquer outra coisa que o VRChat calibre
  como corpo inteiro
- Um controle com analógico (veja [Controles compatíveis](#controles-compatíveis))
- **OSC do VRChat ligado**: Menu de ações (Action Menu) > Options > OSC > Enabled

Sem rastreamento de corpo inteiro (3 pontos), o ArmSwing deixa seus braços
rastreados normalmente. O suporte a 3 pontos está em desenvolvimento.

## Instalação

1. Feche o SteamVR.
2. Extraia o zip inteiro em uma pasta permanente, por exemplo
   `C:\Tools\ArmSwing`. Você deve ver a pasta `armswing`, `install.bat`,
   `uninstall.bat` e os arquivos README.
3. Clique duas vezes em `install.bat`.
4. Inicie o SteamVR e depois execute `armswing\armswing.exe`.

Nenhuma janela abre: o ArmSwing roda na bandeja do sistema, ao lado do
relógio. Clique com o botão direito no ícone para sair. Ele fecha sozinho
quando o SteamVR fecha.

**Atualizar:** feche o SteamVR, extraia o novo zip e execute o `install.bat`
dele. Ele remove o registro das cópias antigas para você. Depois apague a
pasta antiga.

**Se o Windows mostrar um aviso:** o ArmSwing ainda não tem assinatura de
código, então o Windows SmartScreen pode mostrar "O Windows protegeu o
computador" ao abrir `install.bat` ou `armswing.exe`. Clique em **Mais
informações** e depois em **Executar assim mesmo**. Alguns antivírus também
podem marcar o driver, porque ele se conecta ao SteamVR do jeito que os
complementos de driver fazem. Todo o código-fonte é público, e cada versão
traz somas de verificação SHA-256 para você confirmar que o download é o
original.

## Como usar

- Ande com o analógico esquerdo com os braços abaixados: eles balançam.
- Levante um braço acima da cintura, mais ou menos, ou segure o grip ou o
  gatilho dele: esse braço volta a seguir o controle na hora, mesmo no meio
  da caminhada.
- Quando o analógico está pressionado mas o VRChat não está de fato te
  movendo (andando contra uma parede, modo de câmera voando, sentado,
  agachado bem baixo), seus braços continuam rastreados.
- O ArmSwing age em todos os jogos do SteamVR enquanto estiver aberto. Feche-o
  pelo ícone da bandeja antes de jogar outra coisa.

## Controles compatíveis

Prontos para usar, sem configuração: PlayStation VR2 Sense, Meta Quest e Rift
Touch (Quest Link, Air Link, Steam Link, Virtual Desktop, ALVR), Valve Index,
Steam Frame, Vive Cosmos, HP Reverb G2, Windows Mixed Reality. Os controles
Pico e Vive Focus 3 / XR Elite (pelo Steam Link) usam automaticamente a
predefinição do Quest.

Outros controles com analógico: em SteamVR > Configurações > Controles >
Gerenciar associações de controle > ArmSwing, associe seu analógico de
movimento a `left_stick_pos`. Os controles Vive (wands) não são compatíveis:
eles têm trackpad, não analógico.

## Confira o seu setup

Encontre o seu caso na tabela. As configurações citadas estão explicadas em
[Configurações](#configurações).

| Seu caso | O que fazer |
|---|---|
| Um controle da lista acima | Nada. |
| Outro controle com analógico | Associe `left_stick_pos` (veja acima). |
| Controles que você pode soltar (com alça, ex.: Globular Cluster, alças do Index frouxas) | Opcional: `wake_mode=touch`, para o braço continuar seu enquanto sua mão toca o controle. |
| Seus braços não balançam quando abaixados | Diminua `raise_drop` (ex.: `0.50`). |
| Seus braços balançam quando você quer mantê-los | Aumente `raise_drop` (ex.: `0.90`). |
| Sem rastreamento de corpo inteiro (3 pontos) | Ainda não é compatível: seus braços continuam rastreados normalmente. |
| Você anda com o analógico direito | Ainda não é compatível. |
| VRChat fora da Steam, ou OSC do VRChat desligado | Os braços balançam sempre que o analógico é pressionado, mesmo contra paredes e em menus. Use a versão da Steam com o OSC ligado para o comportamento completo. |
| Pasta de instalação com caracteres incomuns (emojis, ou um alfabeto diferente do idioma do seu Windows) | Instale em um caminho simples como `C:\Tools\ArmSwing`. |

## Configurações

Edite `armswing\config.txt` em qualquer editor de texto. Você pode salvar
estando em VR: as mudanças valem em cerca de um segundo.

| Configuração | Padrão | O que faz |
|---|---|---|
| `raise_drop` | `0.70` | Quantos metros abaixo do headset um braço conta como levantado. Menor: você precisa levantar mais o braço para mantê-lo. |
| `wake_mode` | `grip` | `grip`: segurar o grip ou o gatilho mantém o braço em qualquer altura. `touch`: os sensores de toque também contam; use só com controles que você pode soltar (com alça). |
| `wake_hold` | `120` | Por quanto tempo, em ms, o braço continua seu depois que você solta o grip ou o gatilho. |
| `walk_hold` | `90` | Por quanto tempo, em ms, o analógico precisa ficar pressionado antes de os braços balançarem. Ignora toques rápidos no analógico. |

Para desligar o ArmSwing sem desinstalar, escreva `off` em
`armswing\mode.txt` (escreva `9` para ligar de novo).

## Solução de problemas

Passe o mouse sobre o ícone da bandeja para ver o status. Os logs ficam na
pasta `armswing`: `armswing-watcher.log` (o app da bandeja) e `armswing.log`
(o driver do SteamVR). Os logs da sessão anterior ficam guardados como
`*.prev`.

- **Os braços balançam contra paredes, em menus ou no modo câmera:** o OSC do
  VRChat está desligado. Ligue-o (Action Menu > Options > OSC > Enabled).
  Quando funciona, o `armswing-watcher.log` mostra `VRChat's OSC arriving`.
- **Os braços nunca balançam:** procure `[stick] action INACTIVE` no
  `armswing-watcher.log`. Significa que nenhuma associação combinou com seu
  controle; veja [Controles compatíveis](#controles-compatíveis). Confira
  também se o rastreamento de corpo inteiro está calibrado no VRChat.
- **Um braço nunca balança:** você pode estar segurando o grip ou o gatilho
  dele, ou usando `wake_mode=touch` com um controle que fica na sua mão.
- **Qualquer outro problema:** abra uma issue e anexe o `armswing-watcher.log`
  e o `armswing.log`, junto com as cópias `.prev`.

## Compatibilidade e privacidade

- **Outros apps de OSC** (VRCFaceTracking e outros) continuam funcionando: o
  ArmSwing nunca usa as portas 9000 nem 9001. O VRChat o encontra via
  OSCQuery e envia os dados para a porta própria do ArmSwing.
- **Outros complementos do SteamVR** continuam funcionando: a única coisa que
  o ArmSwing muda é se suas duas mãos aparecem como rastreadas enquanto você
  anda. Ele não adiciona dispositivos.
- **Rede:** o ArmSwing só escuta em 127.0.0.1, então nada fora do seu PC
  consegue se conectar a ele. Para o VRChat encontrá-lo, ele se anuncia via
  mDNS na sua rede local, do mesmo jeito que o VRChat e outros apps de OSC. Ele
  nunca se conecta à internet e não coleta nenhum dado.

## Desinstalação

Feche o SteamVR, execute `uninstall.bat` e apague a pasta.

## Compilar a partir do código-fonte

Veja o [README em inglês](README.md#building-from-source).
