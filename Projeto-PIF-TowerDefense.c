/*
 * Tower Defense - raylib (C)
 *
 * Conceitos usados:
 *  - Game loop com maquina de estados (menu, jogando, pausado, fim)
 *  - Grid / tilemap carregado de um mapa em texto
 *  - Estrada fixa no mapa: os inimigos andam por ela e torres so na grama
 *  - BFS (busca em largura) pela estrada gerando um "mapa de distancias" ate a base
 *    -> os inimigos sempre andam para o vizinho mais perto da base
 *  - Pools fixos de entidades (inimigos e projeteis)
 *  - Projeteis teleguiados, torres com upgrade, ondas progressivas
 *  - Loja: sementes (moeda permanente) liberam novas torres
 *  - Persistencia: sementes e torres liberadas sao salvas em "progresso.txt"
 *
 * Controles:
 *  1 a 6      escolher torre para construir
 *  L          abrir / fechar a loja
 *  Clique esq construir / selecionar torre
 *  Clique dir cancelar selecao
 *  U / S      melhorar / vender torre selecionada
 *  ESPACO     iniciar proxima onda
 *  F          alternar velocidade (1x / 2x)
 *  D          mostrar mapa de distancias da BFS
 *  P / ESC    pausar
 */

#include "raylib.h"
#include "raymath.h"
#include <stdio.h>
#include <string.h>
#include <stdbool.h>
#include <math.h>
#include <stdlib.h>

/* ------------------------------------------------------------------ */
/* Configuracoes                                                      */
/* ------------------------------------------------------------------ */
#define COLS        20
#define ROWS        12
#define TILE        48
#define UI_H        124
#define GRID_H      (ROWS * TILE)
#define SCREEN_W    (COLS * TILE)
#define SCREEN_H    (GRID_H + UI_H)

#define MAX_ENEMIES 256
#define MAX_PROJ    512
#define MAX_FX      64
#define MAX_FLOAT   48
#define MAX_PART    400
#define BANNER_TIME 2.2f
#define MAX_LEVEL   3
#define MAX_WAVES   20
#define UPGRADE_UNLOCK_WAVE 5   /* melhorias so ficam disponiveis a partir desta onda */

/* Bonus de cada nivel de melhoria (0.40 = +40% por nivel).
   Os #ifndef permitem testar outros valores no simulador de balanceamento. */
#ifndef UPG_DAMAGE
#define UPG_DAMAGE   0.40f      /* dano e veneno */
#endif
#ifndef UPG_RANGE
#define UPG_RANGE    0.25f      /* alcance */
#endif
#ifndef UPG_FIRERATE
#define UPG_FIRERATE 0.20f      /* tiros por segundo */
#endif
#define INF         9999

/* O dinheiro inicial agora e definido em cada fase (LEVELS[].startMoney) */
#define START_LIVES 20

/* Dificuldade: vida base dos inimigos e quanto ela cresce a cada onda (1.22 = +22%) */
#define ENEMY_BASE_HP 18.0f
#ifndef HP_GROWTH
#define HP_GROWTH     1.205f
#endif

#define SAVE_FILE   "progresso.txt"

/* Sementes ganhas (moeda da loja) */
#define SEEDS_PER_WAVE   1
#define SEEDS_BOSS_WAVE  3   /* bonus extra em ondas de chefe (5, 10, 15, 20) */
#define SEEDS_PER_TANK   2

/* ------------------------------------------------------------------ */
/* Fases                                                              */
/* ------------------------------------------------------------------ */
/* Mapa: '.' grama (pode construir)  '=' estrada  '#' pedra
         'S' entrada dos inimigos  'B' base
         'T' segunda entrada  '+' estrada da segunda entrada
         (fechada ate a onda secondWave da fase, mas ja nao aceita plantas)
   Cada fase e uma linha desta tabela: para criar uma fase nova basta
   desenhar outro mapa e escolher os numeros de dificuldade. */
typedef struct {
    const char *name;
    const char *desc;
    const char *map[ROWS];
    int   secondWave;     /* onda em que a segunda entrada abre */
    float hpMult;         /* multiplicador da vida das pragas */
    int   startMoney;     /* dinheiro no inicio da fase */
    int   pathLen;        /* passos da entrada ate a casa (so para mostrar) */
    Color grassA, grassB; /* cores da grama (xadrez) */
} LevelInfo;

#define LEVEL_COUNT 3

#ifndef LEVEL1_HP
#define LEVEL1_HP 1.00f
#endif
#ifndef LEVEL2_HP
#define LEVEL2_HP 1.00f
#endif
#ifndef LEVEL3_HP
#define LEVEL3_HP 0.85f   /* estrada curtissima: pragas um pouco mais fracas para compensar */
#endif

static const LevelInfo LEVELS[LEVEL_COUNT] = {
    { .name = "Jardim", .desc = "Estrada longa em zigue-zague",
      .map = {
        "..........#.........",
        "....................",
        "S====....======.....",
        "....=....=....=.....",
        "....=....=....=..#..",
        "....=.#..=....=.....",
        "....=....=....=.....",
        ".#..=....=....=.....",
        "....======....=.....",
        "....+.........=====B",
        "T++++..#............",
        "...........#........",
      },
      .secondWave = 10, .hpMult = LEVEL1_HP, .startMoney = 150, .pathLen = 38,
      .grassA = { 98, 162, 82, 255 }, .grassB = { 90, 152, 76, 255 } },

    { .name = "Horta", .desc = "Caminho mais curto, 2ª entrada mais cedo",
      .map = {
        "...S................",
        "...=...#............",
        "...=......=======...",
        ".#.=......=.....=...",
        "...=......=..#..=...",
        "...========.....=...",
        "..........+.....=.#.",
        "......#...+.....=...",
        "..........+.#...=...",
        "T++++++++++.....=...",
        "................=..#",
        ".....#..........B...",
      },
      .secondWave = 7, .hpMult = LEVEL2_HP, .startMoney = 160, .pathLen = 30,
      .grassA = { 132, 162, 74, 255 }, .grassB = { 122, 152, 68, 255 } },

    { .name = "Estufa", .desc = "Duas entradas e estrada curtinha",
      .map = {
        "....................",
        "........#..#........",
        "S=====........+++++T",
        ".....=........+.....",
        ".....=...#....+.....",
        "..#..=........+..#..",
        ".....=........+.....",
        ".....=====+++++.....",
        "#........=.........#",
        ".......#.=..#.......",
        "..#......=.......#..",
        ".........B..........",
      },
      .secondWave = 4, .hpMult = LEVEL3_HP, .startMoney = 180, .pathLen = 18,
      .grassA = { 80, 152, 108, 255 }, .grassB = { 72, 142, 100, 255 } },
};

/* ------------------------------------------------------------------ */
/* Tipos                                                              */
/* ------------------------------------------------------------------ */
typedef enum { TILE_EMPTY, TILE_ROCK, TILE_TOWER, TILE_ROAD } TileType;
typedef enum { STATE_MENU, STATE_LEVELS, STATE_PLAYING, STATE_PAUSED, STATE_SHOP, STATE_TUTORIAL, STATE_GAMEOVER, STATE_VICTORY } GameState;
typedef enum {
    TOWER_BASIC, TOWER_SNIPER,                    /* liberadas desde o inicio */
    TOWER_FROST,                                  /* comprada na loja */
    TOWER_CANNON, TOWER_POISON, TOWER_LASER,      /* compradas na loja */
    TOWER_COUNT
} TowerKind;
typedef enum { ENEMY_NORMAL, ENEMY_FAST, ENEMY_TANK } EnemyKind;

typedef struct {
    const char *name;
    const char *desc;
    int   cost;       /* preco em dinheiro para construir */
    int   seedPrice;   /* preco em sementes para liberar na loja (0 = ja liberada) */
    float range;      /* pixels */
    float fireRate;   /* tiros por segundo */
    float damage;
    float projSpeed;
    float projSize;
    float slow;       /* 0..1, quanto reduz a velocidade do alvo */
    float splash;     /* raio de dano em area (0 = sem area) */
    float poisonDps;  /* dano por segundo do veneno (0 = sem veneno) */
    float poisonPct;  /* veneno extra: fracao da vida maxima do alvo por segundo */
    Color color;
} TowerInfo;

/* Veneno da Urtiga tambem tira uma % da vida maxima, para continuar util nas ondas finais */
#ifndef POISON_PCT
#define POISON_PCT 0.03f
#endif

/* As "torres" sao plantas: cada uma e desenhada com formas simples em DrawPlant() */
static const TowerInfo TOWER_INFO[TOWER_COUNT] = {
    { .name = "Brotinho", .desc = "Cospe sementes. Barato e equilibrado",
      .cost = 70,  .seedPrice = 0, .range = 120, .fireRate = 1.6f, .damage = 10, .projSpeed = 420, .projSize = 4,
      .color = { 90, 200, 70, 255 } },
    { .name = "Cacto",    .desc = "Espinhos de longo alcance",
      .cost = 135, .seedPrice = 0, .range = 260, .fireRate = 0.5f, .damage = 45, .projSpeed = 900, .projSize = 3,
      .color = { 46, 139, 87, 255 } },
    { .name = "Menta",    .desc = "Folhas geladas deixam lento",
      .cost = 70,  .seedPrice = 4, .range = 100, .fireRate = 1.0f, .damage = 4,  .projSpeed = 350, .projSize = 4,
      .slow = 0.5f, .color = { 120, 225, 205, 255 } },
    { .name = "Abóbora",  .desc = "Abóbora explosiva: dano em área",
      .cost = 140, .seedPrice = 14, .range = 130, .fireRate = 0.6f, .damage = 22, .projSpeed = 300, .projSize = 7,
      .splash = 55, .color = { 255, 140, 0, 255 } },
    { .name = "Urtiga",   .desc = "Espinhos venenosos: dano contínuo",
      .cost = 70,  .seedPrice = 6, .range = 110, .fireRate = 1.0f, .damage = 2,  .projSpeed = 380, .projSize = 4,
      .poisonDps = 8, .poisonPct = POISON_PCT, .color = { 155, 70, 175, 255 } },
    { .name = "Girassol", .desc = "Raios de sol muito rápidos",
      .cost = 75,  .seedPrice = 10, .range = 150, .fireRate = 6.0f, .damage = 3,  .projSpeed = 1200, .projSize = 2,
      .color = { 255, 210, 0, 255 } },
};

#define POISON_TIME 3.0f

typedef struct {
    bool      active;
    TowerKind kind;
    int       level;
    int       invested;   /* total gasto (para calcular a venda) */
    float     cooldown;
    float     angle;      /* angulo do canhao em radianos */
    float     recoil;     /* 1 logo apos atirar, cai ate 0 (animacao do tranco) */
} Tower;

typedef struct {
    bool    active;
    int     uid;          /* identifica o inimigo mesmo se o slot for reutilizado */
    EnemyKind kind;
    Vector2 pos;
    int     tc, tr;       /* tile para onde esta indo */
    float   hp, maxHp;
    float   speed;
    float   slowTimer, slowFactor;
    float   poisonTimer, poisonDps, poisonPct;
    int     reward;
    float   radius;
    Color   color;
    float   hitFlash;     /* pisca branco ao levar dano */
} Enemy;

typedef struct {
    bool    active;
    Vector2 pos;
    int     target, targetUid;
    float   damage, slow, speed, size, splash, poisonDps, poisonPct;
    Color   color;
    int     kind;         /* planta que atirou (cada uma tem um tiro desenhado diferente) */
    Vector2 dir;          /* direcao do movimento (para rastros e espinhos) */
    float   total, done;  /* distancia total e percorrida (arco da abobora) */
} Projectile;

/* Efeito visual simples (explosao do canhao) */
typedef struct {
    bool    active;
    Vector2 pos;
    float   radius, life, maxLife;
    Color   color;
} Effect;

/* Particula: faiscas, poeira, fumaca... */
typedef struct {
    bool    active;
    Vector2 pos, vel;
    float   life, maxLife;
    float   size, grow;   /* tamanho inicial e multiplicador no fim da vida */
    float   gravity;
    Color   color;
} Particle;

/* Texto que sobe e some (ex.: "+$5" quando um inimigo morre) */
typedef struct {
    bool    active;
    Vector2 pos;
    float   life;
    char    text[16];
    Color   color;
} FloatText;

/* ------------------------------------------------------------------ */
/* Estado global do jogo                                              */
/* ------------------------------------------------------------------ */
static TileType   grid[ROWS][COLS];
static int        dist[ROWS][COLS];       /* distancia (em tiles) ate a base */
static Tower      towers[ROWS][COLS];
static Enemy      enemies[MAX_ENEMIES];
static Projectile projs[MAX_PROJ];
static Effect     effects[MAX_FX];
static FloatText  floats[MAX_FLOAT];
static Particle   parts[MAX_PART];
static float      shake;                  /* tremida da tela quando a casa e atingida */
static float      bannerTimer;            /* faixa animada no comeco de cada onda */
static char       bannerText[24], bannerSub[48];
static bool       bannerBoss;
static float      smokeTimer;
static float      baseFlash;              /* pisca a casa quando um inimigo chega */

static int spawnC, spawnR, baseC, baseR;
static int spawn2C = -1, spawn2R = -1;    /* segunda entrada ('T') */
static bool branchRoad[ROWS][COLS];       /* tiles da estrada da segunda entrada */

static GameState state = STATE_MENU;
static GameState shopReturn = STATE_MENU;  /* para onde voltar ao fechar a loja */

static int   currentLevel = 0;            /* fase sendo jogada (0 = Jardim) */
static int   levelsWon = 0;               /* bit i ligado = fase i vencida (salvo em arquivo) */
#define SECOND_ENTRANCE_WAVE (LEVELS[currentLevel].secondWave)

static bool LevelUnlocked(int i) { return i == 0 || (levelsWon & (1 << (i - 1))); }

/* Progresso permanente (salvo em arquivo) */
static int   seeds;
static bool  unlocked[TOWER_COUNT];

static bool  upgradesUnlocked;            /* vira true ao terminar a onda anterior a UPGRADE_UNLOCK_WAVE */
static int   money, lives, wave;
static bool  waveActive;
static int   waveSize, spawnedInWave;
static float spawnTimer;
static int   nextUid;
static int   gameSpeed = 1;
static bool  showDist = false;

static int   selBuild = -1;               /* tipo de torre escolhido para construir */
static int   selC = -1, selR = -1;        /* torre selecionada no mapa */

static char  msgText[64];
static float msgTimer;

/* ------------------------------------------------------------------ */
/* Utilitarios                                                        */
/* ------------------------------------------------------------------ */
static bool SecondEntranceOpen(void) { return spawn2C >= 0 && wave >= SECOND_ENTRANCE_WAVE; }

static Vector2 TileCenter(int c, int r)
{
    return (Vector2){ c * TILE + TILE / 2.0f, r * TILE + TILE / 2.0f };
}

static bool InBounds(int c, int r) { return c >= 0 && c < COLS && r >= 0 && r < ROWS; }

static void ShowMessage(const char *text)
{
    strncpy(msgText, text, sizeof(msgText) - 1);
    msgText[sizeof(msgText) - 1] = '\0';
    msgTimer = 2.0f;
}

static float RandF(float a, float b) { return a + (b - a) * (GetRandomValue(0, 10000) / 10000.0f); }

static void SpawnParticle(Vector2 pos, Vector2 vel, float life, float size, float grow, float gravity, Color color)
{
    for (int i = 0; i < MAX_PART; i++) {
        if (parts[i].active) continue;
        parts[i] = (Particle){ true, pos, vel, life, life, size, grow, gravity, color };
        return;
    }
}

/* Explosao de n particulas em todas as direcoes */
static void Burst(Vector2 pos, int n, float speed, float life, float size, float gravity, Color color)
{
    for (int i = 0; i < n; i++) {
        float an = RandF(0, 2 * PI), sp = RandF(speed * 0.35f, speed);
        SpawnParticle(pos, (Vector2){ cosf(an) * sp, sinf(an) * sp }, RandF(life * 0.6f, life),
                      RandF(size * 0.6f, size), 0.2f, gravity, color);
    }
}

static void SpawnFloat(Vector2 pos, const char *text, Color color)
{
    for (int i = 0; i < MAX_FLOAT; i++) {
        if (floats[i].active) continue;
        floats[i].active = true;
        floats[i].pos = pos;
        floats[i].life = 1.0f;
        floats[i].color = color;
        strncpy(floats[i].text, text, sizeof(floats[i].text) - 1);
        floats[i].text[sizeof(floats[i].text) - 1] = '\0';
        return;
    }
}

static const int DC[4] = { 1, -1, 0, 0 };
static const int DR[4] = { 0, 0, 1, -1 };

/* ------------------------------------------------------------------ */
/* Sons: todos gerados pelo proprio codigo (ondas senoidais, quadradas */
/* e ruido), sem nenhum arquivo de audio                              */
/* ------------------------------------------------------------------ */
typedef enum {
    SFX_SHOOT, SFX_POP, SFX_BOOM, SFX_WAVE, SFX_BOSS, SFX_HURT, SFX_PLANT,
    SFX_UPGRADE, SFX_SELL, SFX_ERROR, SFX_CLICK, SFX_WIN, SFX_LOSE, SFX_COUNT
} SfxId;

#define SFX_VOICES  4          /* copias de cada som, para tocar varias vezes ao mesmo tempo */
#define SFX_RATE    22050

static Sound  sfx[SFX_COUNT][SFX_VOICES];
static int    sfxNext[SFX_COUNT];
static double sfxLast[SFX_COUNT];
static bool   soundsReady = false;
static bool   soundOn = true;      /* tecla M liga/desliga (salvo no progresso) */

/* Recordes por fase (salvos no progresso) */
static int  bestWave[LEVEL_COUNT];   /* melhor onda alcancada (20 = venceu) */
static int  bestLives[LEVEL_COUNT];  /* mais vidas sobrando numa vitoria */
static bool newRecord = false;       /* a partida que acabou bateu o recorde? */

