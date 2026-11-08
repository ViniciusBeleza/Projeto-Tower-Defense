/*
 * Tower Defense - raylib (C)
 *
 * Conceitos usados:
 *  - Game loop com maquina de estados (menu, jogando, pausado, fim)
 *  - Grid / tilemap carregado de um mapa em texto
 *  - BFS (busca em largura) gerando um "mapa de distancias" ate a base
 *    -> os inimigos sempre andam para o vizinho mais perto da base
 *    -> o jogador nao pode construir torres que bloqueiem o caminho
 *  - Pools fixos de entidades (inimigos e projeteis)
 *  - Projeteis teleguiados, torres com upgrade, ondas progressivas
 *
 * Controles:
 *  1/2/3      escolher torre para construir
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
#define UI_H        96
#define GRID_H      (ROWS * TILE)
#define SCREEN_W    (COLS * TILE)
#define SCREEN_H    (GRID_H + UI_H)

#define MAX_ENEMIES 256
#define MAX_PROJ    512
#define MAX_LEVEL   3
#define MAX_WAVES   20
#define INF         9999

#define START_MONEY 150
#define START_LIVES 20

/* Mapa: '.' livre  '#' pedra  'S' entrada dos inimigos  'B' base */
static const char *MAP[ROWS] = {
    "....................",
    "..#.......#.........",
    "..#.......#....#....",
    "..#...........#.....",
    "......##............",
    "S.....##.......#....",
    "...........#.......B",
    "....#......#........",
    "....#...............",
    "........#.....##....",
    "........#...........",
    "....................",
};

/* ------------------------------------------------------------------ */
/* Tipos                                                              */
/* ------------------------------------------------------------------ */
typedef enum { TILE_EMPTY, TILE_ROCK, TILE_TOWER } TileType;
typedef enum { STATE_MENU, STATE_PLAYING, STATE_PAUSED, STATE_GAMEOVER, STATE_VICTORY } GameState;
typedef enum { TOWER_BASIC, TOWER_SNIPER, TOWER_FROST, TOWER_COUNT } TowerKind;
typedef enum { ENEMY_NORMAL, ENEMY_FAST, ENEMY_TANK } EnemyKind;

typedef struct {
    const char *name;
    int   cost;
    float range;      /* pixels */
    float fireRate;   /* tiros por segundo */
    float damage;
    float projSpeed;
    float slow;       /* 0..1, quanto reduz a velocidade do alvo */
    Color color;
} TowerInfo;

static const TowerInfo TOWER_INFO[TOWER_COUNT] = {
    { "Basica", 50,  120.0f, 1.6f, 10.0f, 420.0f, 0.0f, { 0, 121, 241, 255 } },
    { "Sniper", 100, 260.0f, 0.5f, 45.0f, 900.0f, 0.0f, { 112, 31, 126, 255 } },
    { "Gelo",   75,  100.0f, 1.0f,  4.0f, 350.0f, 0.5f, { 102, 191, 255, 255 } },
};

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
    int     reward;
    float   radius;
    Color   color;
} Enemy;

typedef struct {
    bool    active;
    Vector2 pos;
    int     target, targetUid;
    float   damage, slow, speed;
    Color   color;
} Projectile;

/* ------------------------------------------------------------------ */
/* Estado global do jogo                                              */
/* ------------------------------------------------------------------ */
static TileType   grid[ROWS][COLS];
static int        dist[ROWS][COLS];       /* distancia (em tiles) ate a base */
static Tower      towers[ROWS][COLS];
static Enemy      enemies[MAX_ENEMIES];
static Projectile projs[MAX_PROJ];

static int spawnC, spawnR, baseC, baseR;

static GameState state = STATE_MENU;
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

static const int DC[4] = { 1, -1, 0, 0 };
static const int DR[4] = { 0, 0, 1, -1 };

static float TowerRange(const Tower *t)    { return TOWER_INFO[t->kind].range    * (1.0f + 0.15f * (t->level - 1)); }
static float TowerDamage(const Tower *t)   { return TOWER_INFO[t->kind].damage   * (1.0f + 0.60f * (t->level - 1)); }
static float TowerFireRate(const Tower *t) { return TOWER_INFO[t->kind].fireRate * (1.0f + 0.15f * (t->level - 1)); }
static int   UpgradeCost(const Tower *t)   { return TOWER_INFO[t->kind].cost * t->level; }
static int   SellValue(const Tower *t)     { return (int)(t->invested * 0.7f); }

