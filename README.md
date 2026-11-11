# Projeto PIF – Tower Defense

Jogo de **tower defense 2D** feito em **C** com a biblioteca **[raylib](https://www.raylib.com/)**.

![Jogo em andamento](imagens/jogo.png)

O jogador constrói torres na grama, ao lado de uma estrada, para impedir que ondas de inimigos cheguem até a base. Os inimigos seguem a estrada, e a rota é calculada com **BFS (busca em largura)**. **Não é possível construir torres na estrada.**

---

## Objetivo

Sobreviver às **20 ondas** de inimigos sem perder todas as **20 vidas**.

- Cada inimigo que chega à base tira 1 vida (o tanque tira 5).
- Cada inimigo derrotado dá dinheiro.
- Ao fim de cada onda, o jogador ganha um bônus em dinheiro.
- **Só dá para plantar quando a onda acaba.** Ao começar uma onda, o modo de plantar é cancelado, os botões das plantas ficam apagados com "Aguarde", as teclas `1` a `6` não funcionam e aparece o aviso "Plante quando a onda acabar". Melhorar e vender continuam liberados.
- **Na onda 10 abre uma segunda entrada** (canto inferior esquerdo). Até lá ela aparece fechada, coberta de mato e com uma placa "Onda 10", e já não aceita plantas. A partir da onda 10, os inimigos saem alternadamente pelas duas entradas. O jogo avisa ao terminar a onda 9.

---

## Interface

Tudo é desenhado com as formas do raylib (círculos, retângulos arredondados, linhas), sem nenhuma imagem externa:

- **Mapa de jardim:** grama com tufos e florzinhas, estrada de terra com bordas e pedrinhas, pedras com sombra, uma **casinha** como base (pisca em vermelho quando perde vida) e **tocas** de onde saem as pragas. Pontinhos animados mostram o caminho calculado pela BFS.
- **Pragas animadas:** besouro (normal), formiga (rápida) e caracol gigante (chefe), com perninhas que se mexem e viram na direção em que andam. A barra de vida aparece quando a praga leva dano. Inimigos lentos ganham um anel azul, e envenenados soltam bolhas verdes.
- **Barra inferior:** painel com ícones de moeda, semente, coração e bandeira, mais uma barra de progresso da onda. Os botões das plantas mostram o desenho, o nome e o preço. Passar o mouse num botão mostra uma **dica** com os atributos.
- **Planta selecionada:** um cartão ao lado dela mostra o nível, o dano, o alcance, os tiros por segundo e o custo do próximo nível.
- **Efeitos:** "+$5" sobe quando uma praga morre, a explosão da abóbora tem um anel, e as mensagens aparecem num balão no topo da tela.
- **Telas** de menu (com tabela de controles), pausa, loja, tutorial, fim de jogo e vitória em painéis arredondados com botões clicáveis.

---

## Controles

| Tecla / Mouse        | Ação                                        |
|----------------------|---------------------------------------------|
| `1` a `6`            | Escolher o tipo de torre para construir     |
| `L`                  | Abrir / fechar a loja de torres             |
| Clique esquerdo      | Construir torre / selecionar torre          |
| Clique direito       | Cancelar seleção                            |
| `U`                  | Melhorar a planta selecionada (a partir da onda 5) |
| `S`                  | Vender a torre selecionada (70% do valor)   |
| `Espaço`             | Iniciar a próxima onda                      |
| `F`                  | Alternar velocidade (1x / 2x)               |
| `D`                  | Mostrar o mapa de distâncias da BFS         |
| `P` / `Esc`          | Pausar                                      |
| `Enter`              | Começar / reiniciar                         |

---

## Plantas (torres)

| Planta | Custo | Alcance | Tiros/s | Dano | Especial                        | Como obter     |
|--------|-------|---------|---------|------|---------------------------------|----------------|
| Brotinho | $70   | 120 px  | 1,6     | 10   | Barata e rápida                 | Já liberada    |
| Cacto  | $135  | 260 px  | 0,5     | 45   | Alcance longo, muito dano       | Já liberada    |
| Menta  | $70   | 100 px  | 1,0     | 4    | Reduz a velocidade em 50%       | Loja: 4 sementes  |
| Abóbora | $140  | 130 px  | 0,6     | 22   | Dano em área (raio de 55 px)    | Loja: 14 sementes |
| Urtiga | $70   | 110 px  | 1,0     | 2    | Veneno: 8 + 3% da vida máxima por segundo, durante 3 s | Loja: 6 sementes  |
| Girassol | $75   | 150 px  | 6,0     | 3    | Atira muito rápido              | Loja: 10 sementes |

### Melhorias

As melhorias ficam **bloqueadas até a onda 5**. Ao terminar a onda 4, o jogo pausa e mostra um **tutorial** explicando como melhorar as plantas. Depois disso o botão "Melhorar" (ou a tecla `U`) passa a funcionar.

- Cada planta pode ser melhorada até o **nível 3** (as bolinhas douradas embaixo dela mostram o nível).
- Cada nível aumenta o dano (e o veneno) em **40%**, o alcance em **25%** e os tiros por segundo em **20%** (constantes `UPG_DAMAGE`, `UPG_RANGE` e `UPG_FIRERATE`).
- Custo: nível 2 = preço da planta, nível 3 = 2× o preço.
- Vender devolve 70% de tudo o que foi gasto na planta, incluindo as melhorias.

A onda de liberação é configurável pela constante `UPGRADE_UNLOCK_WAVE` no código.

---

## Loja de plantas

O jogo tem duas moedas:

| Moeda        | Para que serve                         | Como ganhar                                              | Dura           |
|--------------|----------------------------------------|----------------------------------------------------------|----------------|
| **Dinheiro** | Construir e melhorar torres na partida | Derrotar inimigos e completar ondas                      | Só na partida  |
| **Sementes**    | Liberar novas torres na loja           | +1 por onda, +3 extras em onda de chefe, +2 por tanque   | Para sempre    |

- Abra a loja com **`L`** ou com o botão **"Loja de plantas"**. Também dá para abrir pelo menu inicial.
- O jogo fica **pausado** enquanto a loja está aberta.
- Torres bloqueadas aparecem com um **cadeado** na barra inferior. Clicar nelas abre a loja.
- Depois de liberada, a torre fica disponível em todas as partidas seguintes.

### Progresso salvo

As sementes e as torres liberadas são salvas no arquivo **`progresso.txt`**, criado na pasta onde o jogo roda:

```
sementes 7
torres 1 1 0 1 0 0
```

Cada número da linha `torres` indica se a torre de mesma posição (1 a 6) está liberada (`1`) ou não (`0`). Para **zerar o progresso**, é só apagar esse arquivo.

## Inimigos

| Inimigo | Vida      | Velocidade | Recompensa | Quando aparece               |
|---------|-----------|------------|------------|------------------------------|
| Normal  | base      | média      | $5         | Todas as ondas               |
| Rápido  | 60% base  | alta       | $6         | A partir da onda 3           |
| Tanque  | 8× base   | baixa      | $40        | Chefe a cada 5 ondas         |

A vida base cresce **20% a cada onda** (constantes `ENEMY_BASE_HP` e `HP_GROWTH`).

### Balanceamento

Os preços, os bônus de melhoria e o crescimento da vida dos inimigos foram ajustados com um **simulador**. Ele usa o próprio código do jogo, sem janela, e um "bot" que joga as 20 ondas sozinho (plantando entre as ondas e melhorando a partir da onda 5). Resultado com os valores atuais:

| Estratégia do bot          | Resultado              |
|----------------------------|------------------------|
| Só Brotinho                | Vence com 11 vidas     |
| Brotinho + Cacto           | Vence com 14 vidas     |
| Brotinho + Abóbora         | Vence com 10 vidas     |
| Brotinho + Urtiga          | Vence com 18 vidas     |
| Brotinho + Girassol        | Vence com 16 vidas     |
| Todas as plantas misturadas| Vence com 14 vidas     |

Com o crescimento antigo de 22%, nenhuma estratégia vencia.

---

## Como compilar e executar

### Pré-requisito
Instalar o raylib para Windows (o instalador oficial em <https://www.raylib.com/> já inclui o compilador `w64devkit` e o Notepad++ configurado).

### Opção 1 – Notepad++ do raylib
Abra `Projeto-PIF-TowerDefense.c` no Notepad++ que vem com o raylib e pressione **F6**.

### Opção 2 – Linha de comando
```bash
C:\raylib\w64devkit\bin\gcc Projeto-PIF-TowerDefense.c -o Projeto-PIF-TowerDefense.exe -O2 -Wall -std=c99 -IC:\raylib\raylib\src -LC:\raylib\raylib\src -lraylib -lopengl32 -lgdi32 -lwinmm
```

Depois execute:
```bash
Projeto-PIF-TowerDefense.exe
```

---

## Estrutura do código

Todo o jogo está em `Projeto-PIF-TowerDefense.c`, organizado em seções:

| Seção            | Responsabilidade                                                   |
|------------------|--------------------------------------------------------------------|
| Configurações    | Tamanho da grade, limites, dinheiro e vidas iniciais, mapa em texto |
| Tipos            | `structs` e `enums` de torres, inimigos, projéteis, efeitos e estados |
| Progresso e loja | `SaveProgress`, `LoadProgress`, `AddSeeds`, `BuyTower`, `OpenShop`  |
| Mapa e BFS       | `LoadMap`, `ComputeDistances` (BFS) e `CanPlace`                   |
| Jogo             | Ondas, spawn, movimento, mira, projéteis, dano em área e veneno    |
| Desenho          | Mapa, torres, inimigos, interface, loja e telas de menu/pausa/fim  |
| `main`           | Game loop e máquina de estados                                     |

### Conceitos de programação aplicados

- **Game loop**: a cada frame o jogo lê a entrada, atualiza e desenha.
- **Máquina de estados**: menu → jogando ⇄ pausado / loja / tutorial → fim de jogo / vitória.
- **Persistência em arquivo**: `fopen` / `fprintf` / `fscanf` para salvar e carregar as sementes e as torres liberadas.
- **Dados das torres em tabela**: cada torre é uma linha de `TOWER_INFO`. Para criar uma torre nova, basta adicionar uma linha, sem mexer na lógica.
- **Matriz (grid)**: o mapa é uma matriz `ROWS × COLS` de tiles.
- **BFS (busca em largura)**: parte da base e, andando só pela estrada, calcula a distância de cada tile até ela. Os inimigos sempre andam para o vizinho com menor distância.
- **Validação de construção**: cada tile tem um tipo (grama, estrada, pedra ou torre). `CanPlace` só deixa construir na grama livre e mostra uma mensagem quando o jogador tenta construir na estrada.
- **Pools de entidades**: inimigos e projéteis ficam em vetores de tamanho fixo e os slots são reaproveitados.
- **Vetores 2D**: movimento, distância, mira (`atan2f`) e projéteis teleguiados usando `raymath`.

### Mapa

O mapa é definido como texto no início do código e pode ser editado facilmente:

```
.  grama (pode construir torres)
=  estrada (os inimigos andam por aqui; não pode construir)
#  pedra (não dá para construir nem passar)
S  entrada dos inimigos
T  segunda entrada (abre na onda SECOND_ENTRANCE_WAVE = 10)
+  estrada da segunda entrada (fechada até abrir, mas já não aceita plantas)
B  base
```

A estrada da segunda entrada se junta à estrada principal. Como a BFS parte da base, os inimigos das duas entradas encontram o caminho sozinhos, sem nenhum código extra de rota.

---

## Possíveis melhorias

- Carregar várias fases a partir de arquivos `.txt`
- Sprites e animações no lugar das formas geométricas
- Efeitos sonoros e música
- Salvar recorde em arquivo
- Comprar melhorias permanentes na loja (ex.: +10% de dano para todas as torres)
- Usar A* no lugar da BFS

---

## Tecnologias

- Linguagem **C (C99)**
- **raylib 5** (gráficos, entrada e janela)
- **raymath** (operações com vetores)
