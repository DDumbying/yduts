#define _XOPEN_SOURCE 700
#include "raylib.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <time.h>

/* types */

typedef enum { MODE_STOPWATCH = 0, MODE_COUNTDOWN, MODE_POMODORO } Mode;
typedef enum { POMO_WORK = 0, POMO_BREAK } PomoPhase;

#define POMO_WORK_SECS  (25 * 60)
#define POMO_BREAK_SECS ( 5 * 60)
#define MAX_LABEL       128

/* globals */

static Mode      g_mode          = MODE_STOPWATCH;
static double    g_elapsed       = 0.0;
static double    g_target        = 0.0;
static int       g_paused        = 0;
static int       g_finished      = 0;
static PomoPhase g_pomo_phase    = POMO_WORK;
static int       g_pomo_count    = 0;

static char      g_label[MAX_LABEL] = "";
static time_t    g_session_start    = 0;
static double    g_active_secs      = 0.0;

/* bell */

#define BELL_SAMPLE_RATE  44100
#define BELL_DURATION_SEC 1.8f      /* fade-out duration                    */
#define BELL_FREQ_HZ      520.0f    /* fundamental — clean, not harsh       */
#define BELL_FREQ2_HZ     780.0f    /* fifth above — warmth                 */
#define BELL_SAMPLES      79464   /* 44100 * 1.8 */

static AudioStream g_bell_stream;
static short       g_bell_buf[BELL_SAMPLES];
static int         g_bell_pos     = -1;   /* -1 = not playing               */
static int         g_bell_muted   = 0;
static int         g_audio_ready  = 0;

static void bell_generate(void) {
    for (int i = 0; i < BELL_SAMPLES; i++) {
        float t       = (float)i / BELL_SAMPLE_RATE;
        float env     = expf(-t * 2.8f);                        /* exponential decay */
        float wave    = 0.65f * sinf(2.0f * PI * BELL_FREQ_HZ  * t)
                      + 0.35f * sinf(2.0f * PI * BELL_FREQ2_HZ * t);
        g_bell_buf[i] = (short)(wave * env * 28000.0f);
    }
}

static void bell_init(void) {
    InitAudioDevice();
    if (!IsAudioDeviceReady()) return;
    bell_generate();
    g_bell_stream = LoadAudioStream(BELL_SAMPLE_RATE, 16, 1);
    g_audio_ready = 1;
}

static void bell_play(void) {
    if (!g_audio_ready || g_bell_muted) return;
    g_bell_pos = 0;
}

/* called every frame — streams chunks into raylib */
static void bell_update(void) {
    if (!g_audio_ready || g_bell_pos < 0) return;
    if (IsAudioStreamProcessed(g_bell_stream)) {
        int remaining = BELL_SAMPLES - g_bell_pos;
        if (remaining <= 0) { g_bell_pos = -1; return; }
        int chunk = remaining < 4096 ? remaining : 4096;
        UpdateAudioStream(g_bell_stream, g_bell_buf + g_bell_pos, chunk);
        if (g_bell_pos == 0) PlayAudioStream(g_bell_stream);
        g_bell_pos += chunk;
    }
}

static void bell_close(void) {
    if (!g_audio_ready) return;
    UnloadAudioStream(g_bell_stream);
    CloseAudioDevice();
}

/* log path */

static void get_log_path(char *out, int sz) {
    const char *home = getenv("HOME");
    if (!home) home = ".";
    snprintf(out, sz, "%s/.yduts_log", home);
}

/* session logging */

static void fmt_duration(double secs_f, char *out, int sz) {
    if (secs_f < 0) secs_f = 0;
    int s = (int)secs_f;
    int h = s / 3600; s %= 3600;
    int m = s / 60;   s %= 60;
    snprintf(out, sz, "%02dh%02dm%02ds", h, m, s);
}

static void log_session(const char *status) {
    if (g_active_secs < 5.0) return;
    char path[512];
    get_log_path(path, sizeof path);
    FILE *f = fopen(path, "a");
    if (!f) return;
    char ts[32];
    struct tm *tm_info = localtime(&g_session_start);
    strftime(ts, sizeof ts, "%Y-%m-%dT%H:%M:%S", tm_info);
    char dur[32];
    fmt_duration(g_active_secs, dur, sizeof dur);
    const char *lbl = g_label[0] ? g_label : "-";
    fprintf(f, "%s | %s | %-20s | %s\n", ts, dur, lbl, status);
    fclose(f);
}

/* stats subcommand */

