/*
 * yduts — a minimal studying timer
 * aesthetic: black screen, handwritten digits, nothing else
 *
 * USAGE:
 *   ./yduts                  — stopwatch (count up)
 *   ./yduts 25m              — countdown 25 minutes
 *   ./yduts 1h30m            — countdown 1 hour 30 minutes
 *   ./yduts 90               — countdown 90 seconds
 *   ./yduts pomodoro         — 25m work / 5m break loop
 *   ./yduts -p <mode>        — start paused
 *
 * KEYS:
 *   SPACE   — pause / resume
 *   r       — restart current session
 *   n       — next pomodoro phase (pomodoro mode only)
 *   f       — toggle fullscreen
 *   q / ESC — quit
 */

#include "raylib.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

/* types */

typedef enum {
    MODE_STOPWATCH = 0,
    MODE_COUNTDOWN,
    MODE_POMODORO,
} Mode;

typedef enum {
    POMO_WORK = 0,
    POMO_BREAK,
} PomoPhase;

#define POMO_WORK_SECS  (25 * 60)
#define POMO_BREAK_SECS ( 5 * 60)

/* globals */

static Mode      g_mode          = MODE_STOPWATCH;
static double    g_elapsed       = 0.0;   /* seconds elapsed / remaining */
static double    g_target        = 0.0;   /* countdown target seconds    */
static int       g_paused        = 0;
static int       g_finished      = 0;     /* countdown hit zero          */
static PomoPhase g_pomo_phase    = POMO_WORK;
static int       g_pomo_count    = 0;     /* completed work sessions     */

/* parse helpers */

/* parse "25m", "1h30m", "90", "1h", "30s" → seconds, returns -1 on fail */
static int parse_duration(const char *s) {
    int total = 0, val = 0, found = 0;
    for (; *s; s++) {
        if (*s >= '0' && *s <= '9') {
            val = val * 10 + (*s - '0');
            found = 1;
        } else if (*s == 'h' || *s == 'H') {
            total += val * 3600; val = 0;
        } else if (*s == 'm' || *s == 'M') {
            total += val * 60;   val = 0;
        } else if (*s == 's' || *s == 'S') {
            total += val;        val = 0;
        } else {
            return -1;
        }
    }
    if (found && val > 0) total += val; /* bare number = seconds */
    return (total > 0) ? total : -1;
}

/* drawing */

#define BG      (Color){ 0,   0,   0,   255 }
#define FG      (Color){ 230, 230, 230, 255 }
#define FG_DIM  (Color){ 80,  80,  80,  255 }
#define FG_RED  (Color){ 200, 80,  80,  255 }
#define FG_GRN  (Color){ 80,  180, 80,  255 }

static Font g_font;
static int  g_font_loaded = 0;

static void load_font(void) {
    g_font = LoadFontEx("assets/font.ttf", 300, NULL, 0);
    if (g_font.texture.id != 0) {
        SetTextureFilter(g_font.texture, TEXTURE_FILTER_BILINEAR);
        g_font_loaded = 1;
    }
}

/* format seconds into HH:MM:SS or MM:SS */
static void fmt_time(double secs_f, char *out, int sz) {
    if (secs_f < 0) secs_f = 0;
    int s = (int)secs_f;
    int h = s / 3600; s %= 3600;
    int m = s / 60;   s %= 60;
    if (h > 0)
        snprintf(out, sz, "%02d:%02d:%02d", h, m, s);
    else
        snprintf(out, sz, "%02d:%02d", m, s);
}

static void draw_centered_text(const char *txt, float y, float font_sz, Color col) {
    float spacing = font_sz * 0.04f;
    Vector2 size  = MeasureTextEx(g_font, txt, font_sz, spacing);
    Vector2 pos   = { (GetScreenWidth() - size.x) * 0.5f, y - size.y * 0.5f };
    DrawTextEx(g_font, txt, pos, font_sz, spacing, col);
}

