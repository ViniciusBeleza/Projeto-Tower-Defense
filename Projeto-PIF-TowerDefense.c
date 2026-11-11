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

#define START_MONEY 150
#define START_LIVES 20

/* Dificuldade: vida base dos inimigos e quanto ela cresce a cada onda (1.22 = +22%) */
#define ENEMY_BASE_HP 18.0f
#ifndef HP_GROWTH
#define HP_GROWTH     1.20f
#endif

#define SAVE_FILE   "progresso.txt"

/* Sementes ganhas (moeda da loja) */
#define SEEDS_PER_WAVE   1
#define SEEDS_BOSS_WAVE  3   /* bonus extra em ondas de chefe (5, 10, 15, 20) */
#define SEEDS_PER_TANK   2

/* Mapa: '.' grama (pode construir)  '=' estrada  '#' pedra
         'S' entrada dos inimigos  'B' base
         'T' segunda entrada  '+' estrada da segunda entrada
         (fechada ate a onda SECOND_ENTRANCE_WAVE, mas ja nao aceita plantas) */
static const char *MAP[ROWS] = {
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
};

#define SECOND_ENTRANCE_WAVE 10   /* onda em que a segunda entrada abre */

/* ------------------------------------------------------------------ */
/* Tipos                                                              */
/* ------------------------------------------------------------------ */
typedef enum { TILE_EMPTY, TILE_ROCK, TILE_TOWER, TILE_ROAD } TileType;
typedef enum { STATE_MENU, STATE_PLAYING, STATE_PAUSED, STATE_SHOP, STATE_TUTORIAL, STATE_GAMEOVER, STATE_VICTORY } GameState;
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
    { .name = "Abobora",  .desc = "Abobora explosiva: dano em area",
      .cost = 140, .seedPrice = 14, .range = 130, .fireRate = 0.6f, .damage = 22, .projSpeed = 300, .projSize = 7,
      .splash = 55, .color = { 255, 140, 0, 255 } },
    { .name = "Urtiga",   .desc = "Espinhos venenosos: dano continuo",
      .cost = 70,  .seedPrice = 6, .range = 110, .fireRate = 1.0f, .damage = 2,  .projSpeed = 380, .projSize = 4,
      .poisonDps = 8, .poisonPct = POISON_PCT, .color = { 155, 70, 175, 255 } },
    { .name = "Girassol", .desc = "Raios de sol muito rapidos",
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
} Enemy;

typedef struct {
    bool    active;
    Vector2 pos;
    int     target, targetUid;
    float   damage, slow, speed, size, splash, poisonDps, poisonPct;
    Color   color;
} Projectile;

/* Efeito visual simples (explosao do canhao) */
typedef struct {
    bool    active;
    Vector2 pos;
    float   radius, life, maxLife;
    Color   color;
} Effect;

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
static float      baseFlash;              /* pisca a casa quando um inimigo chega */

static int spawnC, spawnR, baseC, baseR;
static int spawn2C = -1, spawn2R = -1;    /* segunda entrada ('T') */
static bool branchRoad[ROWS][COLS];       /* tiles da estrada da segunda entrada */

static GameState state = STATE_MENU;
static GameState shopReturn = STATE_MENU;  /* para onde voltar ao fechar a loja */

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

static float TowerRange(const Tower *t)    { return TOWER_INFO[t->kind].range    * (1.0f + UPG_RANGE    * (t->level - 1)); }
static float TowerDamage(const Tower *t)   { return TOWER_INFO[t->kind].damage   * (1.0f + UPG_DAMAGE   * (t->level - 1)); }
static float TowerPoison(const Tower *t)   { return TOWER_INFO[t->kind].poisonDps * (1.0f + UPG_DAMAGE  * (t->level - 1)); }
static float TowerFireRate(const Tower *t) { return TOWER_INFO[t->kind].fireRate * (1.0f + UPG_FIRERATE * (t->level - 1)); }
static int   UpgradeCost(const Tower *t)   { return TOWER_INFO[t->kind].cost * t->level; }
static int   SellValue(const Tower *t)     { return (int)(t->invested * 0.7f); }

/* ------------------------------------------------------------------ */
/* Progresso salvo e loja                                             */
/* ------------------------------------------------------------------ */
/* Formato do arquivo:
 *   sementes 7
 *   torres 1 1 1 1 0 0
 */