/* ------------------------------------------------------------------ */
/* Mapa e BFS                                                         */
/* ------------------------------------------------------------------ */
static void LoadMap(void)
{
    for (int r = 0; r < ROWS; r++) {
        for (int c = 0; c < COLS; c++) {
            char ch = (c < (int)strlen(MAP[r])) ? MAP[r][c] : '.';
            grid[r][c] = TILE_EMPTY;
            if (ch == '#') grid[r][c] = TILE_ROCK;
            else if (ch == 'S') { spawnC = c; spawnR = r; }
            else if (ch == 'B') { baseC = c; baseR = r; }
        }
    }
}

/* BFS a partir da base: out[r][c] = numero de passos ate a base (INF = sem caminho).
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
            if (grid[nr][nc] != TILE_EMPTY) continue;
            if (out[nr][nc] != INF) continue;
            out[nr][nc] = out[r][c] + 1;
            qc[tail] = nc; qr[tail] = nr; tail++;
        }
    }
    return out[spawnR][spawnC] < INF;
}

/* Verifica se da para construir no tile sem bloquear o caminho */
static bool CanPlace(int c, int r, const char **reason)
{
    *reason = NULL;
    if (grid[r][c] != TILE_EMPTY) { *reason = "Tile ocupado"; return false; }
    if ((c == spawnC && r == spawnR) || (c == baseC && r == baseR)) { *reason = "Nao pode construir aqui"; return false; }

    for (int i = 0; i < MAX_ENEMIES; i++) {
        if (!enemies[i].active) continue;
        int ec = (int)(enemies[i].pos.x / TILE), er = (int)(enemies[i].pos.y / TILE);
        if ((ec == c && er == r) || (enemies[i].tc == c && enemies[i].tr == r)) {
            *reason = "Tem inimigo nesse tile"; return false;
        }
    }

    /* Simula a construcao e roda a BFS de novo */
    int tmp[ROWS][COLS];
    grid[r][c] = TILE_TOWER;
    bool ok = ComputeDistances(tmp);
    for (int i = 0; ok && i < MAX_ENEMIES; i++) {
        if (enemies[i].active && tmp[enemies[i].tr][enemies[i].tc] >= INF) ok = false;
    }
    grid[r][c] = TILE_EMPTY;

    if (!ok) *reason = "Nao pode bloquear o caminho!";
    return ok;
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
}

static void PlaceTower(int c, int r, TowerKind kind)
{
    grid[r][c] = TILE_TOWER;
    towers[r][c] = (Tower){ .active = true, .kind = kind, .level = 1,
                            .invested = TOWER_INFO[kind].cost, .cooldown = 0, .angle = 0 };
    money -= TOWER_INFO[kind].cost;
    ComputeDistances(dist);
}

static void UpgradeSelected(void)
{
    if (selC < 0) return;
    Tower *t = &towers[selR][selC];
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
    ComputeDistances(dist);
}

static void StartWave(void)
{
    if (waveActive || wave >= MAX_WAVES) return;
    wave++;
    waveActive = true;
    waveSize = 6 + wave * 2;
    spawnedInWave = 0;
    spawnTimer = 0;
}