static void draw_frame(void) {
    int W = GetScreenWidth(), H = GetScreenHeight();
    float cy = H * 0.5f;
    (void)W;

    ClearBackground(BG);

    /* main timer */
    char tbuf[32];
    double display_secs = (g_mode == MODE_COUNTDOWN || g_mode == MODE_POMODORO)
                          ? g_elapsed : g_elapsed;

    fmt_time(display_secs, tbuf, sizeof tbuf);

    Color timer_col = FG;
    if (g_finished) timer_col = FG_RED;
    if (g_paused)   timer_col = FG_DIM;

    float font_sz = H * 0.22f;
    /* clamp so it fits width */
    {
        float spacing = font_sz * 0.04f;
        Vector2 sz = MeasureTextEx(g_font, tbuf, font_sz, spacing);
        if (sz.x > W * 0.90f) font_sz *= (W * 0.90f) / sz.x;
    }

    draw_centered_text(tbuf, cy, font_sz, timer_col);

    /* small label */
    char label[64] = "";
    if (g_mode == MODE_POMODORO) {
        const char *phase = (g_pomo_phase == POMO_WORK) ? "work" : "break";
        snprintf(label, sizeof label, "pomodoro · %s · session %d",
                 phase, g_pomo_count + 1);
    } else if (g_mode == MODE_COUNTDOWN) {
        snprintf(label, sizeof label, "countdown");
    } else {
        snprintf(label, sizeof label, "stopwatch");
    }
    if (g_paused) {
        size_t n = strlen(label);
        snprintf(label + n, sizeof label - n, " · paused");
    }
    float label_sz = H * 0.025f;
    if (label_sz < 12) label_sz = 12;
    draw_centered_text(label, cy + font_sz * 0.62f, label_sz, FG_DIM);

    /* ── key hint (bottom) ── */
    const char *hints = "SPACE pause · r restart · f fullscreen · q quit";
    if (g_mode == MODE_POMODORO) hints = "SPACE pause · r restart · n next · f fullscreen · q quit";
    float hint_sz = H * 0.018f;
    if (hint_sz < 10) hint_sz = 10;
    draw_centered_text(hints, H - H * 0.04f, hint_sz, (Color){45,45,45,255});
}

/* logic */

static void restart_session(void) {
    g_finished = 0;
    switch (g_mode) {
        case MODE_STOPWATCH: g_elapsed = 0.0; break;
        case MODE_COUNTDOWN: g_elapsed = g_target; break;
        case MODE_POMODORO:
            g_elapsed = (g_pomo_phase == POMO_WORK) ? POMO_WORK_SECS : POMO_BREAK_SECS;
            break;
    }
}

static void next_pomo(void) {
    if (g_pomo_phase == POMO_WORK) {
        g_pomo_count++;
        g_pomo_phase = POMO_BREAK;
    } else {
        g_pomo_phase = POMO_WORK;
    }
    g_finished = 0;
    g_elapsed  = (g_pomo_phase == POMO_WORK) ? POMO_WORK_SECS : POMO_BREAK_SECS;
}

static void handle_input(void) {
    if (IsKeyPressed(KEY_SPACE))  g_paused = !g_paused;
    if (IsKeyPressed(KEY_R))      restart_session();
    if (IsKeyPressed(KEY_F))      ToggleFullscreen();
    if (IsKeyPressed(KEY_Q) || IsKeyPressed(KEY_ESCAPE)) CloseWindow();
    if (IsKeyPressed(KEY_N) && g_mode == MODE_POMODORO) next_pomo();
}

static void tick(double dt) {
    if (g_paused || g_finished) return;

    switch (g_mode) {
        case MODE_STOPWATCH:
            g_elapsed += dt;
            break;
        case MODE_COUNTDOWN:
            g_elapsed -= dt;
            if (g_elapsed <= 0) { g_elapsed = 0; g_finished = 1; }
            break;
        case MODE_POMODORO:
            g_elapsed -= dt;
            if (g_elapsed <= 0) {
                g_elapsed  = 0;
                g_finished = 1; /* user presses n to advance */
            }
            break;
    }
}

/* main */

int main(int argc, char **argv) {
    int start_paused = 0;

    /* parse -p flag first */
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-p") == 0) {
            start_paused = 1;
            /* shift args left */
            for (int j = i; j < argc - 1; j++) argv[j] = argv[j+1];
            argc--; i--;
        }
    }

    if (argc < 2) {
        g_mode    = MODE_STOPWATCH;
        g_elapsed = 0.0;
    } else if (strcmp(argv[1], "pomodoro") == 0) {
        g_mode       = MODE_POMODORO;
        g_pomo_phase = POMO_WORK;
        g_elapsed    = POMO_WORK_SECS;
    } else {
        int secs = parse_duration(argv[1]);
        if (secs < 0) {
            fprintf(stderr, "yduts: cannot parse '%s'\n", argv[1]);
            fprintf(stderr, "usage: yduts [25m | 1h30m | 90 | pomodoro] [-p]\n");
            return 1;
        }
        g_mode    = MODE_COUNTDOWN;
        g_target  = secs;
        g_elapsed = secs;
    }

    g_paused = start_paused;

    /* window */
    SetConfigFlags(FLAG_WINDOW_RESIZABLE | FLAG_MSAA_4X_HINT);
    InitWindow(900, 420, "yduts");
    SetTargetFPS(60);
    SetExitKey(0); /* we handle quit ourselves */

    load_font();

    while (!WindowShouldClose()) {
        handle_input();
        tick(GetFrameTime());

        BeginDrawing();
        draw_frame();
        EndDrawing();
    }

    if (g_font_loaded) UnloadFont(g_font);
    CloseWindow();
    return 0;
}