/* forma: 0 = senoide, 1 = quadrada, 2 = triangulo */
static void SynthAdd(float *buf, int total, float start, float dur, float f0, float f1,
                     int shape, float noise, float vol)
{
    int s0 = (int)(start * SFX_RATE), n = (int)(dur * SFX_RATE);
    float phase = 0;
    unsigned int seed = 12345u + (unsigned int)s0;
    for (int i = 0; i < n && s0 + i < total; i++) {
        float t = (float)i / n;
        phase += 2 * PI * (f0 + (f1 - f0) * t) / SFX_RATE;
        float v = shape == 1 ? (sinf(phase) >= 0 ? 1.0f : -1.0f)
                : shape == 2 ? 2.0f / PI * asinf(sinf(phase))
                : sinf(phase);
        seed = seed * 1664525u + 1013904223u;
        float nz = (seed >> 9) / 8388608.0f * 2 - 1;
        v = v * (1 - noise) + nz * noise;
        float env = (t < 0.04f ? t / 0.04f : 1.0f) * (1 - t) * (1 - t);   /* ataque rapido e queda */
        buf[s0 + i] += v * env * vol;
    }
}

static Sound MakeSound(const float *buf, int n)
{
    short *pcm = (short *)malloc(n * sizeof(short));
    for (int i = 0; i < n; i++) pcm[i] = (short)(Clamp(buf[i], -1, 1) * 32000);
    Wave w = { (unsigned int)n, SFX_RATE, 16, 1, pcm };
    Sound snd = LoadSoundFromWave(w);
    free(pcm);
    return snd;
}

static void LoadSounds(void)
{
    if (!IsAudioDeviceReady()) return;
    static float buf[SFX_RATE];          /* 1 segundo no maximo */
    for (int id = 0; id < SFX_COUNT; id++) {
        memset(buf, 0, sizeof(buf));
        float dur = 0.2f, vol = 0.7f;
        switch (id) {
            case SFX_SHOOT:   dur = 0.07f; vol = 0.35f; SynthAdd(buf, SFX_RATE, 0, dur, 900, 420, 0, 0.1f, 0.6f); break;
            case SFX_POP:     dur = 0.14f; vol = 0.5f;  SynthAdd(buf, SFX_RATE, 0, dur, 700, 160, 1, 0, 0.25f);
                                                        SynthAdd(buf, SFX_RATE, 0, 0.07f, 300, 300, 0, 1, 0.3f); break;
            case SFX_BOOM:    dur = 0.45f;              SynthAdd(buf, SFX_RATE, 0, dur, 130, 40, 0, 0.85f, 0.9f); break;
            case SFX_WAVE:    dur = 0.45f;              SynthAdd(buf, SFX_RATE, 0.00f, 0.14f, 523, 523, 2, 0, 0.45f);
                                                        SynthAdd(buf, SFX_RATE, 0.12f, 0.14f, 659, 659, 2, 0, 0.45f);
                                                        SynthAdd(buf, SFX_RATE, 0.24f, 0.21f, 784, 784, 2, 0, 0.45f); break;
            case SFX_BOSS:    dur = 0.9f;               SynthAdd(buf, SFX_RATE, 0.00f, 0.42f, 110, 100, 1, 0, 0.22f);
                                                        SynthAdd(buf, SFX_RATE, 0.00f, 0.42f, 165, 150, 1, 0, 0.14f);
                                                        SynthAdd(buf, SFX_RATE, 0.45f, 0.45f, 98, 82, 1, 0, 0.22f);
                                                        SynthAdd(buf, SFX_RATE, 0.45f, 0.45f, 147, 123, 1, 0, 0.14f); break;
            case SFX_HURT:    dur = 0.3f;               SynthAdd(buf, SFX_RATE, 0, dur, 170, 55, 1, 0, 0.35f);
                                                        SynthAdd(buf, SFX_RATE, 0, 0.15f, 200, 200, 0, 1, 0.3f); break;
            case SFX_PLANT:   dur = 0.13f;              SynthAdd(buf, SFX_RATE, 0, dur, 280, 560, 0, 0.05f, 0.6f); break;
            case SFX_UPGRADE: dur = 0.38f;              SynthAdd(buf, SFX_RATE, 0.00f, 0.12f, 660, 660, 2, 0, 0.45f);
                                                        SynthAdd(buf, SFX_RATE, 0.09f, 0.12f, 880, 880, 2, 0, 0.45f);
                                                        SynthAdd(buf, SFX_RATE, 0.18f, 0.20f, 1320, 1320, 2, 0, 0.4f); break;
            case SFX_SELL:    dur = 0.24f;              SynthAdd(buf, SFX_RATE, 0.00f, 0.11f, 880, 880, 2, 0, 0.4f);
                                                        SynthAdd(buf, SFX_RATE, 0.10f, 0.14f, 587, 587, 2, 0, 0.4f); break;
            case SFX_ERROR:   dur = 0.18f; vol = 0.5f;  SynthAdd(buf, SFX_RATE, 0.00f, 0.08f, 190, 190, 1, 0, 0.25f);
                                                        SynthAdd(buf, SFX_RATE, 0.09f, 0.09f, 160, 160, 1, 0, 0.25f); break;
            case SFX_CLICK:   dur = 0.04f; vol = 0.45f; SynthAdd(buf, SFX_RATE, 0, dur, 1300, 900, 0, 0, 0.5f); break;
            case SFX_WIN:     dur = 0.95f;              SynthAdd(buf, SFX_RATE, 0.00f, 0.16f, 523, 523, 2, 0, 0.45f);
                                                        SynthAdd(buf, SFX_RATE, 0.15f, 0.16f, 659, 659, 2, 0, 0.45f);
                                                        SynthAdd(buf, SFX_RATE, 0.30f, 0.16f, 784, 784, 2, 0, 0.45f);
                                                        SynthAdd(buf, SFX_RATE, 0.45f, 0.50f, 1047, 1047, 2, 0, 0.45f); break;
            case SFX_LOSE:    dur = 0.95f;              SynthAdd(buf, SFX_RATE, 0.00f, 0.28f, 392, 380, 2, 0, 0.45f);
                                                        SynthAdd(buf, SFX_RATE, 0.27f, 0.28f, 330, 318, 2, 0, 0.45f);
                                                        SynthAdd(buf, SFX_RATE, 0.54f, 0.41f, 262, 240, 2, 0, 0.45f); break;
        }
        sfx[id][0] = MakeSound(buf, (int)(dur * SFX_RATE));
        SetSoundVolume(sfx[id][0], vol);
        for (int v = 1; v < SFX_VOICES; v++) {
            sfx[id][v] = LoadSoundAlias(sfx[id][0]);
            SetSoundVolume(sfx[id][v], vol);
        }
    }
    soundsReady = true;
}

static void UnloadSounds(void)
{
    if (!soundsReady) return;
    for (int id = 0; id < SFX_COUNT; id++) {
        for (int v = 1; v < SFX_VOICES; v++) UnloadSoundAlias(sfx[id][v]);
        UnloadSound(sfx[id][0]);
    }
    soundsReady = false;
}

/* Toca um som (com tom ajustavel). Tiros e estouros tem um intervalo minimo
   para nao virar barulho quando muitas plantas atiram juntas. */
static void SfxPitch(SfxId id, float pitch)
{
    if (!soundsReady || !soundOn) return;
    double now = GetTime();
    double gap = id == SFX_SHOOT ? 0.05 : id == SFX_POP ? 0.035 : 0.0;
    if (now - sfxLast[id] < gap) return;
    sfxLast[id] = now;
    Sound snd = sfx[id][sfxNext[id]];
    sfxNext[id] = (sfxNext[id] + 1) % SFX_VOICES;
    SetSoundPitch(snd, pitch);
    PlaySound(snd);
}

static void Sfx(SfxId id) { SfxPitch(id, 1.0f); }

static float TowerRange(const Tower *t)    { return TOWER_INFO[t->kind].range    * (1.0f + UPG_RANGE    * (t->level - 1)); }
static float TowerDamage(const Tower *t)   { return TOWER_INFO[t->kind].damage   * (1.0f + UPG_DAMAGE   * (t->level - 1)); }
static float TowerPoison(const Tower *t)   { return TOWER_INFO[t->kind].poisonDps * (1.0f + UPG_DAMAGE  * (t->level - 1)); }
/* Menta: 50% de lentidao no nivel 1, +10% por nivel. Abobora: explosao +15% por nivel */
static float TowerSlow(const Tower *t)   { return TOWER_INFO[t->kind].slow > 0 ? TOWER_INFO[t->kind].slow + 0.10f * (t->level - 1) : 0; }
static float TowerSplash(const Tower *t) { return TOWER_INFO[t->kind].splash * (1.0f + 0.15f * (t->level - 1)); }
#define FROST_AREA 42.0f   /* a lentidao da Menta pega tambem as pragas a ate 42 px do alvo */
static float TowerFireRate(const Tower *t) { return TOWER_INFO[t->kind].fireRate * (1.0f + UPG_FIRERATE * (t->level - 1)); }
static int   UpgradeCost(const Tower *t)   { return TOWER_INFO[t->kind].cost * t->level; }
static int   SellValue(const Tower *t)     { return (int)(t->invested * 0.7f); }

/* ------------------------------------------------------------------ */
/* Progresso salvo e loja                                             */
/* ------------------------------------------------------------------ */
/* Formato do arquivo:
 *   sementes 7
 *   torres 1 1 1 1 0 0
 *   fases 1          (bit i = fase i vencida: 1 = Jardim, 3 = Jardim e Horta)
 *   recordes 20 12 0 (melhor onda alcancada em cada fase; 20 = venceu)
 *   vidas 14 0 0     (mais vidas sobrando numa vitoria, por fase)
 *   som 1            (1 = ligado, 0 = desligado)
 */
static void SaveProgress(void)
{
    FILE *f = fopen(SAVE_FILE, "w");
    if (!f) return;
    fprintf(f, "sementes %d\ntorres", seeds);
    for (int i = 0; i < TOWER_COUNT; i++) fprintf(f, " %d", unlocked[i] ? 1 : 0);
    fprintf(f, "\nfases %d\nrecordes", levelsWon);
    for (int i = 0; i < LEVEL_COUNT; i++) fprintf(f, " %d", bestWave[i]);
    fprintf(f, "\nvidas");
    for (int i = 0; i < LEVEL_COUNT; i++) fprintf(f, " %d", bestLives[i]);
    fprintf(f, "\nsom %d\n", soundOn ? 1 : 0);
    fclose(f);
}

static void LoadProgress(void)
{
    seeds = 0;
    levelsWon = 0;
    soundOn = true;
    for (int i = 0; i < LEVEL_COUNT; i++) bestWave[i] = bestLives[i] = 0;
    for (int i = 0; i < TOWER_COUNT; i++) unlocked[i] = (TOWER_INFO[i].seedPrice == 0);

    FILE *f = fopen(SAVE_FILE, "r");
    if (!f) return;
    int g;
    if (fscanf(f, " %*s %d", &g) == 1 && g >= 0) seeds = g;
    if (fscanf(f, " torres") == 0) {
        for (int i = 0; i < TOWER_COUNT; i++) {
            int v;
            if (fscanf(f, " %d", &v) != 1) break;
            if (v) unlocked[i] = true;
        }
    }
    int fw;
    if (fscanf(f, " fases %d", &fw) == 1 && fw >= 0) levelsWon = fw;   /* arquivos antigos nao tem esta linha */
    /* linhas opcionais (versoes antigas do arquivo nao tem): %n diz se a palavra foi lida */
    int got = 0, v;
    if (fscanf(f, " recordes%n", &got) >= 0 && got > 0)
        for (int i = 0; i < LEVEL_COUNT && fscanf(f, " %d", &v) == 1; i++) bestWave[i] = v;
    got = 0;
    if (fscanf(f, " vidas%n", &got) >= 0 && got > 0)
        for (int i = 0; i < LEVEL_COUNT && fscanf(f, " %d", &v) == 1; i++) bestLives[i] = v;
    if (fscanf(f, " som %d", &v) == 1) soundOn = (v != 0);
    fclose(f);
}

static void AddSeeds(int n)
{
    seeds += n;
    SaveProgress();
}

static void BuyTower(int kind)
{
    if (unlocked[kind]) return;
    if (seeds < TOWER_INFO[kind].seedPrice) { ShowMessage("Sementes insuficientes"); Sfx(SFX_ERROR); return; }
    seeds -= TOWER_INFO[kind].seedPrice;
    unlocked[kind] = true;
    SaveProgress();
    Sfx(SFX_UPGRADE);
    ShowMessage(TextFormat("%s liberada! Tecla %d", TOWER_INFO[kind].name, kind + 1));
}

static void OpenShop(void)
{
    shopReturn = state;
    state = STATE_SHOP;
}

/* ------------------------------------------------------------------ */
/* Mapa e BFS                                                         */
/* ------------------------------------------------------------------ */
static void LoadMap(void)
{
    const char *const *map = LEVELS[currentLevel].map;
    spawn2C = spawn2R = -1;
    for (int r = 0; r < ROWS; r++) {
        for (int c = 0; c < COLS; c++) {
            char ch = (c < (int)strlen(map[r])) ? map[r][c] : '.';
            grid[r][c] = TILE_EMPTY;
            branchRoad[r][c] = false;
            if (ch == '#') grid[r][c] = TILE_ROCK;
            else if (ch == '=') grid[r][c] = TILE_ROAD;
            else if (ch == '+') { grid[r][c] = TILE_ROAD; branchRoad[r][c] = true; }
            else if (ch == 'S') { grid[r][c] = TILE_ROAD; spawnC = c; spawnR = r; }
            else if (ch == 'T') { grid[r][c] = TILE_ROAD; branchRoad[r][c] = true; spawn2C = c; spawn2R = r; }
            else if (ch == 'B') { grid[r][c] = TILE_ROAD; baseC = c; baseR = r; }
        }
    }
}

/* BFS a partir da base, andando so pela estrada:
   out[r][c] = numero de passos ate a base (INF = fora da estrada).
   Retorna true se a entrada dos inimigos consegue chegar na base. */
static bool ComputeDistances(int out[ROWS][COLS])
{
    static int qc[ROWS * COLS], qr[ROWS * COLS];
    int head = 0, tail = 0;

    for (int r = 0; r < ROWS; r++)
        for (int c = 0; c < COLS; c++)
            out[r][c] = INF;

    out[baseR][baseC] = 0;
    qc[tail] = baseC; qr[tail] = baseR; tail++;

    while (head < tail) {
        int c = qc[head], r = qr[head]; head++;
        for (int i = 0; i < 4; i++) {
            int nc = c + DC[i], nr = r + DR[i];
            if (!InBounds(nc, nr)) continue;
            if (grid[nr][nc] != TILE_ROAD) continue;
            if (out[nr][nc] != INF) continue;
            out[nr][nc] = out[r][c] + 1;
            qc[tail] = nc; qr[tail] = nr; tail++;
        }
    }
    return out[spawnR][spawnC] < INF;
}

/* Torres so podem ser construidas na grama livre (nunca na estrada) */
static bool CanPlace(int c, int r, const char **reason)
{
    *reason = NULL;
    if (waveActive && grid[r][c] == TILE_EMPTY) { *reason = "Espere a onda terminar para plantar"; return false; }
    switch (grid[r][c]) {
        case TILE_EMPTY: return true;
        case TILE_ROAD:  *reason = "Não pode plantar na estrada!"; return false;
        case TILE_ROCK:  *reason = "Não pode plantar na pedra";    return false;
        default:         *reason = "Espaço ocupado";                    return false;
    }
}

/* ------------------------------------------------------------------ */
/* Jogo                                                               */
/* ------------------------------------------------------------------ */
static void ResetGame(void)
{
    LoadMap();
    memset(towers, 0, sizeof(towers));
    memset(enemies, 0, sizeof(enemies));
    memset(projs, 0, sizeof(projs));
    memset(effects, 0, sizeof(effects));
    memset(floats, 0, sizeof(floats));
    memset(parts, 0, sizeof(parts));
    baseFlash = 0;
    shake = 0;
    bannerTimer = 0;
    ComputeDistances(dist);

    money = LEVELS[currentLevel].startMoney;
    lives = START_LIVES;
    wave = 0;
    waveActive = false;
    waveSize = spawnedInWave = 0;
    spawnTimer = 0;
    nextUid = 1;
    gameSpeed = 1;
    selBuild = -1;
    selC = selR = -1;
    msgTimer = 0;
    upgradesUnlocked = false;
}

static void PlaceTower(int c, int r, TowerKind kind)
{
    grid[r][c] = TILE_TOWER;
    towers[r][c] = (Tower){ .active = true, .kind = kind, .level = 1,
                            .invested = TOWER_INFO[kind].cost, .cooldown = 0, .angle = 0 };
    money -= TOWER_INFO[kind].cost;
    Sfx(SFX_PLANT);
}

static void UpgradeSelected(void)
{
    if (selC < 0) return;
    Tower *t = &towers[selR][selC];
    if (!upgradesUnlocked) { ShowMessage(TextFormat("Melhorias liberadas na onda %d", UPGRADE_UNLOCK_WAVE)); Sfx(SFX_ERROR); return; }
    if (t->level >= MAX_LEVEL) { ShowMessage("Nível máximo"); Sfx(SFX_ERROR); return; }
    int cost = UpgradeCost(t);
    if (money < cost) { ShowMessage("Dinheiro insuficiente"); Sfx(SFX_ERROR); return; }
    money -= cost;
    t->invested += cost;
    t->level++;
    Sfx(SFX_UPGRADE);
}

static void SellSelected(void)
{
    if (selC < 0) return;
    Tower *t = &towers[selR][selC];
    money += SellValue(t);
    Sfx(SFX_SELL);
    t->active = false;
    grid[selR][selC] = TILE_EMPTY;
    selC = selR = -1;
}

static void StartWave(void)
{
    if (waveActive || wave >= MAX_WAVES) return;
    wave++;
    waveActive = true;
    waveSize = 6 + wave * 2;
    spawnedInWave = 0;
    bannerTimer = BANNER_TIME;
    bannerBoss = (wave % 5 == 0);
    snprintf(bannerText, sizeof(bannerText), "ONDA %d", wave);
    if (bannerBoss)                                          snprintf(bannerSub, sizeof(bannerSub), "Chefe: caracol gigante!");
    else if (spawn2C >= 0 && wave == SECOND_ENTRANCE_WAVE)   snprintf(bannerSub, sizeof(bannerSub), "Nova entrada aberta!");
    else                                                     snprintf(bannerSub, sizeof(bannerSub), "%d pragas a caminho", waveSize);
    selBuild = -1;   /* so da para plantar entre as ondas */
    spawnTimer = 0;
    Sfx(bannerBoss ? SFX_BOSS : SFX_WAVE);
}

