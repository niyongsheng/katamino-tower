#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <stdbool.h>
#include <string.h>
#ifdef _OPENMP
#include <omp.h>
#endif

#define COLS 12
#define LAYERS 5
#define NUM_PENT 10
#define TOTAL_CELLS (LAYERS * COLS)
#define MAX_ATTEMPTS 2000

// exhaustive mode globals
static long long g_total_solutions = 0;
static long long g_limit = 0; // 0 = unlimited
static int g_exhaustive = 0;

typedef struct { int x, y; } Cell;
typedef struct { Cell c[5]; } Shape;

typedef struct {
    int piece_idx;
    bool flipped;
    int bc, bl;
    Cell cells[5];
} Placement;

// ─── 积木矩阵定义 (3x12 01 矩阵, 每块各 5 格, 上层→下层 row2→row0) ───
// 环形块 block_0~4: 仅 ring 内弧占用内环 12 列，外弧占用 bottom 行指定列
// block_0 ~ block_4 矩阵: 对应 bottom 行 1 个格, (此处 1 表示外弧位置)
//   block_0:     block_1:     block_2:     block_3:     block_4:
//   row2 1 1 0.. row2 1 0 1 0.. row2 1 0 0 1 0.. row2 1 0 0 0 1 0.. row2 1 0 0 0 0 1 0..
//   row1 0..     row1 0..     row1 0..     row1 0..     row1 0..
//   row0 0..     row0 0..     row0 0..     row0 0..     row0 0..
//
// 五联骨牌 block_I,S,F,Q,N,L,T,W,U,Y (3x12):
//         I       S       F       Q       N       L       T       W       U       Y
// row2: 11111    0110    010    000    000    000    1110    001    000    000
//               010     111    011    011    001    010    010    101    010
// row0:        110     100    111    111    1111   010    110    111    1111
// (每个 1 表示该列该层被占用一格)

static const int BLOCK_MATRIX[15][3][12] = {
    // Rings block_0~4 (外弧位置)
    {{0},{0},{1,1,0,0,0,0,0,0,0,0,0,0}},
    {{0},{0},{1,0,1,0,0,0,0,0,0,0,0,0}},
    {{0},{0},{1,0,0,1,0,0,0,0,0,0,0,0}},
    {{0},{0},{1,0,0,0,1,0,0,0,0,0,0,0}},
    {{0},{0},{1,0,0,0,0,1,0,0,0,0,0,0}},
    // I
    {{0,0,0,0,0,0,0,0,0,0,0,0},
     {0,0,0,0,0,0,0,0,0,0,0,0},
     {1,1,1,1,1,0,0,0,0,0,0,0}},
    // S
    {{0,1,1,0,0,0,0,0,0,0,0,0},
     {0,1,0,0,0,0,0,0,0,0,0,0},
     {1,1,0,0,0,0,0,0,0,0,0,0}},
    // F
    {{0,1,0,0,0,0,0,0,0,0,0,0},
     {1,1,1,0,0,0,0,0,0,0,0,0},
     {1,0,0,0,0,0,0,0,0,0,0,0}},
    // Q
    {{0,0,0,0,0,0,0,0,0,0,0,0},
     {0,1,1,0,0,0,0,0,0,0,0,0},
     {1,1,1,0,0,0,0,0,0,0,0,0}},
    // N
    {{0,0,0,0,0,0,0,0,0,0,0,0},
     {0,0,1,1,0,0,0,0,0,0,0,0},
     {1,1,1,0,0,0,0,0,0,0,0,0}},
    // L
    {{0,0,0,0,0,0,0,0,0,0,0,0},
     {0,0,0,1,0,0,0,0,0,0,0,0},
     {1,1,1,1,0,0,0,0,0,0,0,0}},
    // T
    {{1,1,1,0,0,0,0,0,0,0,0,0},
     {0,1,0,0,0,0,0,0,0,0,0,0},
     {0,1,0,0,0,0,0,0,0,0,0,0}},
    // W
    {{0,0,1,0,0,0,0,0,0,0,0,0},
     {0,1,1,0,0,0,0,0,0,0,0,0},
     {1,1,0,0,0,0,0,0,0,0,0,0}},
    // U
    {{0,0,0,0,0,0,0,0,0,0,0,0},
     {1,0,1,0,0,0,0,0,0,0,0,0},
     {1,1,1,0,0,0,0,0,0,0,0,0}},
    // Y
    {{0,0,0,0,0,0,0,0,0,0,0,0},
     {0,0,1,0,0,0,0,0,0,0,0,0},
     {1,1,1,1,0,0,0,0,0,0,0,0}},
};