static int parse_log_duration(const char *s) {
    int h = 0, m = 0, sec = 0;
    sscanf(s, "%dh%dm%ds", &h, &m, &sec);
    return h * 3600 + m * 60 + sec;
}

static void cmd_stats(void) {
    char path[512];
    get_log_path(path, sizeof path);
    FILE *f = fopen(path, "r");
    if (!f) { printf("yduts: no log found at %s\n", path); return; }

    time_t now = time(NULL);
    struct tm *now_tm = localtime(&now);
    struct tm day_start = *now_tm;
    day_start.tm_hour = 0; day_start.tm_min = 0; day_start.tm_sec = 0;
    time_t today_t = mktime(&day_start);
    time_t week_t  = today_t - 6 * 86400;

    double total_today = 0, total_week = 0, total_all = 0;
    int    count_today = 0, count_week = 0, count_all = 0;

    #define MAX_TOPICS 64
    char   topic_names[MAX_TOPICS][MAX_LABEL];
    double topic_secs [MAX_TOPICS];
    int    topic_count = 0;
    memset(topic_names, 0, sizeof topic_names);
    memset(topic_secs,  0, sizeof topic_secs);

    char line[512];
    while (fgets(line, sizeof line, f)) {
        char ts[32], dur[32], lbl[MAX_LABEL], status[32];
        if (sscanf(line, "%31s | %31s | %127[^|] | %31s", ts, dur, lbl, status) != 4) continue;
        int ll = (int)strlen(lbl);
        while (ll > 0 && lbl[ll-1] == ' ') lbl[--ll] = '\0';
        struct tm entry_tm = {0};
        if (!strptime(ts, "%Y-%m-%dT%H:%M:%S", &entry_tm)) continue;
        time_t entry_t = mktime(&entry_tm);
        int secs = parse_log_duration(dur);
        total_all += secs; count_all++;
        if (entry_t >= week_t)  { total_week  += secs; count_week++;  }
        if (entry_t >= today_t) { total_today += secs; count_today++; }
        if (strcmp(lbl, "-") != 0) {
            int found = 0;
            for (int i = 0; i < topic_count; i++) {
                if (strcmp(topic_names[i], lbl) == 0) { topic_secs[i] += secs; found = 1; break; }
            }
            if (!found && topic_count < MAX_TOPICS) {
                snprintf(topic_names[topic_count], MAX_LABEL, "%s", lbl);
                topic_secs[topic_count++] = secs;
            }
        }
    }
    fclose(f);

    /* sort topics by time desc */
    for (int i = 0; i < topic_count - 1; i++)
        for (int j = i+1; j < topic_count; j++)
            if (topic_secs[j] > topic_secs[i]) {
                double td = topic_secs[i]; topic_secs[i] = topic_secs[j]; topic_secs[j] = td;
                char tmp[MAX_LABEL]; strcpy(tmp, topic_names[i]);
                strcpy(topic_names[i], topic_names[j]); strcpy(topic_names[j], tmp);
            }

    char buf[32];
    printf("\n  yduts · study log\n");
    printf("  ─────────────────────────────\n");
    fmt_duration(total_today, buf, sizeof buf);
    printf("  today      %s  (%d session%s)\n", buf, count_today, count_today==1?"":"s");
    fmt_duration(total_week, buf, sizeof buf);
    printf("  this week  %s  (%d session%s)\n", buf, count_week, count_week==1?"":"s");
    fmt_duration(total_all, buf, sizeof buf);
    printf("  all time   %s  (%d session%s)\n", buf, count_all, count_all==1?"":"s");
    if (topic_count > 0) {
        printf("\n  by topic:\n");
        for (int i = 0; i < topic_count; i++) {
            fmt_duration(topic_secs[i], buf, sizeof buf);
            printf("    %-22s %s\n", topic_names[i], buf);
        }
    }
    printf("  ─────────────────────────────\n");
    printf("  log: %s\n\n", path);
}

/* parse helpers */

static int parse_duration(const char *s) {
    int total = 0, val = 0, found = 0;
    for (; *s; s++) {
        if      (*s >= '0' && *s <= '9')    { val = val * 10 + (*s - '0'); found = 1; }
        else if (*s == 'h' || *s == 'H')    { total += val * 3600; val = 0; }
        else if (*s == 'm' || *s == 'M')    { total += val * 60;   val = 0; }
        else if (*s == 's' || *s == 'S')    { total += val;        val = 0; }
        else return -1;
    }
    if (found && val > 0) total += val;
    return (total > 0) ? total : -1;
}

/* drawing */