/* Acaba a partida: atualiza recordes, salva e toca o som de vitoria ou derrota */
static void FinishGame(bool victory)
{
    int lv = currentLevel;
    newRecord = false;
    if (victory) {
        levelsWon |= 1 << lv;
        newRecord = bestWave[lv] < MAX_WAVES || lives > bestLives[lv];
        bestWave[lv] = MAX_WAVES;
        if (lives > bestLives[lv]) bestLives[lv] = lives;
    } else if (wave > bestWave[lv] && bestWave[lv] < MAX_WAVES) {
        newRecord = bestWave[lv] > 0;      /* a primeira partida da fase nao conta como recorde batido */
        bestWave[lv] = wave;
    }
    state = victory ? STATE_VICTORY : STATE_GAMEOVER;
    SaveProgress();
    Sfx(victory ? SFX_WIN : SFX_LOSE);
}

static void SpawnEnemy(void)
{
    for (int i = 0; i < MAX_ENEMIES; i++) {
        if (enemies[i].active) continue;
        Enemy *e = &enemies[i];
        float baseHp = ENEMY_BASE_HP * LEVELS[currentLevel].hpMult * powf(HP_GROWTH, (float)(wave - 1));

        EnemyKind kind = ENEMY_NORMAL;
        if (wave % 5 == 0 && spawnedInWave == waveSize - 1) kind = ENEMY_TANK;
        else if (wave >= 3 && spawnedInWave % 3 == 2)       kind = ENEMY_FAST;

        *e = (Enemy){ 0 };
        e->active = true;
        e->uid = nextUid++;
        e->kind = kind;
        /* A partir da onda SECOND_ENTRANCE_WAVE os inimigos alternam entre as duas entradas */
        int sc = spawnC, sr = spawnR;
        if (SecondEntranceOpen() && spawnedInWave % 2 == 1) { sc = spawn2C; sr = spawn2R; }
        e->pos = TileCenter(sc, sr);
        e->tc = sc; e->tr = sr;
        e->slowFactor = 1.0f;

        switch (kind) {
            case ENEMY_NORMAL: e->hp = baseHp;        e->speed = 55; e->reward = 5;  e->radius = 10; e->color = RED;    break;
            case ENEMY_FAST:   e->hp = baseHp * 0.6f; e->speed = 95; e->reward = 6;  e->radius = 8;  e->color = ORANGE; break;
            case ENEMY_TANK:   e->hp = baseHp * 8.0f; e->speed = 35; e->reward = 40; e->radius = 16; e->color = MAROON; break;
        }
        e->maxHp = e->hp;
        spawnedInWave++;
        return;
    }
}

/* Escolhe o vizinho com menor distancia ate a base (segue o mapa da BFS) */
static void PickNextTile(Enemy *e)
{
    int best = dist[e->tr][e->tc];
    int bc = e->tc, br = e->tr;
    for (int i = 0; i < 4; i++) {
        int nc = e->tc + DC[i], nr = e->tr + DR[i];
        if (!InBounds(nc, nr)) continue;
        if (dist[nr][nc] < best) { best = dist[nr][nc]; bc = nc; br = nr; }
    }
    e->tc = bc; e->tr = br;
}

/* Aplica dano; se o inimigo morrer, da a recompensa */
static void DamageEnemy(Enemy *e, float dmg)
{
    if (!e->active) return;
    e->hp -= dmg;
    if (dmg > 0.5f) e->hitFlash = 0.1f;      /* o veneno (dano pequeno por frame) nao pisca */
    if (e->hp <= 0) {
        e->active = false;
        money += e->reward;
        Burst(e->pos, e->kind == ENEMY_TANK ? 18 : 8, 70, 0.45f, e->kind == ENEMY_TANK ? 7 : 4.5f, 0, (Color){ 235, 230, 220, 200 });
        Burst(e->pos, e->kind == ENEMY_TANK ? 12 : 5, 110, 0.55f, 3, 220, e->color);
        SpawnFloat(e->pos, TextFormat("+$%d", e->reward), (Color){ 255, 214, 80, 255 });
        SfxPitch(SFX_POP, RandF(0.9f, 1.15f));
        if (e->kind == ENEMY_TANK) {
            AddSeeds(SEEDS_PER_TANK);
            ShowMessage(TextFormat("Tanque destruído! +%d sementes", SEEDS_PER_TANK));
        }
    }
}

static void UpdateEnemies(float dt)
{
    for (int i = 0; i < MAX_ENEMIES; i++) {
        Enemy *e = &enemies[i];
        if (!e->active) continue;

        if (e->hitFlash > 0) e->hitFlash -= dt;

        if (e->poisonTimer > 0) {
            e->poisonTimer -= dt;
            DamageEnemy(e, (e->poisonDps + e->poisonPct * e->maxHp) * dt);
            if (!e->active) continue;
        }

        if (e->slowTimer > 0) {
            e->slowTimer -= dt;
            if (e->slowTimer <= 0) e->slowFactor = 1.0f;
        }

        float step = e->speed * e->slowFactor * dt;
        Vector2 target = TileCenter(e->tc, e->tr);
        float d = Vector2Distance(e->pos, target);

        if (d <= step) {
            e->pos = target;
            if (e->tc == baseC && e->tr == baseR) {
                e->active = false;
                lives -= (e->kind == ENEMY_TANK) ? 5 : 1;
                baseFlash = 0.4f;
                shake = 0.35f;
                Sfx(SFX_HURT);
                Burst(TileCenter(baseC, baseR), 12, 110, 0.5f, 4, 0, (Color){ 230, 60, 50, 255 });
                if (lives <= 0) { lives = 0; FinishGame(false); }
                continue;
            }
            PickNextTile(e);
        } else {
            e->pos = Vector2Add(e->pos, Vector2Scale(Vector2Subtract(target, e->pos), step / d));
        }
    }
}

static void FireProjectile(const Tower *t, Vector2 from, int targetIdx)
{
    for (int i = 0; i < MAX_PROJ; i++) {
        if (projs[i].active) continue;
        projs[i] = (Projectile){
            .active = true, .pos = from,
            .target = targetIdx, .targetUid = enemies[targetIdx].uid,
            .damage = TowerDamage(t), .slow = TowerSlow(t),
            .speed = TOWER_INFO[t->kind].projSpeed, .size = TOWER_INFO[t->kind].projSize,
            .splash = TowerSplash(t), .poisonDps = TowerPoison(t),
            .poisonPct = TOWER_INFO[t->kind].poisonPct * (1.0f + UPG_DAMAGE * (t->level - 1)),
            .color = TOWER_INFO[t->kind].color,
            .kind = t->kind,
            .dir = Vector2Normalize(Vector2Subtract(enemies[targetIdx].pos, from)),
            .total = fmaxf(1.0f, Vector2Distance(enemies[targetIdx].pos, from)),
        };
        static const float SHOT_PITCH[TOWER_COUNT] = { 1.0f, 0.7f, 1.35f, 0.55f, 1.15f, 1.6f };
        SfxPitch(SFX_SHOOT, SHOT_PITCH[t->kind] * RandF(0.95f, 1.05f));
        return;
    }
}

static void UpdateTowers(float dt)
{
    for (int r = 0; r < ROWS; r++) {
        for (int c = 0; c < COLS; c++) {
            Tower *t = &towers[r][c];
            if (!t->active) continue;
            if (t->cooldown > 0) t->cooldown -= dt;
            if (t->recoil > 0) t->recoil = fmaxf(0, t->recoil - dt * 5);

            Vector2 ctr = TileCenter(c, r);
            float range = TowerRange(t);

            /* Mira no inimigo mais adiantado (menor distancia ate a base) dentro do alcance */
            int best = -1, bestDist = INF + 1;
            float bestTie = 1e9f;
            for (int i = 0; i < MAX_ENEMIES; i++) {
                Enemy *e = &enemies[i];
                if (!e->active) continue;
                if (Vector2Distance(ctr, e->pos) > range + e->radius) continue;
                int dd = dist[e->tr][e->tc];
                float tie = Vector2Distance(e->pos, TileCenter(e->tc, e->tr));
                if (dd < bestDist || (dd == bestDist && tie < bestTie)) {
                    best = i; bestDist = dd; bestTie = tie;
                }
            }

            if (best >= 0) {
                Vector2 dir = Vector2Subtract(enemies[best].pos, ctr);
                t->angle = atan2f(dir.y, dir.x);
                if (t->cooldown <= 0) {
                    FireProjectile(t, ctr, best);
                    t->cooldown = 1.0f / TowerFireRate(t);
                    t->recoil = 1.0f;
                }
            }
        }
    }
}

static void SpawnEffect(Vector2 pos, float radius, Color color)
{
    for (int i = 0; i < MAX_FX; i++) {
        if (effects[i].active) continue;
        effects[i] = (Effect){ true, pos, radius, 0.3f, 0.3f, color };
        return;
    }
}

static void UpdateEffects(float dt)
{
    for (int i = 0; i < MAX_FX; i++)
        if (effects[i].active && (effects[i].life -= dt) <= 0) effects[i].active = false;
    for (int i = 0; i < MAX_FLOAT; i++) {
        if (!floats[i].active) continue;
        floats[i].pos.y -= 28 * dt;
        if ((floats[i].life -= dt) <= 0) floats[i].active = false;
    }
    if (baseFlash > 0) baseFlash -= dt;
    if (shake > 0) shake -= dt;
    if (bannerTimer > 0) bannerTimer -= dt;

    for (int i = 0; i < MAX_PART; i++) {
        Particle *q = &parts[i];
        if (!q->active) continue;
        q->vel.y += q->gravity * dt;
        q->vel = Vector2Scale(q->vel, 1.0f - 2.5f * dt);     /* resistencia do ar */
        q->pos = Vector2Add(q->pos, Vector2Scale(q->vel, dt));
        if ((q->life -= dt) <= 0) q->active = false;
    }

    /* fumacinha saindo da chamine da casa */
    if ((smokeTimer -= dt) <= 0) {
        smokeTimer = 0.35f;
        Vector2 chimney = { baseC * TILE + 36.0f, baseR * TILE + 5.0f };
        SpawnParticle(chimney, (Vector2){ RandF(-4, 4), -16 }, 1.8f, 3.5f, 2.6f, -3, (Color){ 225, 225, 220, 120 });
    }
}

/* Aplica os efeitos de um projetil em um inimigo */
static void ApplyHit(Enemy *e, const Projectile *p)
{
    if (p->slow > 0) {
        e->slowFactor = 1.0f - p->slow;
        e->slowTimer = 1.5f;
    }
    if (p->poisonDps > 0) {
        if (p->poisonDps > e->poisonDps || e->poisonTimer <= 0) { e->poisonDps = p->poisonDps; e->poisonPct = p->poisonPct; }
        e->poisonTimer = POISON_TIME;
    }
    DamageEnemy(e, p->damage);
}

static void UpdateProjectiles(float dt)
{
    for (int i = 0; i < MAX_PROJ; i++) {
        Projectile *p = &projs[i];
        if (!p->active) continue;

        Enemy *e = &enemies[p->target];
        if (!e->active || e->uid != p->targetUid) { p->active = false; continue; }

        float step = p->speed * dt;
        float d = Vector2Distance(p->pos, e->pos);
        if (d > 0.01f) p->dir = Vector2Scale(Vector2Subtract(e->pos, p->pos), 1.0f / d);
        p->done += step;
        if (p->done + d > p->total) p->total = p->done + d;   /* o alvo andou: o arco acompanha */
        if (d <= step + e->radius) {
            Burst(e->pos, 4, 60, 0.25f, 2.5f, 0, p->color);
            if (p->splash > 0) {
                Burst(e->pos, 16, 150, 0.45f, 4.5f, 80, (Color){ 255, 150, 40, 255 });
                Sfx(SFX_BOOM);
                /* Dano em area: atinge todos os inimigos dentro do raio */
                Vector2 hit = e->pos;
                for (int j = 0; j < MAX_ENEMIES; j++)
                    if (enemies[j].active && Vector2Distance(hit, enemies[j].pos) <= p->splash + enemies[j].radius)
                        ApplyHit(&enemies[j], p);
                SpawnEffect(hit, p->splash, ORANGE);
            } else {
                if (p->slow > 0) {   /* Menta: a lentidao pega as pragas em volta do alvo */
                    for (int j = 0; j < MAX_ENEMIES; j++) {
                        Enemy *o = &enemies[j];
                        if (!o->active || o == e || Vector2Distance(e->pos, o->pos) > FROST_AREA + o->radius) continue;
                        o->slowFactor = 1.0f - p->slow;
                        o->slowTimer = 1.5f;
                    }
                    Burst(e->pos, 6, 70, 0.35f, 3, 0, (Color){ 200, 240, 255, 230 });
                }
                ApplyHit(e, p);
            }
            p->active = false;
        } else {
            p->pos = Vector2Add(p->pos, Vector2Scale(Vector2Subtract(e->pos, p->pos), step / d));
        }
    }
}

static void UpdateWave(float dt)
{
    if (!waveActive) return;

    if (spawnedInWave < waveSize) {
        spawnTimer -= dt;
        if (spawnTimer <= 0) {
            SpawnEnemy();
            spawnTimer = (wave >= 10) ? 0.55f : 0.8f;
        }
        return;
    }

    for (int i = 0; i < MAX_ENEMIES; i++) if (enemies[i].active) return;

    /* Onda terminou */
    waveActive = false;
    int bonus = 20 + wave * 5;
    int seedBonus = SEEDS_PER_WAVE + ((wave % 5 == 0) ? SEEDS_BOSS_WAVE : 0);
    money += bonus;
    AddSeeds(seedBonus);
    if (wave >= MAX_WAVES) {
        FinishGame(true);
    }
    else if (spawn2C >= 0 && wave == SECOND_ENTRANCE_WAVE - 1)
        ShowMessage("Atenção: na próxima onda abre uma NOVA ENTRADA!");
    else ShowMessage(TextFormat("Onda %d completa! +$%d  +%d semente(s)", wave, bonus, seedBonus));

    /* Libera as melhorias antes da onda UPGRADE_UNLOCK_WAVE e mostra o tutorial */
    if (!upgradesUnlocked && wave >= UPGRADE_UNLOCK_WAVE - 1 && state == STATE_PLAYING) {
        upgradesUnlocked = true;
        state = STATE_TUTORIAL;
    }
}

static void HandleInput(void)
{
    for (int i = 0; i < TOWER_COUNT; i++) {
        if (!IsKeyPressed(KEY_ONE + i)) continue;
        if (waveActive) {
            ShowMessage("Só dá para plantar quando a onda acabar");
            Sfx(SFX_ERROR);
        } else if (!unlocked[i]) {
            ShowMessage(TextFormat("%s bloqueada - compre na Loja [L]", TOWER_INFO[i].name));
            Sfx(SFX_ERROR);
        } else {
            selBuild = (selBuild == i) ? -1 : i;
            selC = selR = -1;
        }
    }
    if (IsKeyPressed(KEY_L)) { OpenShop(); return; }

    if (IsKeyPressed(KEY_SPACE)) StartWave();
    if (IsKeyPressed(KEY_F))     gameSpeed = (gameSpeed == 1) ? 2 : 1;
    if (IsKeyPressed(KEY_D))     showDist = !showDist;
    if (IsKeyPressed(KEY_U))     UpgradeSelected();
    if (IsKeyPressed(KEY_S))     SellSelected();

    if (IsMouseButtonPressed(MOUSE_BUTTON_RIGHT)) { selBuild = -1; selC = selR = -1; }

    Vector2 m = GetMousePosition();
    if (IsMouseButtonPressed(MOUSE_BUTTON_LEFT) && m.y >= 0 && m.y < GRID_H && m.x >= 0 && m.x < SCREEN_W) {
        int c = (int)(m.x / TILE), r = (int)(m.y / TILE);
        if (towers[r][c].active) {
            selC = c; selR = r; selBuild = -1;
        } else if (selBuild >= 0) {
            const char *reason;
            if (money < TOWER_INFO[selBuild].cost) { ShowMessage("Dinheiro insuficiente"); Sfx(SFX_ERROR); }
            else if (!CanPlace(c, r, &reason))     { ShowMessage(reason); Sfx(SFX_ERROR); }
            else                                   PlaceTower(c, r, (TowerKind)selBuild);
        } else {
            selC = selR = -1;
        }
    }
}

/* ------------------------------------------------------------------ */
/* Desenho                                                            */
/* ------------------------------------------------------------------ */
/* ---------- Paleta e utilitarios de interface ---------- */
static const Color UI_BG     = { 26, 34, 28, 255 };
static const Color UI_PANEL  = { 38, 52, 40, 255 };
static const Color UI_BORDER = { 92, 128, 84, 255 };
static const Color UI_TEXT   = { 238, 240, 228, 255 };
static const Color UI_MUTED  = { 165, 180, 158, 255 };
static const Color UI_ACCENT = { 245, 200, 80, 255 };

/* Numero pseudo-aleatorio fixo por tile (decoracao sempre igual) */
static unsigned int Hash2(int a, int b)
{
    /* conta feita em unsigned: estourar o limite e permitido e da a volta */
    unsigned int h = ((unsigned int)a * 73856093u) ^ ((unsigned int)b * 19349663u);
    h ^= h >> 13; h *= 0x5bd1e995u; h ^= h >> 15;
    return h;
}

/* ---------- Fontes ----------
   Usa a Fredoka (fonts/Fredoka.ttf, licenca SIL Open Font License), arredondada
   e divertida, combinando com o tema de jardim. Ordem de tentativa:
     1. fonts/Fredoka.ttf (na pasta atual ou na pasta do .exe)
     2. Segoe UI do Windows
     3. fonte padrao do raylib (se nenhuma das anteriores existir) */
#define FONTE_ARQUIVO     "fonts/Fredoka.ttf"
#define FONTE_BASE        64    /* tamanho em que a fonte e gerada (reduzida com mipmaps) */
#define FONTE_TITULO_MIN  24    /* a partir deste tamanho usa a versao em negrito */

static Font fonteTexto, fonteTitulo;
static bool fontesCarregadas = false;

static Font CarregarFonte(const char *arquivo)
{
    int codepoints[224];                       /* ASCII + Latin-1 (acentos do portugues) */
    for (int i = 0; i < 224; i++) codepoints[i] = 32 + i;
    Font f = LoadFontEx(arquivo, FONTE_BASE, codepoints, 224);
    GenTextureMipmaps(&f.texture);             /* texto pequeno fica nitido, sem serrilhado */
    SetTextureFilter(f.texture, TEXTURE_FILTER_TRILINEAR);
    return f;
}