// ─── pentomino shapes (x=column offset, y=layer offset) ───
static const Shape SHAPES[NUM_PENT] = {
    {{{0,0},{1,0},{2,0},{3,0},{4,0}}}, // I
    {{{3,1},{0,2},{1,2},{2,2},{3,2}}}, // L
    {{{1,0},{2,0},{1,1},{0,2},{1,2}}}, // S
    {{{0,0},{1,0},{2,0},{1,1},{1,2}}}, // T
    {{{1,0},{0,1},{1,1},{2,1},{0,2}}}, // F
    {{{2,0},{1,1},{2,1},{0,2},{1,2}}}, // W
    {{{1,1},{2,1},{0,2},{1,2},{2,2}}}, // Q
    {{{0,1},{2,1},{0,2},{1,2},{2,2}}}, // U
    {{{2,1},{3,1},{0,2},{1,2},{2,2}}}, // N
    {{{2,1},{0,2},{1,2},{2,2},{3,2}}}, // Y
};

static const char *PIECE_NAMES[NUM_PENT] = {
    "I", "L", "S", "T", "F", "W", "Q", "U", "N", "Y"
};

// 5 ring pieces: each occupies inner ring (all 12 cols) + specific outer columns
static const int RING_OUTER[LAYERS][2] = {
    {0, 1}, {1, 3}, {2, 5}, {3, 7}, {4, 9}
};

// ─── helpers ───
Shape flip_shape_180(Shape s) {
    int min_x = 99, max_x = -99, min_y = 99, max_y = -99;
    for (int i = 0; i < 5; i++) {
        if (s.c[i].x < min_x) min_x = s.c[i].x;
        if (s.c[i].x > max_x) max_x = s.c[i].x;
        if (s.c[i].y < min_y) min_y = s.c[i].y;
        if (s.c[i].y > max_y) max_y = s.c[i].y;
    }
    float cx = (min_x + max_x) / 2.0f;
    float cy = (min_y + max_y) / 2.0f;
    Shape r;
    for (int i = 0; i < 5; i++) {
        r.c[i].x = (int)(2 * cx - s.c[i].x);
        r.c[i].y = (int)(2 * cy - s.c[i].y);
    }
    return r;
}

// grid index: column wraps 0..11, layer is row
int gk(int col, int layer) {
    return layer * COLS + ((col % COLS + COLS) % COLS);
}

// ─── precompute all valid placements for every piece ───
Placement *all_placements[NUM_PENT];
int placement_counts[NUM_PENT];

void precompute_placements(void) {
    for (int pi = 0; pi < NUM_PENT; pi++) {
        int cap = 0;
        for (int f = 0; f <= 1; f++) {
            Shape sh = f ? flip_shape_180(SHAPES[pi]) : SHAPES[pi];
            int mn = sh.c[0].y, mx = sh.c[0].y;
            for (int i = 1; i < 5; i++) {
                if (sh.c[i].y < mn) mn = sh.c[i].y;
                if (sh.c[i].y > mx) mx = sh.c[i].y;
            }
            cap += COLS * (LAYERS - (mx - mn));
        }
        all_placements[pi] = malloc(cap * sizeof(Placement));

        int cnt = 0;
        for (int f = 0; f <= 1; f++) {
            Shape sh = f ? flip_shape_180(SHAPES[pi]) : SHAPES[pi];
            int mn = sh.c[0].y, mx = sh.c[0].y;
            for (int i = 1; i < 5; i++) {
                if (sh.c[i].y < mn) mn = sh.c[i].y;
                if (sh.c[i].y > mx) mx = sh.c[i].y;
            }
            for (int bc = 0; bc < COLS; bc++) {
                for (int bl = -mn; bl < LAYERS - mx; bl++) {
                    Placement p = { .piece_idx = pi, .flipped = f ? true : false, .bc = bc, .bl = bl };
                    for (int i = 0; i < 5; i++) {
                        p.cells[i].x = ((bc + sh.c[i].x) % COLS + COLS) % COLS;
                        p.cells[i].y = bl + sh.c[i].y;
                    }
                    all_placements[pi][cnt++] = p;
                }
            }
        }
        placement_counts[pi] = cnt;
    }
}