static void SaveProgress(void)
{
    FILE *f = fopen(SAVE_FILE, "w");
    if (!f) return;
    fprintf(f, "sementes %d\ntorres", seeds);
    for (int i = 0; i < TOWER_COUNT; i++) fprintf(f, " %d", unlocked[i] ? 1 : 0);
    fprintf(f, "\n");
    fclose(f);
}

static void LoadProgress(void)
{
    seeds = 0;
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
    if (seeds < TOWER_INFO[kind].seedPrice) { ShowMessage("Sementes insuficientes"); return; }
    seeds -= TOWER_INFO[kind].seedPrice;
    unlocked[kind] = true;
    SaveProgress();
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
    for (int r = 0; r < ROWS; r++) {
        for (int c = 0; c < COLS; c++) {
            char ch = (c < (int)strlen(MAP[r])) ? MAP[r][c] : '.';
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
        case TILE_ROAD:  *reason = "Nao pode plantar na estrada!"; return false;
        case TILE_ROCK:  *reason = "Nao pode plantar na pedra";    return false;
        default:         *reason = "Tile ocupado";                    return false;
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
    baseFlash = 0;
    ComputeDistances(dist);

    money = START_MONEY;
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
}

static void UpgradeSelected(void)
{
    if (selC < 0) return;
    Tower *t = &towers[selR][selC];
    if (!upgradesUnlocked) { ShowMessage(TextFormat("Melhorias liberadas na onda %d", UPGRADE_UNLOCK_WAVE)); return; }
    if (t->level >= MAX_LEVEL) { ShowMessage("Nivel maximo"); return; }
    int cost = UpgradeCost(t);
    if (money < cost) { ShowMessage("Dinheiro insuficiente"); return; }
    money -= cost;
    t->invested += cost;
    t->level++;
}

static void SellSelected(void)
{
    if (selC < 0) return;
    Tower *t = &towers[selR][selC];
    money += SellValue(t);
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
    if (spawn2C >= 0 && wave == SECOND_ENTRANCE_WAVE) ShowMessage("NOVA ENTRADA ABERTA! Inimigos vem pelos dois lados");
    selBuild = -1;   /* so da para plantar entre as ondas */
    spawnTimer = 0;
}

static void SpawnEnemy(void)
{
    for (int i = 0; i < MAX_ENEMIES; i++) {
        if (enemies[i].active) continue;
        Enemy *e = &enemies[i];
        float baseHp = ENEMY_BASE_HP * powf(HP_GROWTH, (float)(wave - 1));

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
    if (e->hp <= 0) {
        e->active = false;
        money += e->reward;
        SpawnFloat(e->pos, TextFormat("+$%d", e->reward), (Color){ 255, 214, 80, 255 });
        if (e->kind == ENEMY_TANK) {
            AddSeeds(SEEDS_PER_TANK);
            ShowMessage(TextFormat("Tanque destruido! +%d sementes", SEEDS_PER_TANK));
        }
    }
}

static void UpdateEnemies(float dt)
{
    for (int i = 0; i < MAX_ENEMIES; i++) {
        Enemy *e = &enemies[i];
        if (!e->active) continue;

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
                if (lives <= 0) { lives = 0; state = STATE_GAMEOVER; }
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
            .damage = TowerDamage(t), .slow = TOWER_INFO[t->kind].slow,
            .speed = TOWER_INFO[t->kind].projSpeed, .size = TOWER_INFO[t->kind].projSize,
            .splash = TOWER_INFO[t->kind].splash, .poisonDps = TowerPoison(t),
            .poisonPct = TOWER_INFO[t->kind].poisonPct * (1.0f + UPG_DAMAGE * (t->level - 1)),
            .color = TOWER_INFO[t->kind].color,
        };
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
        if (d <= step + e->radius) {
            if (p->splash > 0) {
                /* Dano em area: atinge todos os inimigos dentro do raio */
                Vector2 hit = e->pos;
                for (int j = 0; j < MAX_ENEMIES; j++)
                    if (enemies[j].active && Vector2Distance(hit, enemies[j].pos) <= p->splash + enemies[j].radius)
                        ApplyHit(&enemies[j], p);
                SpawnEffect(hit, p->splash, ORANGE);
            } else {
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
    if (wave >= MAX_WAVES) state = STATE_VICTORY;
    else if (spawn2C >= 0 && wave == SECOND_ENTRANCE_WAVE - 1)
        ShowMessage("Atencao: na proxima onda abre uma NOVA ENTRADA!");
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
            ShowMessage("So da para plantar quando a onda acabar");
        } else if (!unlocked[i]) {
            ShowMessage(TextFormat("%s bloqueada - compre na Loja [L]", TOWER_INFO[i].name));
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
            if (money < TOWER_INFO[selBuild].cost) ShowMessage("Dinheiro insuficiente");
            else if (!CanPlace(c, r, &reason))     ShowMessage(reason);
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
    unsigned int h = (unsigned int)(a * 73856093) ^ (unsigned int)(b * 19349663);
    h ^= h >> 13; h *= 0x5bd1e995u; h ^= h >> 15;
    return h;
}

static void DrawPanel(Rectangle r, Color fill, Color border)
{
    DrawRectangleRounded((Rectangle){ r.x + 3, r.y + 5, r.width, r.height }, 0.08f, 8, Fade(BLACK, 0.35f));
    DrawRectangleRounded(r, 0.08f, 8, fill);
    DrawRectangleRoundedLinesEx(r, 0.08f, 8, 2, border);
}

static void DrawTextShadow(const char *t, int x, int y, int fs, Color c)
{
    DrawText(t, x + 2, y + 2, fs, Fade(BLACK, 0.5f * c.a / 255.0f));
    DrawText(t, x, y, fs, c);
}

static void DrawCentered(const char *text, int y, int fs, Color col)
{
    DrawTextShadow(text, SCREEN_W / 2 - MeasureText(text, fs) / 2, y, fs, col);
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
    int w = MeasureText(key, 14) + 14;
    DrawRectangleRounded((Rectangle){ (float)x, (float)y, (float)w, 22 }, 0.35f, 6, (Color){ 58, 66, 60, 255 });
    DrawRectangleRounded((Rectangle){ (float)x, (float)y, (float)w, 19 }, 0.35f, 6, (Color){ 96, 110, 98, 255 });
    DrawText(key, x + 7, y + 3, 14, UI_TEXT);
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
            DrawText(line1, (int)(r.x + (r.width - MeasureText(line1, fs)) / 2), (int)(r.y + r.height / 2 - fs - 2), fs, fg);
            DrawText(line2, (int)(r.x + (r.width - MeasureText(line2, fs)) / 2), (int)(r.y + r.height / 2 + 2), fs, enabled ? UI_ACCENT : fg);
        } else {
            DrawText(line1, (int)(r.x + (r.width - MeasureText(line1, fs)) / 2), (int)(r.y + (r.height - fs) / 2), fs, fg);
        }
    }
    return enabled && hover && IsMouseButtonPressed(MOUSE_BUTTON_LEFT);
}

/* ---------- Mapa ---------- */
static bool IsRoadAt(int c, int r) { return InBounds(c, r) && grid[r][c] == TILE_ROAD; }

static void DrawGrassTile(int c, int r)
{
    int v = (int)(Hash2(c, r) % 9) - 4;
    Color g = ((r + c) % 2 == 0) ? (Color){ (unsigned char)(98 + v), (unsigned char)(162 + v), (unsigned char)(82 + v), 255 }
                                 : (Color){ (unsigned char)(90 + v), (unsigned char)(152 + v), (unsigned char)(76 + v), 255 };
    DrawRectangle(c * TILE, r * TILE, TILE, TILE, g);
}

static void DrawGrassDecor(int c, int r)
{
    unsigned int h = Hash2(c + 101, r + 37);
    int x = c * TILE, y = r * TILE;
    Color blade = { 62, 124, 54, 255 };
    for (int i = 0; i < 2; i++) {     /* tufos de capim */
        int tx = x + 8 + (int)((h >> (i * 5)) % 30), ty = y + 14 + (int)((h >> (i * 7 + 3)) % 26);
        DrawLine(tx, ty, tx - 3, ty - 6, blade);
        DrawLine(tx, ty, tx, ty - 8, blade);
        DrawLine(tx, ty, tx + 3, ty - 6, blade);
    }
    if (h % 6 == 0) {                 /* florzinha */
        int fx = x + 10 + (int)((h >> 9) % 28), fy = y + 10 + (int)((h >> 13) % 28);
        Color pet = (h % 3 == 0) ? (Color){ 250, 250, 250, 255 }
                  : (h % 3 == 1) ? (Color){ 255, 214, 90, 255 } : (Color){ 240, 140, 190, 255 };
        DrawCircle(fx + 3, fy, 2.5f, pet); DrawCircle(fx - 3, fy, 2.5f, pet);
        DrawCircle(fx, fy + 3, 2.5f, pet); DrawCircle(fx, fy - 3, 2.5f, pet);
        DrawCircle(fx, fy, 1.8f, (Color){ 230, 160, 40, 255 });
    }
}

static void DrawRoadTile(int c, int r, bool closed)
{
    int x = c * TILE, y = r * TILE;
    Color dirt = { 188, 152, 104, 255 }, edge = { 142, 110, 72, 255 };
    DrawRectangle(x, y, TILE, TILE, dirt);
    if (!IsRoadAt(c, r - 1)) DrawRectangle(x, y, TILE, 4, edge);
    if (!IsRoadAt(c, r + 1)) DrawRectangle(x, y + TILE - 4, TILE, 4, edge);
    if (!IsRoadAt(c - 1, r)) DrawRectangle(x, y, 4, TILE, edge);
    if (!IsRoadAt(c + 1, r)) DrawRectangle(x + TILE - 4, y, 4, TILE, edge);

    unsigned int h = Hash2(c * 7 + 3, r * 11 + 5);
    for (int i = 0; i < 3; i++) {     /* pedrinhas */
        int px = x + 9 + (int)((h >> (i * 6)) % 30), py = y + 9 + (int)((h >> (i * 6 + 3)) % 30);
        DrawCircle(px, py, 1.5f + (float)((h >> (i + 20)) % 2), (Color){ 160, 128, 88, 255 });
    }

    if (closed) {                     /* estrada ainda fechada: coberta de mato */
        DrawRectangle(x, y, TILE, TILE, (Color){ 86, 140, 70, 210 });
        Color blade = { 54, 110, 46, 255 };
        for (int i = 0; i < 6; i++) {
            int bx = x + 4 + i * 8, by = y + 32 + (int)((h >> i) % 12);
            DrawLine(bx, by, bx - 2, by - 13, blade);
            DrawLine(bx + 2, by, bx + 4, by - 10, blade);
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
    int w = MeasureText(text, 10) + 10;
    Rectangle r = { center.x - w / 2.0f, center.y - 7, (float)w, 14 };
    DrawRectangleRounded(r, 0.3f, 4, board);
    DrawRectangleRoundedLinesEx(r, 0.3f, 4, 1, Fade(BLACK, 0.4f));
    DrawText(text, (int)r.x + 5, (int)r.y + 2, 10, RAYWHITE);
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

    for (int r = 0; r < ROWS; r++) {
        for (int c = 0; c < COLS; c++) {
            if (grid[r][c] == TILE_ROAD)       DrawRoadTile(c, r, branchRoad[r][c] && !SecondEntranceOpen());
            else if (grid[r][c] == TILE_ROCK)  DrawRock(c, r);
            else if (grid[r][c] == TILE_EMPTY) DrawGrassDecor(c, r);
        }
    }

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
                DrawCircleV(Vector2Lerp(a, b, u), 2.0f, Fade(WHITE, 0.35f));
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
                    DrawText(TextFormat("%d", dist[rr][cc]), cc * TILE + 4, rr * TILE + 4, 10, Fade(BLACK, 0.7f));
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
    if (selC >= 0) {
        Tower *t = &towers[selR][selC];
        Vector2 ctr = TileCenter(selC, selR);
        DrawCircleV(ctr, TowerRange(t), Fade(WHITE, 0.10f));
        DrawCircleLinesV(ctr, TowerRange(t), Fade(WHITE, 0.8f));
    }

    for (int r = 0; r < ROWS; r++) {
        for (int c = 0; c < COLS; c++) {
            Tower *t = &towers[r][c];
            if (!t->active) continue;
            DrawPlant(t->kind, TileCenter(c, r), 1.0f, t->angle, false);
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

static void DrawProjectiles(void)
{
    for (int i = 0; i < MAX_PROJ; i++)
        if (projs[i].active) {
            DrawCircleV(projs[i].pos, projs[i].size + 2, Fade(projs[i].color, 0.3f));
            DrawCircleV(projs[i].pos, projs[i].size, projs[i].color);
        }

    for (int i = 0; i < MAX_FX; i++)
        if (effects[i].active) {
            float a = effects[i].life / effects[i].maxLife;
            DrawCircleV(effects[i].pos, effects[i].radius * (1.2f - 0.4f * a), Fade(effects[i].color, 0.40f * a));
            DrawRing(effects[i].pos, effects[i].radius * (1.2f - 0.4f * a) - 3, effects[i].radius * (1.2f - 0.4f * a), 0, 360, 32, Fade(YELLOW, 0.6f * a));
        }

    for (int i = 0; i < MAX_FLOAT; i++)
        if (floats[i].active) {
            int w = MeasureText(floats[i].text, 16);
            DrawTextShadow(floats[i].text, (int)floats[i].pos.x - w / 2, (int)floats[i].pos.y - 20, 16, Fade(floats[i].color, floats[i].life));
        }
}

/* ---------- Interface ---------- */
static void DrawToast(int y)
{
    if (msgTimer <= 0) return;
    float a = msgTimer < 0.3f ? msgTimer / 0.3f : 1.0f;
    int fs = 20, w = MeasureText(msgText, fs);
    Rectangle r = { SCREEN_W / 2.0f - w / 2.0f - 16, (float)y, w + 32.0f, fs + 16.0f };
    DrawRectangleRounded((Rectangle){ r.x + 2, r.y + 4, r.width, r.height }, 0.45f, 8, Fade(BLACK, 0.35f * a));
    DrawRectangleRounded(r, 0.45f, 8, Fade((Color){ 25, 32, 27, 255 }, 0.94f * a));
    DrawRectangleRoundedLinesEx(r, 0.45f, 8, 2, Fade(UI_ACCENT, a));
    DrawText(msgText, (int)r.x + 16, (int)r.y + 8, fs, Fade(UI_TEXT, a));
}

static void DrawSpecialLine(const TowerInfo *ti, float poisonScale, int x, int y, int fs)
{
    if (ti->splash > 0)    DrawText(TextFormat("Dano em area: %.0f px", ti->splash), x, y, fs, ORANGE);
    if (ti->slow > 0)      DrawText(TextFormat("Deixa lento: -%.0f%% velocidade", ti->slow * 100), x, y, fs, SKYBLUE);
    if (ti->poisonDps > 0) DrawText(TextFormat("Veneno: %.0f + %.0f%% da vida/s", ti->poisonDps * poisonScale, ti->poisonPct * 100 * poisonScale), x, y, fs, LIME);
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
    DrawText(ti->name, (int)x + 44, (int)y + 10, 20, UI_TEXT);
    DrawText(TextFormat("Nivel %d/%d", t->level, MAX_LEVEL), (int)x + 44, (int)y + 32, 12, UI_MUTED);
    DrawLevelPips(x + 124, y + 38, t->level);

    float scale = 1.0f + UPG_DAMAGE * (t->level - 1);
    DrawText(TextFormat("Dano %.0f   Alcance %.0f", TowerDamage(t), TowerRange(t)), (int)x + 10, (int)y + 54, 12, UI_TEXT);
    DrawText(TextFormat("Tiros/s %.1f", TowerFireRate(t)), (int)x + 10, (int)y + 70, 12, UI_TEXT);
    DrawSpecialLine(ti, scale, (int)x + 10, (int)y + 86, 12);
    const char *next = t->level >= MAX_LEVEL ? "Nivel maximo"
                     : !upgradesUnlocked ? TextFormat("Melhorias na onda %d", UPGRADE_UNLOCK_WAVE)
                     : TextFormat("Proximo nivel: $%d", UpgradeCost(t));
    DrawText(next, (int)x + 10, (int)y + 100, 12, UI_ACCENT);
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
    DrawText(ti->name, (int)x + 48, (int)tip.y + 10, 20, UI_TEXT);
    DrawCoin((Vector2){ x + 56, tip.y + 41 }, 6);
    DrawText(TextFormat("%d", ti->cost), (int)x + 66, (int)tip.y + 35, 14, UI_ACCENT);
    DrawText(ti->desc, (int)x + 10, (int)tip.y + 56, 12, UI_MUTED);
    DrawText(TextFormat("Dano %.0f   Alcance %.0f   Tiros/s %.1f", ti->damage, ti->range, ti->fireRate), (int)x + 10, (int)tip.y + 74, 12, UI_TEXT);
    if (!unlocked[i]) DrawText(TextFormat("Bloqueada: %d sementes na loja [L]", ti->seedPrice), (int)x + 10, (int)tip.y + 92, 12, (Color){ 240, 140, 120, 255 });
    else              DrawSpecialLine(ti, 1.0f, (int)x + 10, (int)tip.y + 92, 12);
}

static void DrawUI(void)
{
    bool live = (state == STATE_PLAYING);
    int y0 = GRID_H;
    Vector2 mouse = GetMousePosition();

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
        DrawText(TextFormat("%d", i + 1), (int)rec.x + 7, (int)rec.y + 6, 12, UI_MUTED);
        Vector2 icon = { rec.x + rec.width / 2, rec.y + 32 };
        DrawPlant((TowerKind)i, icon, 0.68f, 0, !unlocked[i]);
        const char *name = TOWER_INFO[i].name;
        DrawText(name, (int)(rec.x + (rec.width - MeasureText(name, 14)) / 2), (int)rec.y + 60, 14, txt);

        if (!unlocked[i]) {
            DrawLockIcon(icon, 1.1f, UI_TEXT);
            const char *l = "Loja";
            if (!waveActive) DrawText(l, (int)(rec.x + (rec.width - MeasureText(l, 14)) / 2), (int)rec.y + 80, 14, (Color){ 240, 150, 120, 255 });
            if (live && CheckCollisionPointRec(mouse, rec) && IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) OpenShop();
        } else if (!waveActive) {
            const char *price = TextFormat("%d", TOWER_INFO[i].cost);
            int pw = MeasureText(price, 14), total = 16 + pw;
            float px = rec.x + (rec.width - total) / 2;
            DrawCoin((Vector2){ px + 6, rec.y + 87 }, 6);
            DrawText(price, (int)px + 16, (int)rec.y + 80, 14, money >= TOWER_INFO[i].cost ? UI_ACCENT : (Color){ 230, 110, 100, 255 });
        }
    }

    if (live && waveActive) {
        const char *aviso = "Plante quando a onda acabar";
        Rectangle b = { 172, y0 + 84.0f, 488, 24 };
        DrawRectangleRounded(b, 0.5f, 8, Fade((Color){ 20, 24, 20, 255 }, 0.92f));
        DrawRectangleRoundedLinesEx(b, 0.5f, 8, 1.5f, ORANGE);
        DrawText(aviso, (int)(b.x + (b.width - MeasureText(aviso, 16)) / 2), (int)b.y + 4, 16, ORANGE);
    }

    /* Acoes */
    float rx = 676;
    if (DrawButton((Rectangle){ rx, y0 + 8.0f, 135, 34 },
                   waveActive ? "Onda em curso" : "Onda [Espaco]", NULL, live && !waveActive, !waveActive && live))
        StartWave();
    if (DrawButton((Rectangle){ rx + 145, y0 + 8.0f, 135, 34 },
                   TextFormat("Velocidade %dx [F]", gameSpeed), NULL, live, gameSpeed == 2))
        gameSpeed = (gameSpeed == 1) ? 2 : 1;

    if (selC >= 0) {
        Tower *t = &towers[selR][selC];
        const char *up = !upgradesUnlocked ? TextFormat("Melhorar: onda %d", UPGRADE_UNLOCK_WAVE)
                       : (t->level >= MAX_LEVEL) ? "Nivel maximo"
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
        DrawText("Clique numa planta para", (int)rx + 10, y0 + 50, 13, UI_MUTED);
        DrawText(upgradesUnlocked ? "ver detalhes, melhorar ou vender" : "ver detalhes ou vender",
                 (int)rx + 10, y0 + 64, 13, UI_MUTED);
    }

    if (DrawButton((Rectangle){ rx, y0 + 84.0f, 280, 34 }, "Loja de plantas [L]", NULL, live, false))
        OpenShop();

    if (live && hovered >= 0) DrawPlantTooltip(hovered, 170.0f + hovered * 82, y0);
    if (live) DrawToast(12);
}

static void DrawShop(void)
{
    DrawCentered("LOJA DE PLANTAS", 26, 44, UI_ACCENT);
    const char *sv = TextFormat("%d sementes", seeds);
    int sw = MeasureText(sv, 24);
    DrawSeedIcon((Vector2){ SCREEN_W / 2.0f - sw / 2.0f - 16, 92 }, 11);
    DrawTextShadow(sv, SCREEN_W / 2 - sw / 2 + 4, 80, 24, (Color){ 232, 200, 150, 255 });
    DrawCentered(TextFormat("Ganhe sementes: +%d por onda, +%d extra em onda de chefe, +%d por tanque destruido",
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
        DrawText(TextFormat("Tecla %d", i + 1), x + 70, y + 42, 12, UI_MUTED);
        DrawText(ti->desc, x + 14, y + 70, 13, UI_MUTED);
        DrawCoin((Vector2){ x + 20, y + 98.0f }, 6);
        DrawText(TextFormat("Plantar: $%d", ti->cost), x + 32, y + 92, 14, UI_ACCENT);
        DrawText(TextFormat("Dano %.0f   Alcance %.0f   Tiros/s %.1f", ti->damage, ti->range, ti->fireRate), x + 14, y + 114, 13, UI_TEXT);
        DrawSpecialLine(ti, 1.0f, x + 14, y + 134, 13);

        Rectangle btn = { card.x + 14, card.y + 164, 252, 34 };
        if (unlocked[i]) {
            DrawRectangleRounded(btn, 0.3f, 8, (Color){ 34, 78, 40, 255 });
            const char *ok = "LIBERADA";
            DrawText(ok, (int)(btn.x + (btn.width - MeasureText(ok, 18)) / 2), (int)btn.y + 8, 18, (Color){ 140, 230, 110, 255 });
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
        const char *lbl = TextFormat("Nivel %d", lv + 1);
        DrawText(lbl, (int)p.x - MeasureText(lbl, 18) / 2, 300, 18, UI_TEXT);
        if (lv < 2) DrawText(">", (int)(xs[lv] + 82), 212, 30, UI_ACCENT);
    }

    const TowerInfo *ex = &TOWER_INFO[TOWER_BASIC];
    const char *steps[] = {
        "Clique em uma planta que ja esta no mapa para seleciona-la.",
        "Clique em \"Melhorar\" (ou aperte U) para subir de nivel.",
        TextFormat("Cada nivel: +%.0f%% de dano, +%.0f%% de alcance e +%.0f%% de tiros/s.",
                   UPG_DAMAGE * 100, UPG_RANGE * 100, UPG_FIRERATE * 100),
        TextFormat("Custo: nivel 2 = preco da planta, nivel 3 = 2x (%s: $%d e $%d).", ex->name, ex->cost, ex->cost * 2),
        "O nivel maximo e 3 - as bolinhas douradas mostram o nivel.",
    };
    for (int i = 0; i < 5; i++) {
        int y = 338 + i * 32;
        DrawCircle(160, y + 9, 11, UI_BORDER);
        DrawText(TextFormat("%d", i + 1), 156, y + 2, 16, UI_TEXT);
        DrawText(steps[i], 182, y, 18, UI_TEXT);
    }
    DrawText("Dica: vender (S) devolve 70% de tudo o que voce gastou na planta,", 150, 510, 16, (Color){ 232, 200, 150, 255 });
    DrawText("incluindo as melhorias.", 150, 530, 16, (Color){ 232, 200, 150, 255 });

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

    DrawText("CONTROLES", 200, 186, 16, UI_MUTED);
    struct { const char *k; const char *d; } rows[] = {
        { "1 - 6",    "Escolher planta" },     { "Clique",     "Plantar / selecionar" },
        { "U",        "Melhorar" },            { "S",          "Vender" },
        { "Espaco",   "Proxima onda" },        { "L",          "Loja de plantas" },
        { "F",        "Velocidade 1x / 2x" },  { "P / Esc",    "Pausar" },
        { "D",        "Mapa da BFS" },         { "Botao dir.", "Cancelar" },
    };
    rows[2].d = TextFormat("Melhorar (onda %d+)", UPGRADE_UNLOCK_WAVE);
    for (int i = 0; i < 10; i++) {
        int x = 200 + (i % 2) * 290, y = 210 + (i / 2) * 30;
        DrawKeyChip(rows[i].k, x, y);
        DrawText(rows[i].d, x + 104, y + 3, 16, UI_TEXT);
    }

    DrawText("DICAS", 200, 370, 16, UI_MUTED);
    const char *tips[] = {
        "As pragas seguem a estrada - plante na grama, perto das curvas.",
        "So da para plantar quando a onda acaba.",
        TextFormat("Na onda %d abre uma segunda entrada!", SECOND_ENTRANCE_WAVE),
        "Ganhe sementes e libere novas plantas na loja (o progresso fica salvo).",
    };
    for (int i = 0; i < 4; i++) {
        int y = 396 + i * 28;
        DrawCircle(208, y + 8, 4, (Color){ 120, 200, 90, 255 });
        DrawText(tips[i], 222, y, 16, UI_TEXT);
    }

    if (DrawButton((Rectangle){ SCREEN_W / 2.0f - 230, 534, 220, 52 }, "Jogar [ENTER]", NULL, true, true))
        state = STATE_PLAYING;
    if (DrawButton((Rectangle){ SCREEN_W / 2.0f + 10, 534, 220, 52 }, "Loja [L]", NULL, true, false))
        OpenShop();
    DrawCentered("Feito em C com raylib", 604, 12, UI_MUTED);
}

static void DrawEndScreen(bool victory)
{
    Rectangle pnl = { SCREEN_W / 2.0f - 250, 160, 500, 340 };
    DrawPanel(pnl, (Color){ 30, 42, 32, 248 }, victory ? UI_ACCENT : (Color){ 200, 70, 60, 255 });
    if (victory) {
        DrawPlant(TOWER_LASER, (Vector2){ pnl.x + 60, pnl.y + 60 }, 1.5f, (float)GetTime(), false);
        DrawPlant(TOWER_LASER, (Vector2){ pnl.x + pnl.width - 60, pnl.y + 60 }, 1.5f, -(float)GetTime(), false);
        DrawCentered("VITORIA!", (int)pnl.y + 30, 52, UI_ACCENT);
        DrawCentered("O jardim esta a salvo!", (int)pnl.y + 92, 20, UI_TEXT);
    } else {
        DrawCentered("FIM DE JOGO", (int)pnl.y + 30, 52, (Color){ 240, 90, 80, 255 });
        DrawCentered("As pragas chegaram na casinha...", (int)pnl.y + 92, 20, UI_TEXT);
    }

    int y = (int)pnl.y + 140;
    DrawFlagIcon((Vector2){ pnl.x + 150, y + 10.0f }, 9);
    DrawText(TextFormat("Onda alcancada: %d/%d", wave, MAX_WAVES), (int)pnl.x + 170, y, 20, UI_TEXT);
    DrawHeart((Vector2){ pnl.x + 150, y + 42.0f }, 9, (Color){ 232, 70, 70, 255 });
    DrawText(TextFormat("Vidas restantes: %d", lives), (int)pnl.x + 170, y + 32, 20, UI_TEXT);
    DrawSeedIcon((Vector2){ pnl.x + 150, y + 74.0f }, 9);
    DrawText(TextFormat("Sementes guardadas: %d", seeds), (int)pnl.x + 170, y + 64, 20, UI_TEXT);

    if (DrawButton((Rectangle){ SCREEN_W / 2.0f - 130, pnl.y + 260, 260, 50 }, "Jogar de novo [ENTER]", NULL, true, true)) {
        ResetGame();
        state = STATE_PLAYING;
    }
}

static void DrawOverlay(void)
{
    if (state == STATE_PLAYING) return;
    DrawRectangle(0, 0, SCREEN_W, SCREEN_H, Fade(BLACK, state == STATE_SHOP ? 0.82f : 0.6f));

    switch (state) {
        case STATE_MENU:     DrawMenu(); break;
        case STATE_SHOP:     DrawShop(); break;
        case STATE_TUTORIAL: DrawUpgradeTutorial(); break;
        case STATE_PAUSED: {
            DrawPanel((Rectangle){ SCREEN_W / 2.0f - 200, 230, 400, 210 }, (Color){ 30, 42, 32, 248 }, UI_BORDER);
            DrawCentered("PAUSADO", 256, 48, UI_TEXT);
            if (DrawButton((Rectangle){ SCREEN_W / 2.0f - 110, 330, 220, 46 }, "Continuar [P]", NULL, true, true))
                state = STATE_PLAYING;
            DrawCentered("Esc tambem continua", 396, 14, UI_MUTED);
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
    LoadProgress();
    ResetGame();

    while (!WindowShouldClose()) {
        float dt = GetFrameTime();
        if (dt > 0.05f) dt = 0.05f;
        if (msgTimer > 0) msgTimer -= dt;

        switch (state) {
            case STATE_MENU:
                if (IsKeyPressed(KEY_ENTER)) state = STATE_PLAYING;
                else if (IsKeyPressed(KEY_L)) OpenShop();
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
            case STATE_VICTORY:
                if (IsKeyPressed(KEY_ENTER)) { ResetGame(); state = STATE_PLAYING; }
                break;
        }

        BeginDrawing();
        ClearBackground(BLACK);
        DrawMap();
        DrawBuildPreview();
        DrawTowers();
        DrawEnemies();
        DrawProjectiles();
        DrawUI();
        DrawOverlay();
        EndDrawing();
    }

    CloseWindow();
    return 0;
}