#define BG     (Color){ 0,   0,   0,   255 }
#define FG     (Color){ 230, 230, 230, 255 }
#define FG_DIM (Color){ 75,  75,  75,  255 }
#define FG_RED (Color){ 200, 80,  80,  255 }

static Font g_font;
static int  g_font_loaded = 0;

static void load_font(void) {
    g_font = LoadFontEx("assets/font.ttf", 300, NULL, 0);
    if (g_font.texture.id != 0) {
        SetTextureFilter(g_font.texture, TEXTURE_FILTER_BILINEAR);
        g_font_loaded = 1;
    }
}

static void fmt_time(double secs_f, char *out, int sz) {
    if (secs_f < 0) secs_f = 0;
    int s = (int)secs_f;
    int h = s / 3600; s %= 3600;
    int m = s / 60;   s %= 60;
    if (h > 0) snprintf(out, sz, "%02d:%02d:%02d", h, m, s);
    else        snprintf(out, sz, "%02d:%02d", m, s);
}

static void draw_centered_text(const char *txt, float y, float font_sz, Color col) {
    float spacing = font_sz * 0.04f;
    Vector2 size  = MeasureTextEx(g_font, txt, font_sz, spacing);
    Vector2 pos   = { (GetScreenWidth() - size.x) * 0.5f, y - size.y * 0.5f };
    DrawTextEx(g_font, txt, pos, font_sz, spacing, col);
}

/* returns progress 0.0–1.0 for bar; -1 if not applicable */
static float get_progress(void) {
    if (g_mode == MODE_STOPWATCH) return -1.0f;
    double total = 0;
    if (g_mode == MODE_COUNTDOWN) total = g_target;
    else total = (g_pomo_phase == POMO_WORK) ? POMO_WORK_SECS : POMO_BREAK_SECS;
    if (total <= 0) return -1.0f;
    float p = (float)(g_elapsed / total);
    if (p < 0.0f) p = 0.0f;
    if (p > 1.0f) p = 1.0f;
    return p;
}

static void draw_progress_bar(int W, int H, float progress) {
    /* 2px bar pinned to bottom, dims when paused */
    int bar_h  = 2;
    int bar_y  = H - bar_h;
    int bar_w  = (int)(W * progress);

    Color col = g_finished ? FG_RED : (g_paused ? FG_DIM : FG);
    col.a = g_paused ? 80 : 160;   /* subtle always, dimmer when paused */

    /* track */
    DrawRectangle(0, bar_y, W, bar_h, (Color){18, 18, 18, 255});
    /* fill */
    if (bar_w > 0)
        DrawRectangle(0, bar_y, bar_w, bar_h, col);
}

static void draw_frame(void) {
    int W = GetScreenWidth(), H = GetScreenHeight();
    float cy = H * 0.48f;

    ClearBackground(BG);

    /* ── main timer ── */
    char tbuf[32];
    fmt_time(g_elapsed, tbuf, sizeof tbuf);

    Color timer_col = FG;
    if (g_finished) timer_col = FG_RED;
    if (g_paused)   timer_col = FG_DIM;

    float font_sz = H * 0.22f;
    {
        float spacing = font_sz * 0.04f;
        Vector2 sz = MeasureTextEx(g_font, tbuf, font_sz, spacing);
        if (sz.x > W * 0.88f) font_sz *= (W * 0.88f) / sz.x;
    }
    draw_centered_text(tbuf, cy, font_sz, timer_col);

    /* ── session label above timer ── */
    if (g_label[0]) {
        float lbl_sz = H * 0.028f;
        if (lbl_sz < 12) lbl_sz = 12;
        draw_centered_text(g_label, cy - font_sz * 0.58f, lbl_sz, FG_DIM);
    }

    /* ── mode/status line below timer ── */
    char status[128] = "";
    if (g_mode == MODE_POMODORO) {
        const char *phase = (g_pomo_phase == POMO_WORK) ? "work" : "break";
        snprintf(status, sizeof status, "pomodoro · %s · %d", phase, g_pomo_count + 1);
    } else if (g_mode == MODE_COUNTDOWN) {
        snprintf(status, sizeof status, "countdown");
    } else {
        snprintf(status, sizeof status, "stopwatch");
    }
    if (g_paused) { size_t n = strlen(status); snprintf(status+n, sizeof status-n, " · paused"); }
    float status_sz = H * 0.022f;
    if (status_sz < 11) status_sz = 11;
    draw_centered_text(status, cy + font_sz * 0.60f, status_sz, FG_DIM);

    /* ── mute indicator (top-right, tiny) ── */
    if (g_bell_muted) {
        float msz = H * 0.018f;
        if (msz < 10) msz = 10;
        float spacing = msz * 0.04f;
        Vector2 sz = MeasureTextEx(g_font, "muted", msz, spacing);
        Vector2 pos = { W - sz.x - W * 0.02f, H * 0.03f };
        DrawTextEx(g_font, "muted", pos, msz, spacing, (Color){40,40,40,255});
    }

    /* ── key hints (bottom) ── */
    const char *hints = "SPACE pause · r restart · m mute · f fullscreen · q quit";
    if (g_mode == MODE_POMODORO)
        hints = "SPACE pause · r restart · n next · m mute · f fullscreen · q quit";
    float hint_sz = H * 0.016f;
    if (hint_sz < 10) hint_sz = 10;
    /* push hints up a tiny bit so bar doesn't overlap them */
    draw_centered_text(hints, H - H * 0.05f, hint_sz, (Color){40,40,40,255});

    /* ── progress bar ── */
    float progress = get_progress();
    if (progress >= 0.0f)
        draw_progress_bar(W, H, progress);
}