void free_placements(void) {
    for (int i = 0; i < NUM_PENT; i++)
        free(all_placements[i]);
}

// ─── backtracking solver (MRV heuristic) ───
static bool solve_try_place(bool *used, int *grid, int placed,
                            Placement **result, int *order)
{
    if (placed == NUM_PENT) return true;

    int best_cnt = 9999;
    Placement *best_opts[200];
    int best_n = 0;

    for (int i = 0; i < TOTAL_CELLS; i++) {
        if (grid[i] != -1) continue;
        int cl = i / COLS, cc = i % COLS;
        int cnt = 0;
        Placement *opts[200];

        for (int oi = 0; oi < NUM_PENT; oi++) {
            int pi = order[oi];
            if (used[pi]) continue;
            for (int j = 0; j < placement_counts[pi]; j++) {
                Placement *p = &all_placements[pi][j];
                bool covers = false;
                for (int k = 0; k < 5; k++) {
                    if (p->cells[k].x == cc && p->cells[k].y == cl) {
                        covers = true; break;
                    }
                }
                if (!covers) continue;
                bool valid = true;
                for (int k = 0; k < 5; k++) {
                    if (grid[gk(p->cells[k].x, p->cells[k].y)] != -1) {
                        valid = false; break;
                    }
                }
                if (!valid) continue;
                if (cnt < 200) opts[cnt++] = p;
            }
        }
        if (cnt == 0) return false;
        if (cnt < best_cnt) {
            best_cnt = cnt;
            best_n = 0;
            for (int j = 0; j < cnt && j < 200; j++)
                best_opts[best_n++] = opts[j];
            if (best_cnt == 1) break;
        }
    }
    if (best_n == 0) return false;

    for (int i = 0; i < best_n; i++) {
        Placement *p = best_opts[i];
        for (int k = 0; k < 5; k++)
            grid[gk(p->cells[k].x, p->cells[k].y)] = p->piece_idx;
        used[p->piece_idx] = true;
        result[placed] = p;

        if (solve_try_place(used, grid, placed + 1, result, order))
            return true;

        used[p->piece_idx] = false;
        for (int k = 0; k < 5; k++)
            grid[gk(p->cells[k].x, p->cells[k].y)] = -1;
    }
    return false;
}

// ─── solve for a given ring configuration ───
static bool solve_ring_layers(int *order, int ring_blocked[LAYERS][12],
                              int *num_ring_blocked, Placement **result,
                              int *out_grid)
{
    int grid[TOTAL_CELLS];
    for (int i = 0; i < TOTAL_CELLS; i++) grid[i] = -1;
    for (int l = 0; l < LAYERS; l++)
        for (int i = 0; i < num_ring_blocked[l]; i++)
            grid[gk(ring_blocked[l][i], l)] = -2;

    bool used[NUM_PENT] = {false};
    bool ok = solve_try_place(used, grid, 0, result, order);
    if (ok && out_grid) memcpy(out_grid, grid, TOTAL_CELLS * sizeof(int));
    return ok;
}