static void SpawnEnemy(void)
{
    for (int i = 0; i < MAX_ENEMIES; i++) {
        if (enemies[i].active) continue;
        Enemy *e = &enemies[i];
        float baseHp = 18.0f * powf(1.22f, (float)(wave - 1));

        EnemyKind kind = ENEMY_NORMAL;
        if (wave % 5 == 0 && spawnedInWave == waveSize - 1) kind = ENEMY_TANK;
        else if (wave >= 3 && spawnedInWave % 3 == 2)       kind = ENEMY_FAST;

        *e = (Enemy){ 0 };
        e->active = true;
        e->uid = nextUid++;
        e->kind = kind;
        e->pos = TileCenter(spawnC, spawnR);
        e->tc = spawnC; e->tr = spawnR;
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

static void UpdateEnemies(float dt)
{
    for (int i = 0; i < MAX_ENEMIES; i++) {
        Enemy *e = &enemies[i];
        if (!e->active) continue;

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
            .speed = TOWER_INFO[t->kind].projSpeed, .color = TOWER_INFO[t->kind].color,
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
            e->hp -= p->damage;
            if (p->slow > 0) {
                e->slowFactor = 1.0f - p->slow;
                e->slowTimer = 1.5f;
            }
            if (e->hp <= 0) {
                e->active = false;
                money += e->reward;
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
    money += bonus;
    if (wave >= MAX_WAVES) state = STATE_VICTORY;
    else ShowMessage(TextFormat("Onda %d completa! +$%d", wave, bonus));
}

static void HandleInput(void)
{
    if (IsKeyPressed(KEY_ONE))   selBuild = (selBuild == 0) ? -1 : 0;
    if (IsKeyPressed(KEY_TWO))   selBuild = (selBuild == 1) ? -1 : 1;
    if (IsKeyPressed(KEY_THREE)) selBuild = (selBuild == 2) ? -1 : 2;
    if (selBuild >= 0 && (IsKeyPressed(KEY_ONE) || IsKeyPressed(KEY_TWO) || IsKeyPressed(KEY_THREE))) selC = selR = -1;

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
static bool DrawButton(Rectangle rec, const char *line1, const char *line2, bool enabled, bool highlighted)
{
    bool hover = CheckCollisionPointRec(GetMousePosition(), rec);
    Color bg = !enabled ? (Color){ 55, 55, 60, 255 }
             : highlighted ? (Color){ 60, 120, 60, 255 }
             : hover ? (Color){ 85, 85, 110, 255 }
             : (Color){ 60, 60, 80, 255 };
    DrawRectangleRec(rec, bg);
    DrawRectangleLinesEx(rec, 2, highlighted ? GOLD : (Color){ 150, 150, 160, 255 });

    Color fg = enabled ? RAYWHITE : GRAY;
    int fs = 16;
    if (line2) {
        DrawText(line1, (int)(rec.x + (rec.width - MeasureText(line1, fs)) / 2), (int)(rec.y + rec.height / 2 - fs - 2), fs, fg);
        DrawText(line2, (int)(rec.x + (rec.width - MeasureText(line2, fs)) / 2), (int)(rec.y + rec.height / 2 + 2), fs, enabled ? GOLD : GRAY);
    } else {
        DrawText(line1, (int)(rec.x + (rec.width - MeasureText(line1, fs)) / 2), (int)(rec.y + (rec.height - fs) / 2), fs, fg);
    }
    return enabled && hover && IsMouseButtonPressed(MOUSE_BUTTON_LEFT);
}

static void DrawMap(void)
{
    for (int r = 0; r < ROWS; r++) {
        for (int c = 0; c < COLS; c++) {
            Color grass = ((r + c) % 2 == 0) ? (Color){ 96, 160, 80, 255 } : (Color){ 88, 150, 74, 255 };
            DrawRectangle(c * TILE, r * TILE, TILE, TILE, grass);
            if (grid[r][c] == TILE_ROCK) {
                DrawRectangleRounded((Rectangle){ c * TILE + 4.0f, r * TILE + 4.0f, TILE - 8.0f, TILE - 8.0f }, 0.4f, 6, (Color){ 110, 110, 115, 255 });
                DrawRectangleRounded((Rectangle){ c * TILE + 10.0f, r * TILE + 8.0f, TILE - 22.0f, TILE - 26.0f }, 0.5f, 6, (Color){ 140, 140, 145, 255 });
            }
        }
    }

    /* Entrada e base */
    DrawRectangle(spawnC * TILE, spawnR * TILE, TILE, TILE, (Color){ 60, 40, 30, 255 });
    DrawText("IN", spawnC * TILE + 12, spawnR * TILE + 14, 20, RAYWHITE);
    DrawRectangle(baseC * TILE, baseR * TILE, TILE, TILE, (Color){ 40, 70, 160, 255 });
    DrawRectangleLines(baseC * TILE, baseR * TILE, TILE, TILE, GOLD);
    DrawText("BASE", baseC * TILE + 2, baseR * TILE + 16, 14, GOLD);

    /* Caminho atual: segue a BFS da entrada ate a base */
    int c = spawnC, r = spawnR;
    for (int steps = 0; steps < ROWS * COLS && dist[r][c] > 0 && dist[r][c] < INF; steps++) {
        int bc = c, br = r;
        for (int i = 0; i < 4; i++) {
            int nc = c + DC[i], nr = r + DR[i];
            if (InBounds(nc, nr) && dist[nr][nc] < dist[br][bc]) { bc = nc; br = nr; }
        }
        DrawLineEx(TileCenter(c, r), TileCenter(bc, br), 4, Fade(YELLOW, 0.35f));
        c = bc; r = br;
    }

    if (showDist) {
        for (int rr = 0; rr < ROWS; rr++)
            for (int cc = 0; cc < COLS; cc++)
                if (dist[rr][cc] < INF)
                    DrawText(TextFormat("%d", dist[rr][cc]), cc * TILE + 4, rr * TILE + 4, 10, Fade(BLACK, 0.7f));
    }
}

static void DrawTowers(void)
{
    for (int r = 0; r < ROWS; r++) {
        for (int c = 0; c < COLS; c++) {
            Tower *t = &towers[r][c];
            if (!t->active) continue;
            Vector2 ctr = TileCenter(c, r);
            Color col = TOWER_INFO[t->kind].color;

            DrawRectangle(c * TILE + 4, r * TILE + 4, TILE - 8, TILE - 8, (Color){ 50, 50, 60, 255 });
            DrawCircleV(ctr, 15, col);
            Vector2 tip = { ctr.x + cosf(t->angle) * 22, ctr.y + sinf(t->angle) * 22 };
            DrawLineEx(ctr, tip, 7, (Color){ 30, 30, 35, 255 });
            DrawCircleV(ctr, 8, ColorBrightness(col, 0.35f));
            for (int i = 0; i < t->level; i++)
                DrawCircle(c * TILE + 11 + i * 9, r * TILE + TILE - 9, 3, GOLD);
        }
    }

    if (selC >= 0) {
        Tower *t = &towers[selR][selC];
        DrawCircleLinesV(TileCenter(selC, selR), TowerRange(t), WHITE);
        DrawRectangleLinesEx((Rectangle){ selC * (float)TILE, selR * (float)TILE, TILE, TILE }, 2, GOLD);
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
    DrawRectangle(c * TILE, r * TILE, TILE, TILE, Fade(ok ? GREEN : RED, 0.45f));
    DrawCircleLinesV(TileCenter(c, r), TOWER_INFO[selBuild].range, Fade(WHITE, 0.8f));
}

static void DrawEnemies(void)
{
    for (int i = 0; i < MAX_ENEMIES; i++) {
        Enemy *e = &enemies[i];
        if (!e->active) continue;
        DrawCircleV(e->pos, e->radius, e->color);
        DrawCircleLinesV(e->pos, e->radius, BLACK);
        if (e->slowTimer > 0) DrawCircleLinesV(e->pos, e->radius + 3, SKYBLUE);

        float w = 26, frac = e->hp / e->maxHp;
        float x = e->pos.x - w / 2, y = e->pos.y - e->radius - 8;
        DrawRectangle((int)x, (int)y, (int)w, 4, (Color){ 40, 0, 0, 255 });
        DrawRectangle((int)x, (int)y, (int)(w * frac), 4, LIME);
    }
}

static void DrawProjectiles(void)
{
    for (int i = 0; i < MAX_PROJ; i++)
        if (projs[i].active) {
            DrawCircleV(projs[i].pos, 4, projs[i].color);
            DrawCircleLinesV(projs[i].pos, 4, BLACK);
        }
}

static void DrawUI(void)
{
    bool live = (state == STATE_PLAYING);
    int y0 = GRID_H;
    DrawRectangle(0, y0, SCREEN_W, UI_H, (Color){ 30, 30, 40, 255 });
    DrawLine(0, y0, SCREEN_W, y0, GOLD);

    DrawText(TextFormat("$ %d", money), 14, y0 + 10, 24, GOLD);
    DrawText(TextFormat("Vidas: %d", lives), 14, y0 + 40, 20, (Color){ 255, 110, 110, 255 });
    DrawText(TextFormat("Onda: %d/%d", wave, MAX_WAVES), 14, y0 + 66, 20, RAYWHITE);

    for (int i = 0; i < TOWER_COUNT; i++) {
        Rectangle rec = { 180.0f + i * 160, y0 + 16.0f, 150, 64 };
        bool can = live && money >= TOWER_INFO[i].cost;
        if (DrawButton(rec, TextFormat("%d - %s", i + 1, TOWER_INFO[i].name),
                       TextFormat("$%d", TOWER_INFO[i].cost), can, selBuild == i)) {
            selBuild = (selBuild == i) ? -1 : i;
            selC = selR = -1;
        }
    }

    float rx = 676;
    if (DrawButton((Rectangle){ rx, y0 + 10.0f, 135, 36 },
                   waveActive ? "Onda em curso" : "Onda [Espaco]", NULL, live && !waveActive, false))
        StartWave();
    if (DrawButton((Rectangle){ rx + 145, y0 + 10.0f, 135, 36 },
                   TextFormat("Velocidade %dx [F]", gameSpeed), NULL, live, gameSpeed == 2))
        gameSpeed = (gameSpeed == 1) ? 2 : 1;

    if (selC >= 0) {
        Tower *t = &towers[selR][selC];
        const char *up = (t->level >= MAX_LEVEL) ? "Nivel maximo" : TextFormat("Melhorar $%d [U]", UpgradeCost(t));
        if (DrawButton((Rectangle){ rx, y0 + 52.0f, 135, 36 }, up, NULL,
                       live && t->level < MAX_LEVEL && money >= UpgradeCost(t), false))
            UpgradeSelected();
        if (DrawButton((Rectangle){ rx + 145, y0 + 52.0f, 135, 36 },
                       TextFormat("Vender $%d [S]", SellValue(t)), NULL, live, false))
            SellSelected();
    } else {
        DrawText("Clique numa torre para", (int)rx + 4, y0 + 56, 14, LIGHTGRAY);
        DrawText("melhorar ou vender", (int)rx + 4, y0 + 72, 14, LIGHTGRAY);
    }

    if (msgTimer > 0) {
        int fs = 22, w = MeasureText(msgText, fs);
        DrawRectangle(SCREEN_W / 2 - w / 2 - 12, 12, w + 24, fs + 14, Fade(BLACK, 0.7f));
        DrawText(msgText, SCREEN_W / 2 - w / 2, 19, fs, RAYWHITE);
    }
}

static void DrawCentered(const char *text, int y, int fs, Color col)
{
    DrawText(text, SCREEN_W / 2 - MeasureText(text, fs) / 2, y, fs, col);
}

static void DrawOverlay(void)
{
    if (state == STATE_PLAYING) return;
    DrawRectangle(0, 0, SCREEN_W, SCREEN_H, Fade(BLACK, 0.7f));

    switch (state) {
        case STATE_MENU:
            DrawCentered("TOWER DEFENSE", 120, 56, GOLD);
            DrawCentered(TextFormat("Defenda a base por %d ondas", MAX_WAVES), 200, 24, RAYWHITE);
            DrawCentered("1 / 2 / 3 : escolher torre   |   Clique: construir / selecionar", 260, 18, LIGHTGRAY);
            DrawCentered("U: melhorar   S: vender   ESPACO: proxima onda", 290, 18, LIGHTGRAY);
            DrawCentered("F: velocidade   D: mapa da BFS   P/ESC: pausar   Botao dir: cancelar", 320, 18, LIGHTGRAY);
            DrawCentered("Voce pode montar o labirinto, mas nunca bloquear o caminho!", 370, 18, YELLOW);
            DrawCentered("Pressione ENTER para comecar", 450, 28, GREEN);
            break;
        case STATE_PAUSED:
            DrawCentered("PAUSADO", 260, 56, RAYWHITE);
            DrawCentered("P / ESC para continuar", 330, 22, LIGHTGRAY);
            break;
        case STATE_GAMEOVER:
            DrawCentered("FIM DE JOGO", 240, 56, RED);
            DrawCentered(TextFormat("Voce sobreviveu ate a onda %d", wave), 320, 24, RAYWHITE);
            DrawCentered("ENTER para jogar de novo", 380, 22, LIGHTGRAY);
            break;
        case STATE_VICTORY:
            DrawCentered("VITORIA!", 240, 56, GOLD);
            DrawCentered(TextFormat("Base defendida com %d vidas restantes", lives), 320, 24, RAYWHITE);
            DrawCentered("ENTER para jogar de novo", 380, 22, LIGHTGRAY);
            break;
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
    ResetGame();

    while (!WindowShouldClose()) {
        float dt = GetFrameTime();
        if (dt > 0.05f) dt = 0.05f;
        if (msgTimer > 0) msgTimer -= dt;

        switch (state) {
            case STATE_MENU:
                if (IsKeyPressed(KEY_ENTER)) state = STATE_PLAYING;
                break;
            case STATE_PLAYING:
                if (IsKeyPressed(KEY_P) || IsKeyPressed(KEY_ESCAPE)) { state = STATE_PAUSED; break; }
                HandleInput();
                for (int s = 0; s < gameSpeed && state == STATE_PLAYING; s++) {
                    UpdateWave(dt);
                    UpdateEnemies(dt);
                    UpdateTowers(dt);
                    UpdateProjectiles(dt);
                }
                break;
            case STATE_PAUSED:
                if (IsKeyPressed(KEY_P) || IsKeyPressed(KEY_ESCAPE)) state = STATE_PLAYING;
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