/* logic */

static void restart_session(void) {
    log_session("restarted");
    g_active_secs   = 0.0;
    g_session_start = time(NULL);
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
    log_session(g_finished ? "completed" : "skipped");
    g_active_secs   = 0.0;
    g_session_start = time(NULL);
    if (g_pomo_phase == POMO_WORK) { g_pomo_count++; g_pomo_phase = POMO_BREAK; }
    else                           { g_pomo_phase = POMO_WORK; }
    g_finished = 0;
    g_elapsed  = (g_pomo_phase == POMO_WORK) ? POMO_WORK_SECS : POMO_BREAK_SECS;
}

static void handle_input(void) {
    if (IsKeyPressed(KEY_SPACE)) g_paused = !g_paused;
    if (IsKeyPressed(KEY_R))     restart_session();
    if (IsKeyPressed(KEY_F))     ToggleFullscreen();
    if (IsKeyPressed(KEY_M))     g_bell_muted = !g_bell_muted;
    if (IsKeyPressed(KEY_N) && g_mode == MODE_POMODORO) next_pomo();
    if (IsKeyPressed(KEY_Q) || IsKeyPressed(KEY_ESCAPE)) {
        log_session(g_finished ? "completed" : "interrupted");
        bell_close();
        CloseWindow();
    }
}

static void tick(double dt) {
    if (!g_paused && !g_finished) g_active_secs += dt;
    if (g_paused || g_finished)   return;

    switch (g_mode) {
        case MODE_STOPWATCH:
            g_elapsed += dt;
            break;
        case MODE_COUNTDOWN:
            g_elapsed -= dt;
            if (g_elapsed <= 0) {
                g_elapsed       = 0;
                g_finished      = 1;
                log_session("completed");
                g_active_secs   = 0.0;
                g_session_start = time(NULL);
                bell_play();
            }
            break;
        case MODE_POMODORO:
            g_elapsed -= dt;
            if (g_elapsed <= 0) {
                g_elapsed  = 0;
                g_finished = 1;
                bell_play();
            }
            break;
    }
}

/* main */

int main(int argc, char **argv) {
    if (argc >= 2 && strcmp(argv[1], "stats") == 0) {
        cmd_stats();
        return 0;
    }

    int start_paused = 0;
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-p") == 0) {
            start_paused = 1;
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
            fprintf(stderr, "usage: yduts [25m | 1h30m | 90 | pomodoro] [\"label\"] [-p]\n");
            fprintf(stderr, "       yduts stats\n");
            return 1;
        }
        g_mode    = MODE_COUNTDOWN;
        g_target  = secs;
        g_elapsed = secs;
    }

    if (argc >= 3) {
        strncpy(g_label, argv[2], MAX_LABEL - 1);
        g_label[MAX_LABEL - 1] = '\0';
    }

    g_paused        = start_paused;
    g_session_start = time(NULL);

    SetConfigFlags(FLAG_WINDOW_RESIZABLE | FLAG_MSAA_4X_HINT);
    InitWindow(900, 420, "yduts");
    SetTargetFPS(60);
    SetExitKey(0);

    load_font();
    bell_init();

    while (!WindowShouldClose()) {
        bell_update();
        handle_input();
        tick(GetFrameTime());
        BeginDrawing();
        draw_frame();
        EndDrawing();
    }

    log_session(g_finished ? "completed" : "interrupted");
    if (g_font_loaded) UnloadFont(g_font);
    bell_close();
    CloseWindow();
    return 0;
}