// ─── exhaustive solver: find all solutions ───
static void solve_try_all(bool *used, int *grid, int placed) {
    if (placed == NUM_PENT) {
#ifdef _OPENMP
        #pragma omp atomic
#endif
        g_total_solutions++;
        return;
    }
    if (g_limit && g_total_solutions >= g_limit) return;

    int best_cnt = 9999, best_n = 0;
    Placement *best_opts[500];

    for (int i = 0; i < TOTAL_CELLS; i++) {
        if (grid[i] != -1) continue;
        int cl = i / COLS, cc = i % COLS;
        int cnt = 0;
        Placement *opts[500];

        for (int pi = 0; pi < NUM_PENT; pi++) {
            if (used[pi]) continue;
            for (int j = 0; j < placement_counts[pi]; j++) {
                Placement *p = &all_placements[pi][j];
                bool covers = false;
                for (int k = 0; k < 5; k++)
                    if (p->cells[k].x == cc && p->cells[k].y == cl) { covers = true; break; }
                if (!covers) continue;
                bool valid = true;
                for (int k = 0; k < 5; k++)
                    if (grid[gk(p->cells[k].x, p->cells[k].y)] != -1) { valid = false; break; }
                if (!valid) continue;
                if (cnt < 500) opts[cnt++] = p;
            }
        }
        if (cnt == 0) return;
        if (cnt < best_cnt) {
            best_cnt = cnt; best_n = 0;
            for (int j = 0; j < cnt && j < 500; j++) best_opts[best_n++] = opts[j];
            if (best_cnt == 1) break;
        }
    }
    if (best_n == 0) return;

    for (int i = 0; i < best_n; i++) {
        Placement *p = best_opts[i];
        for (int k = 0; k < 5; k++) grid[gk(p->cells[k].x, p->cells[k].y)] = p->piece_idx;
        used[p->piece_idx] = true;
        solve_try_all(used, grid, placed + 1);
        used[p->piece_idx] = false;
        for (int k = 0; k < 5; k++) grid[gk(p->cells[k].x, p->cells[k].y)] = -1;
        if (g_limit && g_total_solutions >= g_limit) return;
    }
}

// ─── exhaustive: enumerate all ring configs (with OpenMP parallel) ───
static void auto_solve_all(void) {
    g_total_solutions = 0;
    time_t start = time(NULL);
    const int total_configs = 120 * 20736; // 5! × 12^4  (fix ring0 rotation)
    volatile int stop = 0;
    volatile int progress = 0;
    volatile time_t last_report = 0;

    printf("  穷举 %d 种环配置", total_configs);
#ifdef _OPENMP
    int nthr = 0;
    #pragma omp parallel
    #pragma omp single
    nthr = omp_get_num_threads();
    printf(", %d 线程并行", nthr);
#endif
    printf("...\n\n");

#pragma omp parallel for schedule(dynamic, 512)
    for (int idx = 0; idx < total_configs; idx++) {
        if (stop) continue;

        // decode idx → ring permutation (0..119) + rotations for rings 1..4
        int perm_idx = idx / 20736;
        int rc = idx % 20736;
        int r1 = rc % 12; rc /= 12;
        int r2 = rc % 12; rc /= 12;
        int r3 = rc % 12; rc /= 12;
        int r4 = rc;
        int rot[5] = {0, r1, r2, r3, r4};

        // k-th permutation of [0,1,2,3,4]
        int nums[] = {0,1,2,3,4};
        int k = perm_idx, fact = 120;
        int perm[5];
        for (int i = 0; i < 5; i++) {
            fact /= 5 - i;
            int d = k / fact;
            perm[i] = nums[d];
            for (int j = d; j < 4 - i; j++) nums[j] = nums[j+1];
            k %= fact;
        }

        // build grid with ring constraints
        int grid[TOTAL_CELLS];
        memset(grid, -1, sizeof(grid));
        for (int l = 0; l < LAYERS; l++) {
            int ri = perm[l], r = rot[ri];
            for (int j = 0; j < 2; j++)
                grid[gk(((RING_OUTER[ri][j] + r) % COLS + COLS) % COLS, l)] = -2;
        }

        bool used[NUM_PENT] = {false};
        solve_try_all(used, grid, 0);

        if (g_limit && g_total_solutions >= g_limit) stop = 1;

#ifdef _OPENMP
        #pragma omp atomic
#endif
        progress++;

        // progress report (~1s intervals)
#ifdef _OPENMP
        if (omp_get_thread_num() == 0)
#endif
        {
            time_t now = time(NULL);
            if (now > last_report) {
                last_report = now;
                double sec = difftime(now, start);
                printf("\r  进度: %d/%d (%d%%) | 解: %lld | 用时: %.0fs",
                       (int)progress, total_configs,
                       (int)(progress * 100 / total_configs),
                       (long long)g_total_solutions, sec);
                fflush(stdout);
            }
        }
    }

    double sec = difftime(time(NULL), start);
    printf("\n\n=== 穷举完成 ===\n");
    printf("  总解数: %lld\n", (long long)g_total_solutions);
    printf("  用时: %.0f 秒\n", sec);
}