static void CarregarFontes(void)
{
    /* 1. Fredoka, que vem junto com o projeto */
    const char *fredoka[2] = { FONTE_ARQUIVO, TextFormat("%s%s", GetApplicationDirectory(), FONTE_ARQUIVO) };
    for (int i = 0; i < 2; i++) {
        if (!FileExists(fredoka[i])) continue;
        fonteTexto  = CarregarFonte(fredoka[i]);
        fonteTitulo = CarregarFonte(fredoka[i]);
        fontesCarregadas = IsFontValid(fonteTexto) && IsFontValid(fonteTitulo);
        if (fontesCarregadas) return;
    }

    /* 2. Segoe UI do Windows */
    const char *texto  = "C:/Windows/Fonts/seguisb.ttf";   /* Segoe UI Semibold */
    const char *titulo = "C:/Windows/Fonts/segoeuib.ttf";  /* Segoe UI Bold */
    if (!FileExists(texto) || !FileExists(titulo)) return;
    fonteTexto  = CarregarFonte(texto);
    fonteTitulo = CarregarFonte(titulo);
    fontesCarregadas = IsFontValid(fonteTexto) && IsFontValid(fonteTitulo);
}

static void DescarregarFontes(void)
{
    if (!fontesCarregadas) return;
    UnloadFont(fonteTexto);
    UnloadFont(fonteTitulo);
    fontesCarregadas = false;
}

static Font FonteDoTamanho(int fs) { return fs >= FONTE_TITULO_MIN ? fonteTitulo : fonteTexto; }
static float EspacoDoTamanho(int fs) { return fs * 0.03f; }
/* Fontes TTF tem letras mais baixas que a fonte padrao: texto pequeno ganha 10% */
static float TamanhoReal(int fs) { return fs < FONTE_TITULO_MIN ? fs * 1.1f : (float)fs; }

/* O raylib so le o peso mais fino da Fredoka (arquivo de peso variavel).
   Para ela ficar mais "gordinha", o texto e desenhado algumas vezes com um
   deslocamento minimo para o lado (negrito simulado): mais forte nos titulos. */
static float NegritoDoTamanho(int fs) { return fs >= FONTE_TITULO_MIN ? fs * 0.05f : fs * 0.045f; }

/* Substitutos de DrawText/MeasureText com os mesmos parametros */
static void DesenharTexto(const char *t, int x, int y, int fs, Color c)
{
    if (!fontesCarregadas) { DrawText(t, x, y, fs, c); return; }
    float off = NegritoDoTamanho(fs);
    int passes = (int)ceilf(off);
    for (int i = 0; i <= passes; i++)
        DrawTextEx(FonteDoTamanho(fs), t, (Vector2){ x + off * i / passes, (float)y }, TamanhoReal(fs), EspacoDoTamanho(fs), c);
}

static int MedirTexto(const char *t, int fs)
{
    if (!fontesCarregadas) return MeasureText(t, fs);
    return (int)(MeasureTextEx(FonteDoTamanho(fs), t, TamanhoReal(fs), EspacoDoTamanho(fs)).x + NegritoDoTamanho(fs));
}

static void DrawPanel(Rectangle r, Color fill, Color border)
{
    DrawRectangleRounded((Rectangle){ r.x + 3, r.y + 5, r.width, r.height }, 0.08f, 8, Fade(BLACK, 0.35f));
    DrawRectangleRounded(r, 0.08f, 8, fill);
    DrawRectangleRoundedLinesEx(r, 0.08f, 8, 2, border);
}

static void DrawTextShadow(const char *t, int x, int y, int fs, Color c)
{
    DesenharTexto(t, x + 2, y + 2, fs, Fade(BLACK, 0.5f * c.a / 255.0f));
    DesenharTexto(t, x, y, fs, c);
}

static void DrawCentered(const char *text, int y, int fs, Color col)
{
    DrawTextShadow(text, SCREEN_W / 2 - MedirTexto(text, fs) / 2, y, fs, col);
}

/* ---------- Icones ---------- */
static void DrawCoin(Vector2 p, float r)
{
    DrawCircleV(p, r, (Color){ 200, 145, 25, 255 });
    DrawCircleV(p, r * 0.78f, (Color){ 250, 205, 70, 255 });
    DrawCircleV((Vector2){ p.x - r * 0.3f, p.y - r * 0.3f }, r * 0.22f, Fade(WHITE, 0.7f));
}

static void DrawSeedIcon(Vector2 p, float r)
{
    DrawEllipse((int)p.x, (int)p.y, r * 0.75f, r, (Color){ 140, 98, 50, 255 });
    DrawEllipse((int)p.x, (int)p.y, r * 0.52f, r * 0.8f, (Color){ 214, 172, 112, 255 });
    DrawEllipse((int)(p.x + r * 0.45f), (int)(p.y - r * 0.95f), r * 0.45f, r * 0.22f, (Color){ 100, 190, 70, 255 });
}

static void DrawHeart(Vector2 p, float r, Color c)
{
    DrawCircleV((Vector2){ p.x - r * 0.5f, p.y - r * 0.2f }, r * 0.56f, c);
    DrawCircleV((Vector2){ p.x + r * 0.5f, p.y - r * 0.2f }, r * 0.56f, c);
    DrawTriangle((Vector2){ p.x - r * 1.04f, p.y }, (Vector2){ p.x, p.y + r }, (Vector2){ p.x + r * 1.04f, p.y }, c);
}

static void DrawFlagIcon(Vector2 p, float r)
{
    DrawLineEx((Vector2){ p.x - r * 0.5f, p.y - r }, (Vector2){ p.x - r * 0.5f, p.y + r }, 2, (Color){ 210, 210, 210, 255 });
    DrawTriangle((Vector2){ p.x - r * 0.5f, p.y - r }, (Vector2){ p.x - r * 0.5f, p.y },
                 (Vector2){ p.x + r * 0.85f, p.y - r * 0.5f }, (Color){ 230, 80, 70, 255 });
}

static void DrawLockIcon(Vector2 p, float s, Color c)
{
    DrawRectangleRounded((Rectangle){ p.x - 6 * s, p.y - 1 * s, 12 * s, 9 * s }, 0.3f, 4, c);
    DrawRing((Vector2){ p.x, p.y - 1 * s }, 3 * s, 5 * s, 180, 360, 12, c);
}

/* "Tecla" desenhada como uma pecinha; devolve a largura */
static int DrawKeyChip(const char *key, int x, int y)
{
    int w = MedirTexto(key, 14) + 14;
    DrawRectangleRounded((Rectangle){ (float)x, (float)y, (float)w, 22 }, 0.35f, 6, (Color){ 58, 66, 60, 255 });
    DrawRectangleRounded((Rectangle){ (float)x, (float)y, (float)w, 19 }, 0.35f, 6, (Color){ 96, 110, 98, 255 });
    DesenharTexto(key, x + 7, y + 3, 14, UI_TEXT);
    return w;
}

/* ---------- Botao ---------- */
static bool DrawButton(Rectangle rec, const char *line1, const char *line2, bool enabled, bool highlighted)
{
    bool hover = CheckCollisionPointRec(GetMousePosition(), rec);
    bool down  = enabled && hover && IsMouseButtonDown(MOUSE_BUTTON_LEFT);
    Color fill = !enabled ? (Color){ 50, 56, 52, 255 }
               : highlighted ? (Color){ 72, 142, 70, 255 }
               : (Color){ 60, 88, 62, 255 };
    if (enabled && hover) fill = ColorBrightness(fill, 0.18f);

    Rectangle r = rec;
    if (down) r.y += 2;
    DrawRectangleRounded((Rectangle){ rec.x, rec.y + 3, rec.width, rec.height }, 0.3f, 8, Fade(BLACK, 0.35f));
    DrawRectangleRounded(r, 0.3f, 8, fill);
    DrawRectangleRounded((Rectangle){ r.x + 3, r.y + 2, r.width - 6, r.height * 0.42f }, 0.5f, 8, Fade(WHITE, enabled ? 0.08f : 0.03f));
    DrawRectangleRoundedLinesEx(r, 0.3f, 8, 2, highlighted ? UI_ACCENT
                                              : enabled ? (Color){ 118, 160, 108, 255 } : (Color){ 76, 84, 78, 255 });

    Color fg = enabled ? UI_TEXT : (Color){ 128, 136, 128, 255 };
    int fs = 16;
    if (line1 && line1[0]) {
        if (line2) {
            DesenharTexto(line1, (int)(r.x + (r.width - MedirTexto(line1, fs)) / 2), (int)(r.y + r.height / 2 - fs - 2), fs, fg);
            DesenharTexto(line2, (int)(r.x + (r.width - MedirTexto(line2, fs)) / 2), (int)(r.y + r.height / 2 + 2), fs, enabled ? UI_ACCENT : fg);
        } else {
            DesenharTexto(line1, (int)(r.x + (r.width - MedirTexto(line1, fs)) / 2), (int)(r.y + (r.height - fs) / 2), fs, fg);
        }
    }
    bool clicked = enabled && hover && IsMouseButtonPressed(MOUSE_BUTTON_LEFT);
    if (clicked) Sfx(SFX_CLICK);
    return clicked;
}

/* ---------- Mapa ---------- */
static bool IsRoadAt(int c, int r) { return InBounds(c, r) && grid[r][c] == TILE_ROAD; }

/* Ruido suave (value noise): numeros aleatorios fixos nos cantos de uma grade,
   interpolados suavemente. Usado para manchas naturais na grama. */
static float ValueNoise(float x, float y)
{
    int xi = (int)floorf(x), yi = (int)floorf(y);
    float fx = x - xi, fy = y - yi;
    float a = (Hash2(xi, yi) % 1000) / 999.0f,     b = (Hash2(xi + 1, yi) % 1000) / 999.0f;
    float c = (Hash2(xi, yi + 1) % 1000) / 999.0f, d = (Hash2(xi + 1, yi + 1) % 1000) / 999.0f;
    float ux = fx * fx * (3 - 2 * fx), uy = fy * fy * (3 - 2 * fy);
    return Lerp(Lerp(a, b, ux), Lerp(c, d, ux), uy);
}

static void DrawGrassTile(int c, int r)
{
    const LevelInfo *lv = &LEVELS[currentLevel];
    Color dark  = ColorBrightness(lv->grassB, -0.07f);
    Color light = ColorBrightness(lv->grassA,  0.04f);
    float seed = currentLevel * 7.3f + 3.1f;
    const int SUB = 4, SZ = TILE / SUB;          /* 4x4 pedacinhos de 12 px: transicao suave */
    for (int sy = 0; sy < SUB; sy++) {
        for (int sx = 0; sx < SUB; sx++) {
            float gx = c * SUB + sx + 0.5f, gy = r * SUB + sy + 0.5f;
            float n = 0.75f * ValueNoise(gx / 10.0f + seed, gy / 10.0f + seed)
                    + 0.25f * ValueNoise(gx / 3.0f + 50, gy / 3.0f + 50);
            Color g = ColorLerp(dark, light, n);
            DrawRectangle(c * TILE + sx * SZ, r * TILE + sy * SZ, SZ, SZ, g);
        }
    }
}

static void DrawGrassDecor(int c, int r)
{
    unsigned int h = Hash2(c + 101, r + 37);
    int x = c * TILE, y = r * TILE;
    float tm = (float)GetTime();
    Color blade = ColorBrightness(LEVELS[currentLevel].grassB, -0.32f);
    for (int i = 0; i < 2; i++) {     /* tufos de capim balancando com o vento */
        float tx = x + 8.0f + (h >> (i * 5)) % 30, ty = y + 14.0f + (h >> (i * 7 + 3)) % 26;
        float sw = sinf(tm * 2.2f + tx * 0.15f + ty * 0.1f) * 1.6f;
        DrawLineV((Vector2){ tx, ty }, (Vector2){ tx - 3 + sw, ty - 6 }, blade);
        DrawLineV((Vector2){ tx, ty }, (Vector2){ tx + sw, ty - 8 }, blade);
        DrawLineV((Vector2){ tx, ty }, (Vector2){ tx + 3 + sw, ty - 6 }, blade);
    }
    if (h % 6 == 0) {                 /* florzinha */
        int fx = x + 10 + (int)((h >> 9) % 28), fy = y + 10 + (int)((h >> 13) % 28);
        Color pet = (h % 3 == 0) ? (Color){ 250, 250, 250, 255 }
                  : (h % 3 == 1) ? (Color){ 255, 214, 90, 255 } : (Color){ 240, 140, 190, 255 };
        DrawCircle(fx + 1, fy + 2, 4.5f, Fade(BLACK, 0.10f));
        DrawCircle(fx + 3, fy, 2.5f, pet); DrawCircle(fx - 3, fy, 2.5f, pet);
        DrawCircle(fx, fy + 3, 2.5f, pet); DrawCircle(fx, fy - 3, 2.5f, pet);
        DrawCircle(fx, fy, 1.8f, (Color){ 230, 160, 40, 255 });
    }
}

/* Desenha a estrada inteira como uma faixa continua: um circulo em cada tile e
   um retangulo ligando tiles vizinhos. Assim as curvas ficam arredondadas. */
static void DrawRoadShape(float offY, float half, Color col)
{
    for (int r = 0; r < ROWS; r++) {
        for (int c = 0; c < COLS; c++) {
            if (!IsRoadAt(c, r)) continue;
            Vector2 p = TileCenter(c, r);
            p.y += offY;
            DrawCircleV(p, half, col);
            if (IsRoadAt(c + 1, r)) DrawRectangleRec((Rectangle){ p.x, p.y - half, (float)TILE, half * 2 }, col);
            if (IsRoadAt(c, r + 1)) DrawRectangleRec((Rectangle){ p.x - half, p.y, half * 2, (float)TILE }, col);
            /* entradas e casa na borda do mapa: a estrada vai ate a borda */
            if (c == 0)        DrawRectangleRec((Rectangle){ 0, p.y - half, p.x, half * 2 }, col);
            if (c == COLS - 1) DrawRectangleRec((Rectangle){ p.x, p.y - half, TILE / 2.0f, half * 2 }, col);
            if (r == 0)        DrawRectangleRec((Rectangle){ p.x - half, 0, half * 2, p.y }, col);
            if (r == ROWS - 1) DrawRectangleRec((Rectangle){ p.x - half, p.y, half * 2, TILE / 2.0f }, col);
        }
    }
}

static void DrawRoads(void)
{
    DrawRoadShape(3, 23, Fade(BLACK, 0.16f));               /* sombra */
    DrawRoadShape(0, 22, (Color){ 128, 96, 60, 255 });      /* borda */
    DrawRoadShape(0, 19, (Color){ 192, 156, 108, 255 });    /* terra */
    DrawRoadShape(0, 9,  (Color){ 204, 170, 122, 255 });    /* faixa gasta no meio */

    for (int r = 0; r < ROWS; r++) {
        for (int c = 0; c < COLS; c++) {
            if (!IsRoadAt(c, r)) continue;
            Vector2 p = TileCenter(c, r);
            unsigned int h = Hash2(c * 7 + 3, r * 11 + 5);
            for (int i = 0; i < 3; i++) {   /* pedrinhas */
                float px = p.x - 13 + (h >> (i * 6)) % 26, py = p.y - 13 + (h >> (i * 6 + 3)) % 26;
                float rad = 1.4f + (float)((h >> (i + 20)) % 2);
                DrawCircleV((Vector2){ px + 0.8f, py + 0.8f }, rad, (Color){ 150, 118, 80, 255 });
                DrawCircleV((Vector2){ px, py }, rad, (Color){ 176, 144, 102, 255 });
            }
            if (branchRoad[r][c] && !SecondEntranceOpen()) {   /* estrada ainda fechada: coberta de mato */
                Color g = ColorBrightness(LEVELS[currentLevel].grassB, -0.05f);
                DrawRectangle(c * TILE, r * TILE, TILE, TILE, Fade(g, 0.9f));
                Color blade = ColorBrightness(LEVELS[currentLevel].grassB, -0.35f);
                float tm = (float)GetTime();
                for (int i = 0; i < 6; i++) {
                    float bx = c * TILE + 5.0f + i * 7.5f, by = r * TILE + 34.0f + (h >> i) % 10;
                    float sw = sinf(tm * 2 + bx * 0.2f) * 2;
                    DrawLineEx((Vector2){ bx, by }, (Vector2){ bx - 2 + sw, by - 14 }, 1.5f, blade);
                    DrawLineEx((Vector2){ bx + 2, by }, (Vector2){ bx + 4 + sw, by - 10 }, 1.5f, blade);
                }
            }
        }
    }
}

static void DrawRock(int c, int r)
{
    float x = (float)(c * TILE), y = (float)(r * TILE);
    DrawEllipse((int)x + 24, (int)y + 39, 19, 6, Fade(BLACK, 0.22f));
    DrawRectangleRounded((Rectangle){ x + 6, y + 10, 36, 28 }, 0.6f, 8, (Color){ 112, 114, 120, 255 });
    DrawRectangleRounded((Rectangle){ x + 10, y + 12, 22, 13 }, 0.7f, 8, (Color){ 152, 154, 160, 255 });
    DrawCircle((int)x + 31, (int)y + 30, 3, (Color){ 96, 98, 104, 255 });
}

static void DrawSign(Vector2 center, const char *text, Color board)
{
    int w = MedirTexto(text, 10) + 10;
    Rectangle r = { center.x - w / 2.0f, center.y - 7, (float)w, 14 };
    DrawRectangleRounded(r, 0.3f, 4, board);
    DrawRectangleRoundedLinesEx(r, 0.3f, 4, 1, Fade(BLACK, 0.4f));
    DesenharTexto(text, (int)r.x + 5, (int)r.y + 2, 10, RAYWHITE);
}

/* Toca de onde saem as pragas */
static void DrawBurrow(int c, int r, const char *label)
{
    Vector2 p = TileCenter(c, r);
    DrawEllipse((int)p.x, (int)p.y + 4, 20, 15, (Color){ 120, 86, 52, 255 });
    DrawEllipse((int)p.x, (int)p.y + 6, 15, 10, (Color){ 42, 28, 18, 255 });
    DrawEllipse((int)p.x, (int)p.y + 8, 9, 5, (Color){ 18, 10, 6, 255 });
    DrawSign((Vector2){ p.x, p.y - 15 }, label, (Color){ 130, 92, 52, 255 });
}

static void DrawClosedGate(int c, int r)
{
    int x = c * TILE, y = r * TILE;
    Color wood = { 156, 112, 66, 255 }, dark = { 104, 72, 40, 255 };
    for (int i = 0; i < 4; i++) {
        DrawRectangle(x + 5 + i * 10, y + 16, 7, 28, wood);
        DrawCircle(x + 8 + i * 10, y + 16, 3.5f, wood);
    }
    DrawRectangle(x + 3, y + 22, 42, 5, dark);
    DrawRectangle(x + 3, y + 36, 42, 5, dark);
    DrawSign((Vector2){ x + TILE / 2.0f, y + 7.0f }, TextFormat("Onda %d", SECOND_ENTRANCE_WAVE), (Color){ 170, 40, 40, 255 });
}

