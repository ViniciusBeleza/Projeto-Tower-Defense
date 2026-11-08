# Projeto PIF – Tower Defense

Jogo de **tower defense 2D** feito em **C** com a biblioteca **[raylib](https://www.raylib.com/)**.

O jogador constrói torres em uma grade para impedir que ondas de inimigos cheguem até a base. Não existe um caminho fixo: os inimigos calculam a rota até a base com **BFS (busca em largura)**, e o jogador pode montar um labirinto com as torres, desde que nunca bloqueie totalmente o caminho.

---

## Objetivo

Sobreviver às **20 ondas** de inimigos sem perder todas as **20 vidas**.

- Cada inimigo que chega à base tira 1 vida (o tanque tira 5).
- Cada inimigo derrotado dá dinheiro.
- Ao fim de cada onda, o jogador ganha um bônus em dinheiro.

---

## Controles

| Tecla / Mouse        | Ação                                        |
|----------------------|---------------------------------------------|
| `1` `2` `3`          | Escolher o tipo de torre para construir     |
| Clique esquerdo      | Construir torre / selecionar torre          |
| Clique direito       | Cancelar seleção                            |
| `U`                  | Melhorar a torre selecionada                |
| `S`                  | Vender a torre selecionada (70% do valor)   |
| `Espaço`             | Iniciar a próxima onda                      |
| `F`                  | Alternar velocidade (1x / 2x)               |
| `D`                  | Mostrar o mapa de distâncias da BFS         |
| `P` / `Esc`          | Pausar                                      |
| `Enter`              | Começar / reiniciar                         |

---

## Torres

| Torre  | Custo | Alcance | Tiros/s | Dano | Especial                     |
|--------|-------|---------|---------|------|------------------------------|
| Básica | $50   | 120 px  | 1,6     | 10   | Barata e rápida              |
| Sniper | $100  | 260 px  | 0,5     | 45   | Alcance longo, muito dano    |
| Gelo   | $75   | 100 px  | 1,0     | 4    | Reduz a velocidade em 50%    |

Cada torre pode ser melhorada até o **nível 3**. Cada nível aumenta o dano em 60% e o alcance e a cadência de tiro em 15%.

## Inimigos

| Inimigo | Vida      | Velocidade | Recompensa | Quando aparece               |
|---------|-----------|------------|------------|------------------------------|
| Normal  | base      | média      | $5         | Todas as ondas               |
| Rápido  | 60% base  | alta       | $6         | A partir da onda 3           |
| Tanque  | 8× base   | baixa      | $40        | Chefe a cada 5 ondas         |

A vida base cresce **22% a cada onda**.

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
| Tipos            | `structs` e `enums` de torres, inimigos, projéteis e estados       |
| Mapa e BFS       | `LoadMap`, `ComputeDistances` (BFS) e `CanPlace`                   |
| Jogo             | Ondas, spawn, movimento dos inimigos, mira das torres, projéteis   |
| Desenho          | Mapa, torres, inimigos, interface e telas de menu/pausa/fim        |
| `main`           | Game loop e máquina de estados                                     |

### Conceitos de programação aplicados

- **Game loop**: a cada frame o jogo lê a entrada, atualiza e desenha.
- **Máquina de estados**: menu → jogando ⇄ pausado → fim de jogo / vitória.
- **Matriz (grid)**: o mapa é uma matriz `ROWS × COLS` de tiles.
- **BFS (busca em largura)**: parte da base e calcula a distância de cada tile até ela. Os inimigos sempre andam para o vizinho com menor distância.
- **Validação de construção**: antes de construir, o jogo simula a torre no mapa e roda a BFS de novo. Se a entrada perder o acesso à base, a construção é recusada.
- **Pools de entidades**: inimigos e projéteis ficam em vetores de tamanho fixo e os slots são reaproveitados.
- **Vetores 2D**: movimento, distância, mira (`atan2f`) e projéteis teleguiados usando `raymath`.

### Mapa

O mapa é definido como texto no início do código e pode ser editado facilmente:

```
.  tile livre
#  pedra (não dá para construir nem passar)
S  entrada dos inimigos
B  base
```

---

## Possíveis melhorias

- Carregar várias fases a partir de arquivos `.txt`
- Sprites e animações no lugar das formas geométricas
- Efeitos sonoros e música
- Salvar recorde em arquivo
- Novos tipos de torre (dano em área, veneno)
- Usar A* no lugar da BFS

---

## Tecnologias

- Linguagem **C (C99)**
- **raylib 5** (gráficos, entrada e janela)
- **raymath** (operações com vetores)