// ─── main solver (randomized, single-solve) ───
static void auto_solve(void) {
    for (int attempt = 0; attempt < MAX_ATTEMPTS; attempt++) {
        // shuffle rings 0..4 onto 5 layers
        int layers[LAYERS] = {0, 1, 2, 3, 4};
        for (int i = LAYERS - 1; i > 0; i--) {
            int j = rand() % (i + 1);
            int t = layers[i]; layers[i] = layers[j]; layers[j] = t;
        }

        // random rotation for each ring
        int rotations[LAYERS];
        for (int i = 0; i < LAYERS; i++)
            rotations[i] = rand() % COLS;

        // compute outer cells blocked per layer
        int ring_blocked[LAYERS][12], num_ring_blocked[LAYERS];
        memset(num_ring_blocked, 0, sizeof(num_ring_blocked));
        for (int l = 0; l < LAYERS; l++) {
            int ri = layers[l], rot = rotations[ri];
            for (int j = 0; j < 2; j++) {
                int c = ((RING_OUTER[ri][j] + rot) % COLS + COLS) % COLS;
                ring_blocked[l][num_ring_blocked[l]++] = c;
            }
        }

        // random pentomino order
        int order[NUM_PENT];
        for (int i = 0; i < NUM_PENT; i++) order[i] = i;
        for (int i = NUM_PENT - 1; i > 0; i--) {
            int j = rand() % (i + 1);
            int t = order[i]; order[i] = order[j]; order[j] = t;
        }

        Placement *result[NUM_PENT];
        int final_grid[TOTAL_CELLS];

        if (solve_ring_layers(order, ring_blocked, num_ring_blocked,
                              result, final_grid))
        {
            printf("=== 找到解 (尝试 %d) ===\n\n", attempt + 1);
            printf("环形积木分配:\n");
            for (int l = 0; l < LAYERS; l++) {
                int ri = layers[l];
                printf("  第%d层: ring_%d, 外弧列=[%d,%d], 旋转=%d\n",
                       l, ri, RING_OUTER[ri][0], RING_OUTER[ri][1],
                       rotations[ri]);
            }
            printf("\n五连方块放置:\n");
            for (int i = 0; i < NUM_PENT; i++) {
                Placement *p = result[i];
                printf("  %s: base_col=%d, base_layer=%d, 翻转=%s\n",
                       PIECE_NAMES[p->piece_idx], p->bc, p->bl,
                       p->flipped ? "是" : "否");
            }
            printf("\n网格 (上=第4层, 下=第0层, *=环形, .=空, 0-9=方块编号):\n");
            for (int l = LAYERS - 1; l >= 0; l--) {
                printf("L%d: ", l);
                for (int c = 0; c < COLS; c++) {
                    int v = final_grid[l * COLS + c];
                    if (v == -2)      printf(" *");
                    else if (v == -1) printf(" .");
                    else              printf("%2d", v);
                }
                printf("\n");
            }
            return;
        }
    }
    printf("在 %d 次尝试后未找到解。\n", MAX_ATTEMPTS);
}

int main(int argc, char *argv[]) {
    // parse flags
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-a") == 0) g_exhaustive = 1;
        else if (strcmp(argv[i], "-n") == 0 && i+1 < argc)
            g_limit = atoll(argv[++i]);
    }

    printf("Katamino Tower Solver\n");
    printf("=====================\n\n");

    srand(time(NULL));
    (void)BLOCK_MATRIX;
    precompute_placements();
    for (int i = 0; i < NUM_PENT; i++)
        printf("  %s: %d 种放置方式\n", PIECE_NAMES[i], placement_counts[i]);
    printf("\n");

    if (g_exhaustive) {
        if (g_limit)
            printf("穷举模式 (限制 %lld 解, 固定 Ring0 旋转=0, 共 120×12^4 ≈ 2.5M 配置)\n\n", g_limit);
        else
            printf("穷举模式 (固定 Ring0 旋转=0, 共 120×12^4 ≈ 2.5M 配置)\n\n");
        auto_solve_all();
    } else {
        auto_solve();
    }

    free_placements();
    return 0;
}