/* Casinha do jardim (a base) */
static void DrawHouse(int c, int r)
{
    float x = (float)(c * TILE), y = (float)(r * TILE);
    bool hit = baseFlash > 0 && ((int)(baseFlash * 20) % 2 == 0);
    Color wall = hit ? (Color){ 255, 120, 110, 255 } : (Color){ 242, 228, 198, 255 };
    DrawEllipse((int)x + 24, (int)y + 45, 22, 5, Fade(BLACK, 0.25f));
    DrawRectangle((int)x + 8, (int)y + 20, 32, 25, wall);
    DrawRectangle((int)x + 32, (int)y + 5, 7, 13, (Color){ 150, 70, 56, 255 });
    DrawRectangle((int)x + 31, (int)y + 4, 9, 3, (Color){ 120, 56, 46, 255 });
    DrawTriangle((Vector2){ x + 24, y + 3 }, (Vector2){ x + 2, y + 22 }, (Vector2){ x + 46, y + 22 }, (Color){ 196, 64, 52, 255 });
    DrawRectangle((int)x + 20, (int)y + 31, 9, 14, (Color){ 120, 78, 44, 255 });
    DrawCircle((int)x + 27, (int)y + 38, 1.2f, UI_ACCENT);
    DrawRectangle((int)x + 11, (int)y + 25, 7, 7, (Color){ 140, 200, 240, 255 });
    DrawRectangle((int)x + 31, (int)y + 25, 7, 7, (Color){ 140, 200, 240, 255 });
}

static void DrawMap(void)
{
    for (int r = 0; r < ROWS; r++)
        for (int c = 0; c < COLS; c++)
            DrawGrassTile(c, r);

    for (int r = 0; r < ROWS; r++)
        for (int c = 0; c < COLS; c++)
            if (grid[r][c] == TILE_EMPTY) DrawGrassDecor(c, r);

    DrawRoads();

    for (int r = 0; r < ROWS; r++)
        for (int c = 0; c < COLS; c++)
            if (grid[r][c] == TILE_ROCK) DrawRock(c, r);

    /* Caminho atual: pontinhos andando da entrada ate a base (segue a BFS) */
    float phase = fmodf((float)GetTime() * 1.2f, 1.0f);
    for (int s = 0; s < 2; s++) {
        int c = s == 0 ? spawnC : spawn2C, r = s == 0 ? spawnR : spawn2R;
        if (c < 0 || (s == 1 && !SecondEntranceOpen())) continue;
        for (int steps = 0; steps < ROWS * COLS && dist[r][c] > 0 && dist[r][c] < INF; steps++) {
            int bc = c, br = r;
            for (int i = 0; i < 4; i++) {
                int nc = c + DC[i], nr = r + DR[i];
                if (InBounds(nc, nr) && dist[nr][nc] < dist[br][bc]) { bc = nc; br = nr; }
            }
            Vector2 a = TileCenter(c, r), b = TileCenter(bc, br);
            for (int j = 0; j < 4; j++) {
                float u = (j + phase) / 4.0f;
                DrawCircleV(Vector2Lerp(a, b, u), 1.8f, Fade(WHITE, 0.28f));
            }
            c = bc; r = br;
        }
    }

    DrawBurrow(spawnC, spawnR, "ENTRADA");
    if (spawn2C >= 0) {
        if (SecondEntranceOpen()) DrawBurrow(spawn2C, spawn2R, "ENTRADA 2");
        else                      DrawClosedGate(spawn2C, spawn2R);
    }
    DrawHouse(baseC, baseR);

    if (showDist) {
        for (int rr = 0; rr < ROWS; rr++)
            for (int cc = 0; cc < COLS; cc++)
                if (dist[rr][cc] < INF)
                    DesenharTexto(TextFormat("%d", dist[rr][cc]), cc * TILE + 4, rr * TILE + 4, 10, Fade(BLACK, 0.7f));
    }
}

/* Sombras de nuvens passando devagar sobre o mapa */
static void DrawClouds(void)
{
    float tm = (float)GetTime();
    for (int i = 0; i < 4; i++) {
        float span = SCREEN_W + 520.0f;
        float x = fmodf(tm * (9 + i * 3) + i * 340, span) - 260;
        float y = 70 + i * 135 + sinf(tm * 0.1f + i) * 18;
        for (int k = 0; k < 3; k++) {
            float bx = x + (k - 1) * 62, by = y + (k == 1 ? -14 : 0);
            DrawEllipse((int)bx, (int)by, 78, 36, Fade(BLACK, 0.035f));
            DrawEllipse((int)bx, (int)by, 56, 26, Fade(BLACK, 0.03f));
        }
    }
}

/* Desenha uma planta centrada em p.
   s     = escala (1.0 = tamanho de um tile)
   angle = direcao do alvo (a planta "olha" / gira para ele)
   dim   = desenhar apagada (planta bloqueada na loja) */
static void DrawPlant(TowerKind kind, Vector2 p, float s, float angle, bool dim)
{
    float a = dim ? 0.35f : 1.0f;
    Color col  = Fade(TOWER_INFO[kind].color, a);
    Color leaf = Fade((Color){ 60, 160, 60, 255 }, a);
    Color dark = Fade((Color){ 30, 90, 35, 255 }, a);
    Color soil = Fade((Color){ 95, 62, 35, 255 }, a);

    /* montinho de terra */
    DrawEllipse((int)p.x, (int)(p.y + 13 * s), 16 * s, 6 * s, soil);

    switch (kind) {
        case TOWER_BASIC: {   /* Brotinho: duas folhas e uma cabeca que cospe sementes */
            Vector2 head = { p.x, p.y - 3 * s };
            DrawLineEx((Vector2){ p.x, p.y + 12 * s }, head, 3 * s, dark);
            DrawEllipse((int)(p.x - 8 * s), (int)(p.y + 8 * s), 7 * s, 3.5f * s, leaf);
            DrawEllipse((int)(p.x + 8 * s), (int)(p.y + 8 * s), 7 * s, 3.5f * s, leaf);
            Vector2 tip = { head.x + cosf(angle) * 14 * s, head.y + sinf(angle) * 14 * s };
            DrawLineEx(head, tip, 7 * s, col);
            DrawCircleV(head, 9 * s, col);
            DrawCircleV(tip, 3.5f * s, dark);
            DrawCircleV((Vector2){ head.x - cosf(angle) * 3 * s, head.y - 4 * s }, 2 * s, Fade(BLACK, a));
        } break;

        case TOWER_SNIPER: {  /* Cacto: corpo com bracos, espinhos e uma flor */
            DrawRectangleRounded((Rectangle){ p.x - 5 * s, p.y - 16 * s, 10 * s, 29 * s }, 1.0f, 8, col);
            DrawRectangleRounded((Rectangle){ p.x - 13 * s, p.y - 7 * s, 6 * s, 13 * s }, 1.0f, 8, col);
            DrawRectangleRounded((Rectangle){ p.x - 13 * s, p.y + 1 * s, 11 * s, 5 * s }, 1.0f, 8, col);
            DrawRectangleRounded((Rectangle){ p.x + 7 * s, p.y - 11 * s, 6 * s, 13 * s }, 1.0f, 8, col);
            DrawRectangleRounded((Rectangle){ p.x + 2 * s, p.y - 3 * s, 11 * s, 5 * s }, 1.0f, 8, col);
            for (int i = 0; i < 4; i++)
                DrawCircleV((Vector2){ p.x + ((i % 2) ? 2.0f : -2.0f) * s, p.y - 11 * s + i * 6 * s }, 1.2f * s, Fade(RAYWHITE, a));
            DrawCircleV((Vector2){ p.x, p.y - 17 * s }, 3.5f * s, Fade(PINK, a));
            /* espinho apontando para o alvo */
            DrawLineEx((Vector2){ p.x, p.y - 4 * s },
                       (Vector2){ p.x + cosf(angle) * 9 * s, p.y - 4 * s + sinf(angle) * 9 * s }, 1.5f * s, Fade(RAYWHITE, a));
        } break;

        case TOWER_FROST: {   /* Menta: roseta de folhas com cristais de gelo */
            Vector2 ctr = { p.x, p.y - 2 * s };
            for (int i = 0; i < 5; i++) {
                float an = angle + i * 2 * PI / 5;
                DrawCircleV((Vector2){ ctr.x + cosf(an) * 8 * s, ctr.y + sinf(an) * 8 * s }, 6.5f * s, col);
                DrawCircleV((Vector2){ ctr.x + cosf(an) * 11 * s, ctr.y + sinf(an) * 11 * s }, 1.5f * s, Fade(WHITE, a));
            }
            DrawCircleV(ctr, 5 * s, Fade((Color){ 215, 255, 250, 255 }, a));
        } break;

        case TOWER_CANNON: {  /* Abobora: gomos, cabinho e uma boca virada para o alvo */
            Color ribs = Fade((Color){ 200, 95, 0, 255 }, a);
            DrawEllipse((int)p.x, (int)p.y, 15 * s, 11 * s, col);
            DrawEllipseLines((int)p.x, (int)p.y, 9 * s, 11 * s, ribs);
            DrawEllipseLines((int)p.x, (int)p.y, 3 * s, 11 * s, ribs);
            DrawRectangle((int)(p.x - 2 * s), (int)(p.y - 15 * s), (int)(4 * s) + 1, (int)(6 * s), dark);
            DrawEllipse((int)(p.x + 6 * s), (int)(p.y - 13 * s), 5 * s, 2.5f * s, leaf);
            DrawCircleV((Vector2){ p.x + cosf(angle) * 8 * s, p.y + sinf(angle) * 6 * s }, 3 * s,
                        Fade((Color){ 90, 40, 0, 255 }, a));
        } break;

        case TOWER_POISON: {  /* Urtiga: folhas pontudas em volta de um nucleo venenoso */
            Vector2 ctr = { p.x, p.y - 2 * s };
            for (int i = 0; i < 7; i++) {
                float an = angle + i * 2 * PI / 7;
                Vector2 q = { ctr.x + cosf(an) * 15 * s, ctr.y + sinf(an) * 15 * s };
                DrawLineEx(ctr, q, 4 * s, leaf);
                DrawCircleV(q, 1.6f * s, Fade(LIME, a));
            }
            DrawCircleV(ctr, 7 * s, col);
            DrawCircleV((Vector2){ ctr.x - 2 * s, ctr.y - 2 * s }, 2 * s, Fade(LIME, a));
        } break;

        case TOWER_LASER: {   /* Girassol: petalas que giram para o alvo */
            Vector2 ctr = { p.x, p.y - 3 * s };
            DrawLineEx((Vector2){ p.x, p.y + 12 * s }, ctr, 3 * s, dark);
            for (int i = 0; i < 10; i++) {
                float an = angle + i * 2 * PI / 10;
                DrawCircleV((Vector2){ ctr.x + cosf(an) * 10 * s, ctr.y + sinf(an) * 10 * s }, 5 * s, col);
            }
            DrawCircleV(ctr, 7 * s, Fade((Color){ 120, 70, 20, 255 }, a));
            DrawCircleV((Vector2){ ctr.x - 2 * s, ctr.y - 2 * s }, 1.5f * s, Fade((Color){ 70, 40, 10, 255 }, a));
            DrawCircleV((Vector2){ ctr.x + 2 * s, ctr.y + 1 * s }, 1.5f * s, Fade((Color){ 70, 40, 10, 255 }, a));
        } break;

        default: break;
    }
}

static void DrawLevelPips(float x, float y, int level)
{
    for (int i = 0; i < level; i++) {
        DrawCircle((int)(x + i * 9), (int)y, 3.6f, Fade(BLACK, 0.5f));
        DrawCircle((int)(x + i * 9), (int)y, 2.8f, UI_ACCENT);
    }
}

static void DrawTowers(void)
{
    float tm = (float)GetTime();

    if (selC >= 0) {   /* alcance da planta selecionada: circulo tracejado girando */
        Tower *t = &towers[selR][selC];
        Vector2 ctr = TileCenter(selC, selR);
        float R = TowerRange(t), rot = tm * 18;
        DrawCircleV(ctr, R, Fade(WHITE, 0.09f));
        for (int k = 0; k < 36; k++) {
            float a0 = rot + k * 10;
            DrawRing(ctr, R - 1.5f, R + 1.0f, a0, a0 + 6, 3, Fade(WHITE, 0.85f));
        }
    }

    for (int r = 0; r < ROWS; r++) {
        for (int c = 0; c < COLS; c++) {
            Tower *t = &towers[r][c];
            if (!t->active) continue;
            Vector2 p = TileCenter(c, r);
            unsigned int h = Hash2(c * 13, r * 7);
            float ph = (h % 628) / 100.0f;
            Vector2 dir = { cosf(t->angle), sinf(t->angle) };
            float rc = t->recoil;

            /* aura de nivel: prateada no nivel 2, dourada pulsando no nivel 3 */
            if (t->level >= 2) {
                Color aura = t->level == 3 ? (Color){ 255, 210, 80, 255 } : (Color){ 200, 230, 255, 255 };
                float pulse = 0.5f + 0.5f * sinf(tm * 3 + ph);
                DrawCircleGradient((Vector2){ p.x, p.y + 4 }, 22 + 3 * pulse, Fade(aura, t->level == 3 ? 0.40f : 0.20f), Fade(aura, 0.0f));
            }
            DrawEllipse((int)p.x, (int)p.y + 15, 16, 5, Fade(BLACK, 0.18f));

            /* balanco suave + tranco para tras ao atirar; planta cresce com o nivel */
            float bob = sinf(tm * 2.0f + ph) * 1.2f;
            Vector2 pp = { p.x - dir.x * 3 * rc, p.y + bob - dir.y * 3 * rc };
            float s = 1.0f + 0.07f * rc + (t->level - 1) * 0.06f;
            DrawPlant(t->kind, pp, s, t->angle, false);

            if (rc > 0.55f) {   /* brilho do disparo */
                Vector2 m = { pp.x + dir.x * 17 * s, pp.y - 3 + dir.y * 17 * s };
                DrawCircleV(m, 9 * rc, Fade(TOWER_INFO[t->kind].color, 0.35f * rc));
                DrawCircleV(m, 4.5f * rc, Fade(WHITE, 0.85f * rc));
            }
            if (t->level == 3 && (int)(tm * 3 + ph) % 4 == 0) {   /* brilhinho do nivel maximo */
                float u = fmodf(tm * 3 + ph, 1.0f);
                DrawCircleV((Vector2){ p.x + 12 * cosf(ph * 3), p.y - 12 - u * 8 }, 2 * (1 - u), Fade((Color){ 255, 240, 160, 255 }, 1 - u));
            }
            DrawLevelPips(c * TILE + 12.0f, r * TILE + TILE - 7.0f, t->level);
        }
    }

    if (selC >= 0) {
        Rectangle tr = { selC * (float)TILE + 1, selR * (float)TILE + 1, TILE - 2.0f, TILE - 2.0f };
        DrawRectangleRoundedLinesEx(tr, 0.25f, 6, 2, UI_ACCENT);
    }
}

static void DrawBuildPreview(void)
{
    if (selBuild < 0 || state != STATE_PLAYING) return;
    Vector2 m = GetMousePosition();
    if (m.y < 0 || m.y >= GRID_H || m.x < 0 || m.x >= SCREEN_W) return;
    int c = (int)(m.x / TILE), r = (int)(m.y / TILE);
    if (towers[r][c].active) return;

    const char *reason;
    bool ok = money >= TOWER_INFO[selBuild].cost && CanPlace(c, r, &reason);
    Vector2 ctr = TileCenter(c, r);
    Rectangle tr = { c * (float)TILE + 2, r * (float)TILE + 2, TILE - 4.0f, TILE - 4.0f };
    DrawCircleV(ctr, TOWER_INFO[selBuild].range, Fade(ok ? WHITE : RED, 0.08f));
    DrawCircleLinesV(ctr, TOWER_INFO[selBuild].range, Fade(WHITE, 0.7f));
    DrawRectangleRounded(tr, 0.25f, 6, Fade(ok ? GREEN : RED, 0.30f));
    DrawRectangleRoundedLinesEx(tr, 0.25f, 6, 2, ok ? GREEN : RED);
    if (ok) DrawPlant((TowerKind)selBuild, ctr, 1.0f, 0, true);
}

/* ---------- Pragas do jardim (inimigos) ---------- */
static void DrawEnemy(const Enemy *e)
{
    Vector2 p = e->pos;
    Vector2 to = Vector2Subtract(TileCenter(e->tc, e->tr), p);
    float ang = (Vector2Length(to) > 0.1f) ? atan2f(to.y, to.x) : 0.0f;
    Vector2 f = { cosf(ang), sinf(ang) }, sd = { -f.y, f.x };
    float t = (float)GetTime() * (e->kind == ENEMY_FAST ? 18.0f : 10.0f) * e->slowFactor;
    float R = e->radius;
#define AT(a, b) ((Vector2){ p.x + f.x * (a) + sd.x * (b), p.y + f.y * (a) + sd.y * (b) })

    DrawEllipse((int)p.x, (int)(p.y + R * 0.7f), R * 1.1f, R * 0.4f, Fade(BLACK, 0.2f));

    switch (e->kind) {
        case ENEMY_NORMAL: {   /* besouro vermelho */
            Color shell = { 205, 45, 40, 255 }, dark = { 30, 20, 20, 255 };
            for (int k = -1; k <= 1; k++) {
                float sw = sinf(t + k * 1.3f) * 2.5f;
                DrawLineEx(AT(k * R * 0.5f, 0), AT(k * R * 0.5f + sw,  R * 1.25f), 2, dark);
                DrawLineEx(AT(k * R * 0.5f, 0), AT(k * R * 0.5f - sw, -R * 1.25f), 2, dark);
            }
            DrawCircleV(AT(R * 0.85f, 0), R * 0.5f, dark);
            DrawCircleV(p, R, shell);
            DrawLineEx(AT(R * 0.6f, 0), AT(-R, 0), 1.5f, dark);
            DrawCircleV(AT(0.15f * R,  0.5f * R), R * 0.2f, dark);
            DrawCircleV(AT(0.15f * R, -0.5f * R), R * 0.2f, dark);
            DrawCircleV(AT(-0.5f * R,  0.4f * R), R * 0.17f, dark);
            DrawCircleV(AT(-0.5f * R, -0.4f * R), R * 0.17f, dark);
            DrawCircleV(AT(R * 0.35f, -R * 0.35f), R * 0.22f, Fade(WHITE, 0.3f));
            DrawCircleV(AT(R * 1.1f,  R * 0.22f), 1.6f, WHITE);
            DrawCircleV(AT(R * 1.1f, -R * 0.22f), 1.6f, WHITE);
        } break;

        case ENEMY_FAST: {     /* formiga */
            Color body = { 214, 120, 40, 255 }, dark = { 90, 45, 15, 255 };
            for (int k = -1; k <= 1; k++) {
                float sw = sinf(t + k * 1.5f) * 2.5f;
                DrawLineEx(p, AT(k * R * 0.6f + sw,  R * 1.4f), 1.5f, dark);
                DrawLineEx(p, AT(k * R * 0.6f - sw, -R * 1.4f), 1.5f, dark);
            }
            DrawLineEx(AT(R * 1.2f, 0), AT(R * 2.0f,  R * 0.7f), 1.2f, dark);
            DrawLineEx(AT(R * 1.2f, 0), AT(R * 2.0f, -R * 0.7f), 1.2f, dark);
            DrawCircleV(AT(-R * 0.95f, 0), R * 0.75f, body);
            DrawCircleV(p, R * 0.5f, body);
            DrawCircleV(AT(R * 0.95f, 0), R * 0.55f, body);
            DrawCircleV(AT(-R * 1.1f, -R * 0.25f), R * 0.25f, Fade(WHITE, 0.3f));
            DrawCircleV(AT(R * 1.2f,  R * 0.25f), 1.3f, dark);
            DrawCircleV(AT(R * 1.2f, -R * 0.25f), 1.3f, dark);
        } break;

        case ENEMY_TANK: {     /* caracol gigante (chefe) */
            Color skin = { 200, 190, 150, 255 }, shellC = { 150, 70, 50, 255 }, shellL = { 200, 116, 72, 255 };
            float wob = sinf(t * 0.5f) * 1.5f;
            DrawCircleV(AT(-R * 0.3f, 0), R * 0.6f, skin);
            DrawCircleV(AT(R * 0.5f + wob, 0), R * 0.55f, skin);
            DrawCircleV(AT(R * 1.0f + wob, 0), R * 0.45f, skin);
            Vector2 e1 = AT(R * 1.7f + wob,  R * 0.45f), e2 = AT(R * 1.7f + wob, -R * 0.45f);
            DrawLineEx(AT(R * 1.15f + wob,  R * 0.2f), e1, 2, skin);
            DrawLineEx(AT(R * 1.15f + wob, -R * 0.2f), e2, 2, skin);
            DrawCircleV(e1, 2.5f, (Color){ 40, 30, 30, 255 });
            DrawCircleV(e2, 2.5f, (Color){ 40, 30, 30, 255 });
            DrawCircleV(AT(-R * 0.15f, 0), R * 0.85f, shellC);
            DrawRing(AT(-R * 0.15f, 0), R * 0.45f, R * 0.62f, 0, 360, 24, shellL);
            DrawCircleV(AT(-R * 0.1f, 0), R * 0.24f, shellL);
            DrawCircleV(AT(-R * 0.4f, -R * 0.4f), R * 0.18f, Fade(WHITE, 0.25f));
        } break;
    }
#undef AT

    if (e->hitFlash > 0) DrawCircleV(p, R * 1.1f, Fade(WHITE, 0.75f * e->hitFlash / 0.1f));

    if (e->slowTimer > 0)
        DrawRing(p, R + 2, R + 4, 0, 360, 24, Fade(SKYBLUE, 0.75f));
    if (e->poisonTimer > 0)
        for (int k = 0; k < 3; k++)
            DrawCircleV((Vector2){ p.x - 5 + k * 5, p.y - R - 4 - fmodf((float)GetTime() * 2 + k * 0.7f, 2.0f) * 4 }, 2, Fade(LIME, 0.85f));

    float frac = e->hp / e->maxHp;
    if (frac < 1.0f || e->kind == ENEMY_TANK) {
        float w = e->kind == ENEMY_TANK ? 38 : 26;
        Rectangle bg = { p.x - w / 2, p.y - R - 12, w, 5 };
        Color fill = frac > 0.6f ? (Color){ 110, 210, 80, 255 } : frac > 0.3f ? (Color){ 240, 200, 60, 255 } : (Color){ 230, 70, 60, 255 };
        DrawRectangleRounded((Rectangle){ bg.x - 1, bg.y - 1, bg.width + 2, bg.height + 2 }, 1, 4, Fade(BLACK, 0.6f));
        DrawRectangleRounded((Rectangle){ bg.x, bg.y, bg.width * frac, bg.height }, 1, 4, fill);
    }
}

static void DrawEnemies(void)
{
    for (int i = 0; i < MAX_ENEMIES; i++)
        if (enemies[i].active) DrawEnemy(&enemies[i]);
}

/* Cada planta tem um tiro com desenho proprio */
static void DrawProjectile(const Projectile *pr)
{
    Vector2 p = pr->pos;
    Vector2 d = (Vector2Length(pr->dir) > 0.1f) ? pr->dir : (Vector2){ 1, 0 };
    Vector2 back = { -d.x, -d.y };

    switch (pr->kind) {
        case TOWER_BASIC: {    /* semente com rastro */
            for (int k = 3; k >= 1; k--)
                DrawCircleV(Vector2Add(p, Vector2Scale(back, k * 4.0f)), pr->size * (1 - k * 0.2f), Fade((Color){ 140, 220, 90, 255 }, 0.4f - k * 0.1f));
            DrawCircleV(p, pr->size + 0.5f, (Color){ 70, 140, 50, 255 });
            DrawCircleV(p, pr->size, (Color){ 130, 210, 80, 255 });
            DrawCircleV((Vector2){ p.x - 1.2f, p.y - 1.2f }, pr->size * 0.4f, Fade(WHITE, 0.7f));
        } break;

        case TOWER_SNIPER: {   /* espinho */
            Vector2 tail = Vector2Add(p, Vector2Scale(back, 13));
            DrawLineEx(tail, p, 3.0f, Fade(BLACK, 0.25f));
            DrawLineEx(tail, p, 2.2f, (Color){ 238, 232, 200, 255 });
            DrawCircleV(p, 1.5f, (Color){ 110, 80, 50, 255 });
        } break;

        case TOWER_FROST: {    /* floquinho de gelo girando */
            float rot = (float)GetTime() * 8;
            DrawCircleV(p, 7, Fade(SKYBLUE, 0.25f));
            for (int k = 0; k < 3; k++) {
                float an = rot + k * PI / 3;
                Vector2 o = { cosf(an) * 5, sinf(an) * 5 };
                DrawLineEx(Vector2Subtract(p, o), Vector2Add(p, o), 1.6f, WHITE);
            }
            DrawCircleV(p, 1.8f, (Color){ 200, 240, 255, 255 });
        } break;

        case TOWER_CANNON: {   /* mini abobora voando em arco */
            float u = Clamp(pr->done / pr->total, 0, 1);
            float hgt = sinf(PI * u) * 24;
            DrawEllipse((int)p.x, (int)p.y + 3, 5 + hgt * 0.05f, 2, Fade(BLACK, 0.25f));
            Vector2 q = { p.x, p.y - hgt };
            DrawCircleV(q, 6.5f, (Color){ 200, 100, 10, 255 });
            DrawCircleV(q, 5.5f, (Color){ 246, 140, 24, 255 });
            DrawLineEx((Vector2){ q.x, q.y - 5 }, (Vector2){ q.x + 1.5f, q.y - 9 }, 2, (Color){ 60, 120, 40, 255 });
            DrawCircleV((Vector2){ q.x - 2, q.y - 2 }, 1.8f, Fade(WHITE, 0.45f));
        } break;

        case TOWER_POISON: {   /* gota de veneno pingando */
            for (int k = 2; k >= 1; k--)
                DrawCircleV(Vector2Add(p, Vector2Scale(back, k * 5.0f)), 2.2f - k * 0.5f, Fade((Color){ 170, 90, 200, 255 }, 0.5f - k * 0.15f));
            DrawCircleV(p, pr->size + 1.2f, (Color){ 120, 40, 150, 255 });
            DrawCircleV(p, pr->size, (Color){ 160, 70, 190, 255 });
            DrawCircleV((Vector2){ p.x - 1, p.y - 1 }, pr->size * 0.45f, (Color){ 190, 255, 120, 255 });
        } break;

        case TOWER_LASER: {    /* raio de sol */
            Vector2 tail = Vector2Add(p, Vector2Scale(back, 18));
            DrawLineEx(tail, p, 7, Fade((Color){ 255, 220, 60, 255 }, 0.25f));
            DrawLineEx(tail, p, 2.6f, (Color){ 255, 242, 160, 255 });
            DrawCircleV(p, 2.6f, WHITE);
        } break;

        default:
            DrawCircleV(p, pr->size, pr->color);
            break;
    }
}

static void DrawProjectiles(void)
{
    for (int i = 0; i < MAX_PROJ; i++)
        if (projs[i].active) DrawProjectile(&projs[i]);

    for (int i = 0; i < MAX_FX; i++)
        if (effects[i].active) {
            float a = effects[i].life / effects[i].maxLife;
            float rad = effects[i].radius * (1.2f - 0.4f * a);
            DrawCircleGradient(effects[i].pos, rad, Fade((Color){ 255, 220, 120, 255 }, 0.55f * a), Fade(effects[i].color, 0.15f * a));
            DrawRing(effects[i].pos, rad - 3, rad, 0, 360, 32, Fade(YELLOW, 0.6f * a));
        }

    for (int i = 0; i < MAX_PART; i++)
        if (parts[i].active) {
            const Particle *q = &parts[i];
            float a = q->life / q->maxLife;
            float rad = q->size * (q->grow + (1 - q->grow) * a);
            DrawCircleV(q->pos, rad, Fade(q->color, a * q->color.a / 255.0f));
        }

    for (int i = 0; i < MAX_FLOAT; i++)
        if (floats[i].active) {
            int w = MedirTexto(floats[i].text, 16);
            DrawTextShadow(floats[i].text, (int)floats[i].pos.x - w / 2, (int)floats[i].pos.y - 20, 16, Fade(floats[i].color, floats[i].life));
        }
}

/* ---------- Interface ---------- */
/* Escurece levemente as bordas do mapa; fica vermelho quando a casa esta em perigo */
static void DrawMapVignette(bool live)
{
    int e = 60;
    Color dark = Fade(BLACK, 0.22f);
    DrawRectangleGradientV(0, 0, SCREEN_W, e, dark, BLANK);
    DrawRectangleGradientV(0, GRID_H - e, SCREEN_W, e, BLANK, dark);
    DrawRectangleGradientH(0, 0, e, GRID_H, dark, BLANK);
    DrawRectangleGradientH(SCREEN_W - e, 0, e, GRID_H, BLANK, dark);

    float danger = 0;
    if (live && lives <= 5) danger = 0.22f + 0.12f * sinf((float)GetTime() * 5);
    if (baseFlash > 0) danger = fmaxf(danger, baseFlash / 0.4f * 0.55f);
    if (danger > 0) {
        int d = 80;
        Color red = Fade((Color){ 210, 20, 20, 255 }, danger);
        DrawRectangleGradientV(0, 0, SCREEN_W, d, red, BLANK);
        DrawRectangleGradientV(0, GRID_H - d, SCREEN_W, d, BLANK, red);
        DrawRectangleGradientH(0, 0, d, GRID_H, red, BLANK);
        DrawRectangleGradientH(SCREEN_W - d, 0, d, GRID_H, BLANK, red);
    }
}

/* Mini icones das pragas (para o contador e a previa da onda) */
static void DrawMiniBug(int kind, Vector2 c)
{
    Color dk = { 30, 20, 20, 255 };
    if (kind == ENEMY_FAST) {            /* formiga */
        Color body = { 214, 120, 40, 255 };
        DrawLineEx((Vector2){ c.x - 7, c.y + 5 }, (Vector2){ c.x + 7, c.y - 5 }, 1.2f, (Color){ 90, 45, 15, 255 });
        DrawLineEx((Vector2){ c.x - 7, c.y - 5 }, (Vector2){ c.x + 7, c.y + 5 }, 1.2f, (Color){ 90, 45, 15, 255 });
        DrawCircleV((Vector2){ c.x - 6, c.y }, 4.5f, body);
        DrawCircleV(c, 3, body);
        DrawCircleV((Vector2){ c.x + 6, c.y }, 3.5f, body);
    } else if (kind == ENEMY_TANK) {     /* caracol */
        DrawCircleV((Vector2){ c.x + 4, c.y + 3 }, 4.5f, (Color){ 200, 190, 150, 255 });
        DrawCircleV((Vector2){ c.x - 2, c.y }, 7, (Color){ 150, 70, 50, 255 });
        DrawRing((Vector2){ c.x - 2, c.y }, 3, 4.5f, 0, 360, 16, (Color){ 200, 116, 72, 255 });
    } else {                             /* joaninha vista de cima */
        DrawCircleV((Vector2){ c.x, c.y - 6 }, 3.6f, dk);
        DrawCircleV(c, 7, (Color){ 215, 50, 42, 255 });
        DrawLineEx((Vector2){ c.x, c.y - 5 }, (Vector2){ c.x, c.y + 7 }, 1.2f, dk);
        DrawCircleV((Vector2){ c.x - 3.2f, c.y - 1 }, 1.5f, dk);
        DrawCircleV((Vector2){ c.x + 3.2f, c.y - 1 }, 1.5f, dk);
        DrawCircleV((Vector2){ c.x - 2.8f, c.y + 3.5f }, 1.3f, dk);
        DrawCircleV((Vector2){ c.x + 2.8f, c.y + 3.5f }, 1.3f, dk);
    }
}

/* Antes de cada onda: quantas pragas de cada tipo vem (mesma regra do SpawnEnemy) */
static void DrawNextWavePreview(void)
{
    int n = wave + 1;
    if (n > MAX_WAVES) return;
    int size = 6 + n * 2, boss = (n % 5 == 0) ? 1 : 0, fast = 0;
    for (int i = 0; i < size; i++) {
        if (boss && i == size - 1) continue;
        if (n >= 3 && i % 3 == 2) fast++;
    }
    int counts[3] = { size - fast - boss, fast, boss };
    bool entrance = spawn2C >= 0 && n == SECOND_ENTRANCE_WAVE;

    const char *label = TextFormat("Próxima onda %d:", n);
    int lw = MedirTexto(label, 16);
    float w = 20 + lw + 12;
    for (int k = 0; k < 3; k++) if (counts[k] > 0) w += 22 + MedirTexto(TextFormat("%d", counts[k]), 16) + 12;
    float h = entrance || boss ? 54 : 32;
    Rectangle pill = { SCREEN_W - 10 - w, 10, w, h };
    DrawRectangleRounded((Rectangle){ pill.x + 2, pill.y + 3, pill.width, pill.height }, 0.4f, 8, Fade(BLACK, 0.3f));
    DrawRectangleRounded(pill, 0.4f, 8, Fade(UI_BG, 0.9f));
    DrawRectangleRoundedLinesEx(pill, 0.4f, 8, 1.5f, boss ? (Color){ 230, 100, 80, 255 } : UI_BORDER);
    float x = pill.x + 14;
    DesenharTexto(label, (int)x, (int)pill.y + 8, 16, UI_MUTED);
    x += lw + 12;
    for (int k = 0; k < 3; k++) {
        if (counts[k] == 0) continue;
        DrawMiniBug(k, (Vector2){ x + 8, pill.y + 17 });
        const char *num = TextFormat("%d", counts[k]);
        DesenharTexto(num, (int)x + 20, (int)pill.y + 8, 16, UI_TEXT);
        x += 22 + MedirTexto(num, 16) + 12;
    }
    if (boss)          DesenharTexto("Onda de chefe!", (int)pill.x + 14, (int)pill.y + 31, 14, (Color){ 255, 150, 120, 255 });
    else if (entrance) DesenharTexto("Abre a 2ª entrada!", (int)pill.x + 14, (int)pill.y + 31, 14, ORANGE);
}

/* Contador de pragas restantes no canto superior direito */
static void DrawWaveCounter(void)
{
    if (!waveActive) { DrawNextWavePreview(); return; }
    int alive = 0;
    for (int i = 0; i < MAX_ENEMIES; i++) if (enemies[i].active) alive++;
    int remaining = (waveSize - spawnedInWave) + alive;
    Rectangle pill = { SCREEN_W - 182.0f, 10, 172, 32 };
    DrawRectangleRounded((Rectangle){ pill.x + 2, pill.y + 3, pill.width, pill.height }, 0.5f, 8, Fade(BLACK, 0.3f));
    DrawRectangleRounded(pill, 0.5f, 8, Fade(UI_BG, 0.9f));
    DrawRectangleRoundedLinesEx(pill, 0.5f, 8, 1.5f, UI_BORDER);
    DrawMiniBug(ENEMY_NORMAL, (Vector2){ pill.x + 19, pill.y + 17 });
    DesenharTexto(TextFormat("%d restante%s", remaining, remaining == 1 ? "" : "s"), (int)pill.x + 36, (int)pill.y + 8, 16, UI_TEXT);
}

/* Faixa que entra deslizando no comeco de cada onda */
static void DrawWaveBanner(void)
{
    if (bannerTimer <= 0) return;
    float since = BANNER_TIME - bannerTimer;
    float in = fminf(since / 0.35f, 1.0f), out = fminf(bannerTimer / 0.35f, 1.0f);
    float ease = 1 - powf(1 - in, 3);
    float w = 460, h = 92;
    float x = -w + (SCREEN_W / 2.0f + w / 2) * ease + (1 - out) * (SCREEN_W / 2.0f + w / 2);
    float y = GRID_H / 2.0f - h / 2 - 70;
    Color band = bannerBoss ? (Color){ 150, 38, 34, 255 } : (Color){ 38, 66, 42, 255 };
    Color edge = bannerBoss ? (Color){ 245, 120, 90, 255 } : UI_ACCENT;

    /* pontas da fita */
    DrawTriangle((Vector2){ x - 24, y + 14 }, (Vector2){ x + 10, y + h + 10 }, (Vector2){ x + 10, y + 14 }, ColorBrightness(band, -0.3f));
    DrawTriangle((Vector2){ x + w - 10, y + 14 }, (Vector2){ x + w - 10, y + h + 10 }, (Vector2){ x + w + 24, y + 14 }, ColorBrightness(band, -0.3f));
    DrawRectangleRounded((Rectangle){ x + 3, y + 6, w, h }, 0.2f, 8, Fade(BLACK, 0.35f));
    DrawRectangleRounded((Rectangle){ x, y, w, h }, 0.2f, 8, band);
    DrawRectangleRounded((Rectangle){ x + 4, y + 4, w - 8, h * 0.4f }, 0.4f, 8, Fade(WHITE, 0.07f));
    DrawRectangleRoundedLinesEx((Rectangle){ x, y, w, h }, 0.2f, 8, 3, edge);

    float pulse = bannerBoss ? 1.0f + 0.05f * sinf(since * 12) : 1.0f;
    int fs = (int)(46 * pulse);
    int tw = MedirTexto(bannerText, fs);
    DrawTextShadow(bannerText, (int)(x + (w - tw) / 2), (int)(y + 10), fs, bannerBoss ? (Color){ 255, 220, 200, 255 } : UI_ACCENT);
    int sw = MedirTexto(bannerSub, 18);
    DrawTextShadow(bannerSub, (int)(x + (w - sw) / 2), (int)(y + 62), 18, UI_TEXT);
}

static void DrawToast(int y)
{
    if (msgTimer <= 0) return;
    float a = msgTimer < 0.3f ? msgTimer / 0.3f : 1.0f;
    int fs = 20, w = MedirTexto(msgText, fs);
    Rectangle r = { SCREEN_W / 2.0f - w / 2.0f - 16, (float)y, w + 32.0f, fs + 16.0f };
    DrawRectangleRounded((Rectangle){ r.x + 2, r.y + 4, r.width, r.height }, 0.45f, 8, Fade(BLACK, 0.35f * a));
    DrawRectangleRounded(r, 0.45f, 8, Fade((Color){ 25, 32, 27, 255 }, 0.94f * a));
    DrawRectangleRoundedLinesEx(r, 0.45f, 8, 2, Fade(UI_ACCENT, a));
    DesenharTexto(msgText, (int)r.x + 16, (int)r.y + 8, fs, Fade(UI_TEXT, a));
}

static void DrawSpecialLine(const TowerInfo *ti, int level, int x, int y, int fs)
{
    float m = 1.0f + UPG_DAMAGE * (level - 1);
    if (ti->splash > 0)    DesenharTexto(TextFormat("Dano em área: %.0f px", ti->splash * (1.0f + 0.15f * (level - 1))), x, y, fs, ORANGE);
    if (ti->slow > 0)      DesenharTexto(TextFormat("Lentidão em área: -%.0f%%", (ti->slow + 0.10f * (level - 1)) * 100), x, y, fs, SKYBLUE);
    if (ti->poisonDps > 0) DesenharTexto(TextFormat("Veneno: %.0f + %.0f%% da vida/s", ti->poisonDps * m, ti->poisonPct * 100 * m), x, y, fs, LIME);
}

/* Cartao com os dados da planta selecionada, ao lado dela no mapa */
static void DrawSelectionCard(void)
{
    Tower *t = &towers[selR][selC];
    const TowerInfo *ti = &TOWER_INFO[t->kind];
    float w = 196, h = 118;
    float x = selC * TILE + TILE + 10.0f;
    if (x + w > SCREEN_W - 4) x = selC * TILE - w - 10.0f;
    float y = selR * TILE - 20.0f;
    if (y < 4) y = 4;
    if (y + h > GRID_H - 4) y = GRID_H - h - 4;

    Rectangle card = { x, y, w, h };
    DrawPanel(card, Fade((Color){ 28, 38, 30, 255 }, 0.95f), ti->color);
    DrawPlant(t->kind, (Vector2){ x + 22, y + 24 }, 0.7f, 0, false);
    DesenharTexto(ti->name, (int)x + 44, (int)y + 10, 20, UI_TEXT);
    DesenharTexto(TextFormat("Nível %d/%d", t->level, MAX_LEVEL), (int)x + 44, (int)y + 32, 12, UI_MUTED);
    DrawLevelPips(x + 124, y + 38, t->level);

    DesenharTexto(TextFormat("Dano %.0f   Alcance %.0f", TowerDamage(t), TowerRange(t)), (int)x + 10, (int)y + 54, 12, UI_TEXT);
    DesenharTexto(TextFormat("Tiros/s %.1f", TowerFireRate(t)), (int)x + 10, (int)y + 70, 12, UI_TEXT);
    DrawSpecialLine(ti, t->level, (int)x + 10, (int)y + 86, 12);
    const char *next = t->level >= MAX_LEVEL ? "Nível máximo"
                     : !upgradesUnlocked ? TextFormat("Melhorias na onda %d", UPGRADE_UNLOCK_WAVE)
                     : TextFormat("Próximo nível: $%d", UpgradeCost(t));
    DesenharTexto(next, (int)x + 10, (int)y + 100, 12, UI_ACCENT);
}

/* Dica ao passar o mouse sobre um botao de planta */
static void DrawPlantTooltip(int i, float bx, int y0)
{
    const TowerInfo *ti = &TOWER_INFO[i];
    float w = 270, h = 112;
    float x = bx + 39 - w / 2;
    if (x < 4) x = 4;
    if (x + w > SCREEN_W - 4) x = SCREEN_W - w - 4;
    Rectangle tip = { x, y0 - h - 8.0f, w, h };
    DrawPanel(tip, Fade((Color){ 28, 38, 30, 255 }, 0.96f), ti->color);
    DrawPlant((TowerKind)i, (Vector2){ x + 24, tip.y + 26 }, 0.75f, 0, !unlocked[i]);
    DesenharTexto(ti->name, (int)x + 48, (int)tip.y + 10, 20, UI_TEXT);
    DrawCoin((Vector2){ x + 56, tip.y + 41 }, 6);
    DesenharTexto(TextFormat("%d", ti->cost), (int)x + 66, (int)tip.y + 35, 14, UI_ACCENT);
    DesenharTexto(ti->desc, (int)x + 10, (int)tip.y + 56, 12, UI_MUTED);
    DesenharTexto(TextFormat("Dano %.0f   Alcance %.0f   Tiros/s %.1f", ti->damage, ti->range, ti->fireRate), (int)x + 10, (int)tip.y + 74, 12, UI_TEXT);
    if (!unlocked[i]) DesenharTexto(TextFormat("Bloqueada: %d sementes na loja [L]", ti->seedPrice), (int)x + 10, (int)tip.y + 92, 12, (Color){ 240, 140, 120, 255 });
    else              DrawSpecialLine(ti, 1, (int)x + 10, (int)tip.y + 92, 12);
}

static void DrawUI(void)
{
    bool live = (state == STATE_PLAYING);
    int y0 = GRID_H;
    Vector2 mouse = GetMousePosition();

    DrawMapVignette(live);
    if (live) DrawWaveCounter();
    if (live && selC >= 0) DrawSelectionCard();

    /* Barra inferior */
    DrawRectangle(0, y0, SCREEN_W, UI_H, UI_BG);
    DrawRectangleGradientV(0, y0, SCREEN_W, 12, Fade(BLACK, 0.45f), Fade(BLACK, 0.0f));
    DrawRectangle(0, y0, SCREEN_W, 3, (Color){ 128, 94, 54, 255 });

    /* Painel de status */
    DrawPanel((Rectangle){ 8, y0 + 8.0f, 154, 110 }, UI_PANEL, UI_BORDER);
    DrawCoin((Vector2){ 26, y0 + 22.0f }, 9);
    DrawTextShadow(TextFormat("%d", money), 42, y0 + 13, 20, UI_ACCENT);
    DrawSeedIcon((Vector2){ 26, y0 + 47.0f }, 8);
    DrawTextShadow(TextFormat("%d", seeds), 42, y0 + 38, 20, (Color){ 232, 200, 150, 255 });
    DrawHeart((Vector2){ 26, y0 + 71.0f }, 8, (baseFlash > 0) ? WHITE : (Color){ 232, 70, 70, 255 });
    DrawTextShadow(TextFormat("%d", lives), 42, y0 + 62, 20, (Color){ 255, 135, 125, 255 });
    DrawFlagIcon((Vector2){ 26, y0 + 94.0f }, 8);
    DrawTextShadow(TextFormat("Onda %d/%d", wave, MAX_WAVES), 42, y0 + 85, 20, UI_TEXT);

    float prog = 0;
    if (waveActive && waveSize > 0) {
        int alive = 0;
        for (int i = 0; i < MAX_ENEMIES; i++) if (enemies[i].active) alive++;
        prog = (float)(spawnedInWave - alive) / waveSize;
    } else if (wave > 0) prog = 1;
    DrawRectangleRounded((Rectangle){ 20, y0 + 108.0f, 130, 5 }, 1, 4, (Color){ 18, 24, 20, 255 });
    if (prog > 0) DrawRectangleRounded((Rectangle){ 20, y0 + 108.0f, 130 * prog, 5 }, 1, 4, (Color){ 120, 200, 90, 255 });

    /* Botoes das plantas (bloqueadas abrem a loja) */
    int hovered = -1;
    for (int i = 0; i < TOWER_COUNT; i++) {
        Rectangle rec = { 170.0f + i * 82, y0 + 8.0f, 78, 110 };
        bool can = live && !waveActive && unlocked[i] && money >= TOWER_INFO[i].cost;
        if (DrawButton(rec, "", NULL, can, selBuild == i)) {
            selBuild = (selBuild == i) ? -1 : i;
            selC = selR = -1;
        }
        if (CheckCollisionPointRec(mouse, rec)) hovered = i;

        Color txt = (unlocked[i] && !waveActive) ? UI_TEXT : UI_MUTED;
        DesenharTexto(TextFormat("%d", i + 1), (int)rec.x + 7, (int)rec.y + 6, 12, UI_MUTED);
        Vector2 icon = { rec.x + rec.width / 2, rec.y + 32 };
        DrawPlant((TowerKind)i, icon, 0.68f, 0, !unlocked[i]);
        const char *name = TOWER_INFO[i].name;
        DesenharTexto(name, (int)(rec.x + (rec.width - MedirTexto(name, 14)) / 2), (int)rec.y + 60, 14, txt);

        if (!unlocked[i]) {
            DrawLockIcon(icon, 1.1f, UI_TEXT);
            const char *l = "Loja";
            if (!waveActive) DesenharTexto(l, (int)(rec.x + (rec.width - MedirTexto(l, 14)) / 2), (int)rec.y + 80, 14, (Color){ 240, 150, 120, 255 });
            if (live && CheckCollisionPointRec(mouse, rec) && IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) OpenShop();
        } else if (!waveActive) {
            const char *price = TextFormat("%d", TOWER_INFO[i].cost);
            int pw = MedirTexto(price, 14), total = 16 + pw;
            float px = rec.x + (rec.width - total) / 2;
            DrawCoin((Vector2){ px + 6, rec.y + 87 }, 6);
            DesenharTexto(price, (int)px + 16, (int)rec.y + 80, 14, money >= TOWER_INFO[i].cost ? UI_ACCENT : (Color){ 230, 110, 100, 255 });
        }
    }

    if (live && waveActive) {
        const char *aviso = "Plante quando a onda acabar";
        Rectangle b = { 172, y0 + 84.0f, 488, 24 };
        DrawRectangleRounded(b, 0.5f, 8, Fade((Color){ 20, 24, 20, 255 }, 0.92f));
        DrawRectangleRoundedLinesEx(b, 0.5f, 8, 1.5f, ORANGE);
        DesenharTexto(aviso, (int)(b.x + (b.width - MedirTexto(aviso, 16)) / 2), (int)b.y + 4, 16, ORANGE);
    }

    /* Acoes */
    float rx = 676;
    if (DrawButton((Rectangle){ rx, y0 + 8.0f, 135, 34 },
                   waveActive ? "Onda em curso" : "Onda [Espaço]", NULL, live && !waveActive, !waveActive && live))
        StartWave();
    if (DrawButton((Rectangle){ rx + 145, y0 + 8.0f, 135, 34 },
                   TextFormat("Velocidade %dx [F]", gameSpeed), NULL, live, gameSpeed == 2))
        gameSpeed = (gameSpeed == 1) ? 2 : 1;

    if (selC >= 0) {
        Tower *t = &towers[selR][selC];
        const char *up = !upgradesUnlocked ? TextFormat("Melhorar: onda %d", UPGRADE_UNLOCK_WAVE)
                       : (t->level >= MAX_LEVEL) ? "Nível máximo"
                       : TextFormat("Melhorar $%d [U]", UpgradeCost(t));
        if (DrawButton((Rectangle){ rx, y0 + 46.0f, 135, 34 }, up, NULL,
                       live && upgradesUnlocked && t->level < MAX_LEVEL && money >= UpgradeCost(t), false))
            UpgradeSelected();
        if (DrawButton((Rectangle){ rx + 145, y0 + 46.0f, 135, 34 },
                       TextFormat("Vender $%d [S]", SellValue(t)), NULL, live, false))
            SellSelected();
    } else {
        Rectangle hint = { rx, y0 + 46.0f, 280, 34 };
        DrawRectangleRounded(hint, 0.3f, 8, (Color){ 32, 42, 34, 255 });
        DesenharTexto(TextFormat("Fase %d: %s", currentLevel + 1, LEVELS[currentLevel].name), (int)rx + 10, y0 + 50, 13, UI_ACCENT);
        DesenharTexto(upgradesUnlocked ? "Clique numa planta: detalhes, melhorar, vender" : "Clique numa planta: detalhes ou vender",
                 (int)rx + 10, y0 + 64, 13, UI_MUTED);
    }

    if (DrawButton((Rectangle){ rx, y0 + 84.0f, 280, 34 }, "Loja de plantas [L]", NULL, live, false))
        OpenShop();

    if (live) DrawWaveBanner();
    if (live && hovered >= 0) DrawPlantTooltip(hovered, 170.0f + hovered * 82, y0);
    if (live) DrawToast(12);
}

static void DrawShop(void)
{
    DrawCentered("LOJA DE PLANTAS", 26, 44, UI_ACCENT);
    const char *sv = TextFormat("%d sementes", seeds);
    int sw = MedirTexto(sv, 24);
    DrawSeedIcon((Vector2){ SCREEN_W / 2.0f - sw / 2.0f - 16, 92 }, 11);
    DrawTextShadow(sv, SCREEN_W / 2 - sw / 2 + 4, 80, 24, (Color){ 232, 200, 150, 255 });
    DrawCentered(TextFormat("Ganhe sementes: +%d por onda, +%d extra em onda de chefe, +%d por tanque destruído",
                            SEEDS_PER_WAVE, SEEDS_BOSS_WAVE, SEEDS_PER_TANK), 116, 16, UI_MUTED);

    for (int i = 0; i < TOWER_COUNT; i++) {
        const TowerInfo *ti = &TOWER_INFO[i];
        int col = i % 3, row = i / 3;
        Rectangle card = { 30.0f + col * 310, 145.0f + row * 230, 280, 210 };
        DrawPanel(card, UI_PANEL, unlocked[i] ? (Color){ 110, 200, 90, 255 } : UI_BORDER);

        int x = (int)card.x, y = (int)card.y;
        DrawCircle(x + 34, y + 34, 26, Fade(BLACK, 0.2f));
        DrawPlant((TowerKind)i, (Vector2){ x + 34.0f, y + 34.0f }, 1.1f, (float)GetTime() * 0.6f, false);
        DrawTextShadow(ti->name, x + 70, y + 16, 22, UI_TEXT);
        DesenharTexto(TextFormat("Tecla %d", i + 1), x + 70, y + 42, 12, UI_MUTED);
        DesenharTexto(ti->desc, x + 14, y + 70, 13, UI_MUTED);
        DrawCoin((Vector2){ x + 20, y + 98.0f }, 6);
        DesenharTexto(TextFormat("Plantar: $%d", ti->cost), x + 32, y + 92, 14, UI_ACCENT);
        DesenharTexto(TextFormat("Dano %.0f   Alcance %.0f   Tiros/s %.1f", ti->damage, ti->range, ti->fireRate), x + 14, y + 114, 13, UI_TEXT);
        DrawSpecialLine(ti, 1, x + 14, y + 134, 13);

        Rectangle btn = { card.x + 14, card.y + 164, 252, 34 };
        if (unlocked[i]) {
            DrawRectangleRounded(btn, 0.3f, 8, (Color){ 34, 78, 40, 255 });
            const char *ok = "LIBERADA";
            DesenharTexto(ok, (int)(btn.x + (btn.width - MedirTexto(ok, 18)) / 2), (int)btn.y + 8, 18, (Color){ 140, 230, 110, 255 });
        } else if (DrawButton(btn, TextFormat("Comprar - %d sementes", ti->seedPrice), NULL, seeds >= ti->seedPrice, seeds >= ti->seedPrice)) {
            BuyTower(i);
        }
    }

    if (DrawButton((Rectangle){ SCREEN_W / 2.0f - 110, 616, 220, 42 }, "Voltar [L / ESC]", NULL, true, false))
        state = shopReturn;
    DrawToast(664);
}

/* Tutorial mostrado quando as melhorias sao liberadas */
static void DrawUpgradeTutorial(void)
{
    DrawPanel((Rectangle){ 110, 50, SCREEN_W - 220.0f, 600 }, (Color){ 30, 42, 32, 250 }, UI_ACCENT);

    DrawCentered("NOVIDADE: MELHORIAS LIBERADAS!", 72, 32, UI_ACCENT);
    DrawCentered("A partir de agora suas plantas podem ficar mais fortes", 114, 18, UI_TEXT);

    float xs[3] = { 300, 480, 660 };
    float ranges[3] = { 50, 58, 66 };
    for (int lv = 0; lv < 3; lv++) {
        Vector2 p = { xs[lv], 225 };
        DrawCircleV(p, ranges[lv], Fade(WHITE, 0.08f));
        DrawCircleLinesV(p, ranges[lv], Fade(WHITE, 0.5f));
        DrawPlant(TOWER_BASIC, p, 1.2f + 0.15f * lv, -0.3f, false);
        DrawLevelPips(p.x - 9, p.y + 34, lv + 1);
        const char *lbl = TextFormat("Nível %d", lv + 1);
        DesenharTexto(lbl, (int)p.x - MedirTexto(lbl, 18) / 2, 300, 18, UI_TEXT);
        if (lv < 2) DesenharTexto(">", (int)(xs[lv] + 82), 212, 30, UI_ACCENT);
    }

    const TowerInfo *ex = &TOWER_INFO[TOWER_BASIC];
    /* Copia os textos formatados: o TextFormat reaproveita a mesma memoria a cada chamada */
    char passo3[96], passo4[96];
    snprintf(passo3, sizeof(passo3), "Cada nível: +%.0f%% de dano, +%.0f%% de alcance e +%.0f%% de tiros/s.",
             UPG_DAMAGE * 100, UPG_RANGE * 100, UPG_FIRERATE * 100);
    snprintf(passo4, sizeof(passo4), "Custo: nível 2 = preço da planta, nível 3 = 2x (%s: $%d e $%d).",
             ex->name, ex->cost, ex->cost * 2);
    const char *steps[] = {
        "Clique em uma planta que já está no mapa para selecioná-la.",
        "Clique em \"Melhorar\" (ou aperte U) para subir de nível.",
        passo3,
        passo4,
        "O nível máximo é 3 - as bolinhas douradas mostram o nível.",
    };
    for (int i = 0; i < 5; i++) {
        int y = 338 + i * 32;
        char num[4];
        snprintf(num, sizeof(num), "%d", i + 1);
        DrawCircle(160, y + 9, 11, UI_BORDER);
        DesenharTexto(num, 156, y + 2, 16, UI_TEXT);
        DesenharTexto(steps[i], 182, y, 18, UI_TEXT);
    }
    DesenharTexto("Dica: vender (S) devolve 70% de tudo o que você gastou na planta,", 150, 510, 16, (Color){ 232, 200, 150, 255 });
    DesenharTexto("incluindo as melhorias.", 150, 530, 16, (Color){ 232, 200, 150, 255 });

    if (DrawButton((Rectangle){ SCREEN_W / 2.0f - 110, 578, 220, 44 }, "Entendi! [ENTER]", NULL, true, true))
        state = STATE_PLAYING;
}

static void DrawMenu(void)
{
    DrawPanel((Rectangle){ 150, 50, 660, 590 }, (Color){ 30, 42, 32, 248 }, UI_BORDER);
    float tt = (float)GetTime();
    DrawPlant(TOWER_LASER,  (Vector2){ 200, 108 }, 1.35f, tt * 0.5f, false);
    DrawPlant(TOWER_SNIPER, (Vector2){ 760, 108 }, 1.35f, sinf(tt) * 0.8f, false);
    DrawCentered("TOWER DEFENSE", 80, 52, UI_ACCENT);
    DrawCentered(TextFormat("Defenda o jardim das pragas por %d ondas", MAX_WAVES), 142, 20, UI_TEXT);

    DesenharTexto("CONTROLES", 200, 186, 16, UI_MUTED);
    struct { const char *k; const char *d; } rows[] = {
        { "1 - 6",    "Escolher planta" },     { "Clique",     "Plantar / selecionar" },
        { "U",        "Melhorar" },            { "S",          "Vender" },
        { "Espaço",   "Próxima onda" },        { "L",          "Loja de plantas" },
        { "F",        "Velocidade 1x / 2x" },  { "P / Esc",    "Pausar" },
        { "D",        "Mapa da BFS" },         { "Botão dir.", "Cancelar" },
        { "M",        "Som liga / desliga" },  { "Enter",      "Escolher fase" },
    };
    rows[2].d = TextFormat("Melhorar (onda %d+)", UPGRADE_UNLOCK_WAVE);
    for (int i = 0; i < 12; i++) {
        int x = 200 + (i % 2) * 290, y = 210 + (i / 2) * 30;
        DrawKeyChip(rows[i].k, x, y);
        DesenharTexto(rows[i].d, x + 104, y + 3, 16, UI_TEXT);
    }

    DesenharTexto("DICAS", 200, 398, 16, UI_MUTED);
    const char *tips[] = {
        "As pragas seguem a estrada - plante na grama, perto das curvas.",
        "Só dá para plantar quando a onda acaba.",
        TextFormat("%d fases: vença uma para liberar a próxima.", LEVEL_COUNT),
        "Ganhe sementes e libere novas plantas na loja (o progresso fica salvo).",
    };
    for (int i = 0; i < 4; i++) {
        int y = 422 + i * 26;
        DrawCircle(208, y + 8, 4, (Color){ 120, 200, 90, 255 });
        DesenharTexto(tips[i], 222, y, 16, UI_TEXT);
    }

    if (DrawButton((Rectangle){ SCREEN_W / 2.0f - 230, 534, 220, 52 }, "Jogar [ENTER]", NULL, true, true))
        state = STATE_LEVELS;
    if (DrawButton((Rectangle){ SCREEN_W / 2.0f + 10, 534, 220, 52 }, "Loja [L]", NULL, true, false))
        OpenShop();
    DrawCentered("Feito em C com raylib", 604, 12, UI_MUTED);
}

/* Comeca (ou recomeca) uma fase */
static void StartLevel(int i)
{
    if (i < 0 || i >= LEVEL_COUNT || !LevelUnlocked(i)) return;
    currentLevel = i;
    ResetGame();
    state = STATE_PLAYING;
    ShowMessage(TextFormat("Fase %d: %s", i + 1, LEVELS[i].name));
}

/* Miniatura do mapa de uma fase, desenhada direto do texto do mapa */
static void DrawMiniMap(int level, float x, float y, float cell)
{
    const LevelInfo *lv = &LEVELS[level];
    for (int r = 0; r < ROWS; r++) {
        for (int c = 0; c < COLS; c++) {
            char ch = lv->map[r][c];
            Color col = ((r + c) % 2 == 0) ? lv->grassA : lv->grassB;
            if (ch == '=' || ch == 'S' || ch == 'B') col = (Color){ 188, 152, 104, 255 };
            if (ch == '+' || ch == 'T')              col = (Color){ 160, 130, 92, 255 };
            if (ch == '#')                           col = (Color){ 120, 122, 128, 255 };
            DrawRectangle((int)(x + c * cell), (int)(y + r * cell), (int)cell + 1, (int)cell + 1, col);
            if (ch == 'S' || ch == 'T') DrawCircle((int)(x + c * cell + cell / 2), (int)(y + r * cell + cell / 2), cell * 0.4f, (Color){ 40, 26, 18, 255 });
            if (ch == 'B') DrawCircle((int)(x + c * cell + cell / 2), (int)(y + r * cell + cell / 2), cell * 0.45f, (Color){ 196, 64, 52, 255 });
        }
    }
    DrawRectangleLinesEx((Rectangle){ x - 1, y - 1, COLS * cell + 2, ROWS * cell + 2 }, 2, Fade(BLACK, 0.4f));
}

static void DrawLevelSelect(void)
{
    DrawCentered("ESCOLHA A FASE", 34, 44, UI_ACCENT);
    DrawCentered("Vença uma fase para liberar a próxima", 90, 18, UI_MUTED);

    for (int i = 0; i < LEVEL_COUNT; i++) {
        const LevelInfo *lv = &LEVELS[i];
        bool open = LevelUnlocked(i), won = (levelsWon & (1 << i)) != 0;
        Rectangle card = { 30.0f + i * 310, 130, 280, 420 };
        DrawPanel(card, UI_PANEL, won ? (Color){ 110, 200, 90, 255 } : open ? UI_BORDER : (Color){ 70, 76, 72, 255 });
        int x = (int)card.x, y = (int)card.y;

        DrawMiniMap(i, card.x + 20, card.y + 18, 12);
        const char *rec = won ? TextFormat("VENCIDA · %d vidas", bestLives[i])
                        : bestWave[i] > 0 ? TextFormat("Recorde: onda %d", bestWave[i]) : NULL;
        if (rec) {
            float bw = MedirTexto(rec, 14) + 20.0f;
            Rectangle badge = { card.x + 254 - bw, card.y + 24, bw, 22 };
            DrawRectangleRounded(badge, 0.5f, 8, won ? (Color){ 60, 140, 60, 255 } : (Color){ 40, 52, 44, 235 });
            DesenharTexto(rec, (int)badge.x + 10, (int)badge.y + 3, 14, UI_TEXT);
        }

        DesenharTexto(TextFormat("Fase %d", i + 1), x + 20, y + 176, 14, UI_MUTED);
        DrawTextShadow(lv->name, x + 20, y + 194, 28, UI_TEXT);
        DesenharTexto(lv->desc, x + 20, y + 230, 13, UI_MUTED);

        DesenharTexto("Dificuldade", x + 20, y + 258, 14, UI_TEXT);
        for (int k = 0; k < LEVEL_COUNT; k++) {
            Color pip = k <= i ? (Color){ 230, 90, 70, 255 } : (Color){ 60, 68, 62, 255 };
            DrawCircle(x + 130 + k * 20, y + 266, 6, pip);
        }
        DesenharTexto(TextFormat("Estrada: %d passos até a casa", lv->pathLen), x + 20, y + 286, 14, UI_TEXT);
        DrawCoin((Vector2){ x + 27.0f, y + 316.0f }, 6);
        DesenharTexto(TextFormat("Dinheiro inicial: $%d", lv->startMoney), x + 38, y + 308, 14, UI_TEXT);
        DrawFlagIcon((Vector2){ x + 27.0f, y + 338.0f }, 7);
        DesenharTexto(TextFormat("2ª entrada abre na onda %d", lv->secondWave), x + 38, y + 330, 14, UI_TEXT);

        if (DrawButton((Rectangle){ card.x + 20, card.y + 362, 240, 42 }, TextFormat("Jogar [%d]", i + 1), NULL, open, open))
            StartLevel(i);

        if (!open) {
            DrawRectangleRounded(card, 0.08f, 8, Fade(BLACK, 0.55f));
            DrawLockIcon((Vector2){ card.x + card.width / 2, card.y + 78 }, 3.0f, UI_TEXT);
            const char *need = TextFormat("Vença a fase %d", i);
            DrawTextShadow(need, (int)(card.x + (card.width - MedirTexto(need, 20)) / 2), (int)card.y + 116, 20, UI_TEXT);
        }
    }

    if (DrawButton((Rectangle){ SCREEN_W / 2.0f - 110, 584, 220, 42 }, "Voltar [ESC]", NULL, true, false))
        state = STATE_MENU;
    if (state == STATE_PLAYING) return;
    DrawToast(640);
}

static void DrawEndScreen(bool victory)
{
    Rectangle pnl = { SCREEN_W / 2.0f - 250, 160, 500, 340 };
    DrawPanel(pnl, (Color){ 30, 42, 32, 248 }, victory ? UI_ACCENT : (Color){ 200, 70, 60, 255 });
    if (victory) {
        DrawPlant(TOWER_LASER, (Vector2){ pnl.x + 60, pnl.y + 60 }, 1.5f, (float)GetTime(), false);
        DrawPlant(TOWER_LASER, (Vector2){ pnl.x + pnl.width - 60, pnl.y + 60 }, 1.5f, -(float)GetTime(), false);
        DrawCentered("VITÓRIA!", (int)pnl.y + 30, 52, UI_ACCENT);
        DrawCentered("O jardim está a salvo!", (int)pnl.y + 92, 20, UI_TEXT);
    } else {
        DrawCentered("FIM DE JOGO", (int)pnl.y + 30, 52, (Color){ 240, 90, 80, 255 });
        DrawCentered("As pragas chegaram na casinha...", (int)pnl.y + 92, 20, UI_TEXT);
    }

    int y = (int)pnl.y + 140;
    DrawFlagIcon((Vector2){ pnl.x + 150, y + 10.0f }, 9);
    DesenharTexto(TextFormat("Onda alcançada: %d/%d", wave, MAX_WAVES), (int)pnl.x + 170, y, 20, UI_TEXT);
    DrawHeart((Vector2){ pnl.x + 150, y + 42.0f }, 9, (Color){ 232, 70, 70, 255 });
    DesenharTexto(TextFormat("Vidas restantes: %d", lives), (int)pnl.x + 170, y + 32, 20, UI_TEXT);
    DrawSeedIcon((Vector2){ pnl.x + 150, y + 74.0f }, 9);
    DesenharTexto(TextFormat("Sementes guardadas: %d", seeds), (int)pnl.x + 170, y + 64, 20, UI_TEXT);

    DrawCentered(TextFormat("Fase %d: %s", currentLevel + 1, LEVELS[currentLevel].name), (int)pnl.y + 118, 14, UI_MUTED);

    bool hasNext = victory && currentLevel + 1 < LEVEL_COUNT;
    const char *mainLabel = hasNext ? "Próxima fase [ENTER]" : victory ? "Jogar de novo [ENTER]" : "Tentar de novo [ENTER]";
    if (DrawButton((Rectangle){ pnl.x + 20, pnl.y + 262, 225, 50 }, mainLabel, NULL, true, true)) {
        if (hasNext) StartLevel(currentLevel + 1);
        else         StartLevel(currentLevel);
    }
    if (DrawButton((Rectangle){ pnl.x + 255, pnl.y + 262, 225, 50 }, "Escolher fase", NULL, true, false))
        state = STATE_LEVELS;
    if (victory && !hasNext)
        DrawCentered(newRecord ? "Você venceu todas as fases! Novo recorde!" : "Você venceu todas as fases!", (int)pnl.y + 238, 16, UI_ACCENT);
    else if (newRecord)
        DrawCentered("Novo recorde nesta fase!", (int)pnl.y + 238, 16, UI_ACCENT);
}

static void DrawOverlay(void)
{
    if (state == STATE_PLAYING) return;
    DrawRectangle(0, 0, SCREEN_W, SCREEN_H, Fade(BLACK, state == STATE_SHOP ? 0.82f : 0.6f));

    switch (state) {
        case STATE_MENU:     DrawMenu(); break;
        case STATE_LEVELS:   DrawLevelSelect(); break;
        case STATE_SHOP:     DrawShop(); break;
        case STATE_TUTORIAL: DrawUpgradeTutorial(); break;
        case STATE_PAUSED: {
            DrawPanel((Rectangle){ SCREEN_W / 2.0f - 200, 210, 400, 270 }, (Color){ 30, 42, 32, 248 }, UI_BORDER);
            DrawCentered("PAUSADO", 234, 48, UI_TEXT);
            DrawCentered(TextFormat("Fase %d: %s", currentLevel + 1, LEVELS[currentLevel].name), 290, 16, UI_MUTED);
            if (DrawButton((Rectangle){ SCREEN_W / 2.0f - 110, 322, 220, 46 }, "Continuar [P]", NULL, true, true))
                state = STATE_PLAYING;
            if (DrawButton((Rectangle){ SCREEN_W / 2.0f - 110, 378, 220, 46 }, "Escolher fase", NULL, true, false))
                state = STATE_LEVELS;
            DrawCentered("Esc também continua", 442, 14, UI_MUTED);
        } break;
        case STATE_GAMEOVER: DrawEndScreen(false); break;
        case STATE_VICTORY:  DrawEndScreen(true); break;
        default: break;
    }
}

/* ------------------------------------------------------------------ */
/* main                                                               */
/* ------------------------------------------------------------------ */
int main(void)
{
    InitWindow(SCREEN_W, SCREEN_H, "Tower Defense - raylib");
    SetExitKey(KEY_NULL);   /* ESC pausa em vez de fechar */
    SetTargetFPS(60);
    CarregarFontes();
    InitAudioDevice();
    LoadSounds();
    LoadProgress();
    ResetGame();

    while (!WindowShouldClose()) {
        float dt = GetFrameTime();
        if (dt > 0.05f) dt = 0.05f;
        if (msgTimer > 0) msgTimer -= dt;

        if (IsKeyPressed(KEY_M)) {   /* liga/desliga o som em qualquer tela */
            soundOn = !soundOn;
            SaveProgress();
            ShowMessage(soundOn ? "Som ligado" : "Som desligado");
        }

        switch (state) {
            case STATE_MENU:
                if (IsKeyPressed(KEY_ENTER)) state = STATE_LEVELS;
                else if (IsKeyPressed(KEY_L)) OpenShop();
                break;
            case STATE_LEVELS:
                if (IsKeyPressed(KEY_ESCAPE)) state = STATE_MENU;
                for (int i = 0; i < LEVEL_COUNT; i++)
                    if (IsKeyPressed(KEY_ONE + i)) StartLevel(i);
                break;
            case STATE_PLAYING:
                if (IsKeyPressed(KEY_P) || IsKeyPressed(KEY_ESCAPE)) { state = STATE_PAUSED; break; }
                HandleInput();
                for (int s = 0; s < gameSpeed && state == STATE_PLAYING; s++) {
                    UpdateWave(dt);
                    UpdateEnemies(dt);
                    UpdateTowers(dt);
                    UpdateProjectiles(dt);
                    UpdateEffects(dt);
                }
                break;
            case STATE_PAUSED:
                if (IsKeyPressed(KEY_P) || IsKeyPressed(KEY_ESCAPE)) state = STATE_PLAYING;
                break;
            case STATE_SHOP:   /* o jogo fica pausado enquanto a loja esta aberta */
                if (IsKeyPressed(KEY_L) || IsKeyPressed(KEY_ESCAPE)) state = shopReturn;
                break;
            case STATE_TUTORIAL: /* jogo pausado ate o jogador fechar o tutorial */
                if (IsKeyPressed(KEY_ENTER) || IsKeyPressed(KEY_ESCAPE)) state = STATE_PLAYING;
                break;
            case STATE_GAMEOVER:
                if (IsKeyPressed(KEY_ENTER)) StartLevel(currentLevel);
                break;
            case STATE_VICTORY:
                if (IsKeyPressed(KEY_ENTER))
                    StartLevel(currentLevel + 1 < LEVEL_COUNT ? currentLevel + 1 : currentLevel);
                break;
        }

        BeginDrawing();
        ClearBackground(BLACK);

        /* O mapa e desenhado com uma camera 2D: quando a casa e atingida,
           a camera treme por um instante */
        Camera2D cam = { 0 };
        cam.zoom = 1.0f;
        if (shake > 0 && state == STATE_PLAYING)
            cam.offset = (Vector2){ RandF(-1, 1) * shake * 14, RandF(-1, 1) * shake * 14 };
        BeginScissorMode(0, 0, SCREEN_W, GRID_H);
        BeginMode2D(cam);
        DrawMap();
        DrawBuildPreview();
        DrawTowers();
        DrawEnemies();
        DrawProjectiles();
        DrawClouds();
        EndMode2D();
        EndScissorMode();

        DrawUI();
        DrawOverlay();
        EndDrawing();
    }

    UnloadSounds();
    CloseAudioDevice();
    DescarregarFontes();
    CloseWindow();
    return 0;
}
