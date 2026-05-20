#define _XOPEN_SOURCE 700
#include "raylib.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <time.h>
#include <ctype.h>

/* types */

typedef enum { MODE_STOPWATCH = 0, MODE_COUNTDOWN, MODE_POMODORO } Mode;
typedef enum { POMO_WORK = 0, POMO_BREAK } PomoPhase;

#define MAX_LABEL    128
#define MAX_PATH     512

/* config */

typedef struct {
    char  bell_sound[MAX_PATH];  /* path to .wav/.ogg, empty = use generated */
    float bell_volume;           /* 0.0 – 1.0 */
    int   mute;
    int   pomo_work_secs;
    int   pomo_break_secs;
    char  font[MAX_PATH];        /* path to .ttf, empty = assets/font.ttf   */
    /* theme fields reserved for future use */
} Config;

static Config g_cfg = {
    .bell_sound      = "",
    .bell_volume     = 1.0f,
    .mute            = 0,
    .pomo_work_secs  = 25 * 60,
    .pomo_break_secs =  5 * 60,
    .font            = "",
};

/* trim leading/trailing whitespace in-place */
static char *trim(char *s) {
    while (isspace((unsigned char)*s)) s++;
    char *end = s + strlen(s);
    while (end > s && isspace((unsigned char)*(end-1))) *--end = '\0';
    return s;
}

static int cfg_parse_duration(const char *s) {
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

static void config_load(void) {
    const char *home = getenv("HOME");
    if (!home) home = ".";
    char path[MAX_PATH];
    snprintf(path, sizeof path, "%s/.yduts_config", home);

    FILE *f = fopen(path, "r");
    if (!f) return;   /* no config = all defaults, that's fine */

    char line[MAX_PATH * 2];
    while (fgets(line, sizeof line, f)) {
        /* strip comment */
        char *hash = strchr(line, '#');
        if (hash) *hash = '\0';

        char *eq = strchr(line, '=');
        if (!eq) continue;

        *eq = '\0';
        char *key = trim(line);
        char *val = trim(eq + 1);
        if (!*key || !*val) continue;

        if (strcmp(key, "bell_sound") == 0) {
            snprintf(g_cfg.bell_sound, MAX_PATH, "%s", val);

        } else if (strcmp(key, "bell_volume") == 0) {
            float v = (float)atof(val);
            if (v < 0.0f) v = 0.0f;
            if (v > 1.0f) v = 1.0f;
            g_cfg.bell_volume = v;

        } else if (strcmp(key, "mute") == 0) {
            g_cfg.mute = (strcmp(val, "true") == 0 || strcmp(val, "1") == 0);

        } else if (strcmp(key, "pomodoro_work") == 0) {
            int s = cfg_parse_duration(val);
            if (s > 0) g_cfg.pomo_work_secs = s;

        } else if (strcmp(key, "pomodoro_break") == 0) {
            int s = cfg_parse_duration(val);
            if (s > 0) g_cfg.pomo_break_secs = s;

        } else if (strcmp(key, "font") == 0) {
            snprintf(g_cfg.font, MAX_PATH, "%s", val);
        }
        /* unknown keys silently ignored */
    }
    fclose(f);
}

/* write a default config if none exists */
static void config_init_file(void) {
    const char *home = getenv("HOME");
    if (!home) home = ".";
    char path[MAX_PATH];
    snprintf(path, sizeof path, "%s/.yduts_config", home);

    FILE *probe = fopen(path, "r");
    if (probe) { fclose(probe); return; }   /* already exists */

    FILE *f = fopen(path, "w");
    if (!f) return;
    fprintf(f,
        "# yduts configuration\n"
        "# Lines starting with # are comments.\n"
        "\n"
        "# Path to a .wav or .ogg file to play when a session ends.\n"
        "# Leave empty (or comment out) to use the built-in generated tone.\n"
        "# bell_sound = /home/user/sounds/bell.wav\n"
        "\n"
        "# Bell volume: 0.0 (silent) to 1.0 (full)\n"
        "bell_volume = 1.0\n"
        "\n"
        "# Start muted (true/false)\n"
        "mute = false\n"
        "\n"
        "# Pomodoro session lengths\n"
        "pomodoro_work  = 25m\n"
        "pomodoro_break = 5m\n"
        "\n"
        "# Custom font: path to any .ttf file.\n"
        "# Defaults to assets/font.ttf (bundled Lora Italic).\n"
        "# font = /home/user/fonts/Caveat-Regular.ttf\n"
        "\n"
        "# Theme (coming soon)\n"
        "# theme_fg     = e6e6e6\n"
        "# theme_bg     = 000000\n"
        "# theme_dim    = 4b4b4b\n"
        "# theme_alert  = c85050\n"
    );
    fclose(f);
    fprintf(stderr, "yduts: created default config at %s\n", path);
}

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
#define BELL_DURATION_SEC 1.8f
#define BELL_FREQ_HZ      520.0f
#define BELL_FREQ2_HZ     780.0f
#define BELL_SAMPLES      79464   /* 44100 * 1.8 */

/* generated-tone path */
static AudioStream g_bell_stream;
static short       g_bell_buf[BELL_SAMPLES];
static int         g_bell_pos    = -1;
static int         g_use_stream  = 0;   /* 1 = streaming generated tone */

/* file-sound path */
static Sound       g_bell_sound;
static int         g_use_sound   = 0;   /* 1 = loaded from file */

static int         g_audio_ready = 0;

static void bell_generate(void) {
    for (int i = 0; i < BELL_SAMPLES; i++) {
        float t   = (float)i / BELL_SAMPLE_RATE;
        float env = expf(-t * 2.8f);
        float w   = 0.65f * sinf(2.0f * PI * BELL_FREQ_HZ  * t)
                  + 0.35f * sinf(2.0f * PI * BELL_FREQ2_HZ * t);
        g_bell_buf[i] = (short)(w * env * 28000.0f * g_cfg.bell_volume);
    }
}

static void bell_init(void) {
    InitAudioDevice();
    if (!IsAudioDeviceReady()) return;
    g_audio_ready = 1;

    /* try loading user file first */
    if (g_cfg.bell_sound[0] != '\0') {
        g_bell_sound = LoadSound(g_cfg.bell_sound);
        if (g_bell_sound.frameCount > 0) {
            SetSoundVolume(g_bell_sound, g_cfg.bell_volume);
            g_use_sound = 1;
            return;
        }
        fprintf(stderr, "yduts: could not load bell_sound '%s', using built-in tone\n",
                g_cfg.bell_sound);
    }

    /* fall back to generated tone */
    bell_generate();
    g_bell_stream = LoadAudioStream(BELL_SAMPLE_RATE, 16, 1);
    SetAudioStreamVolume(g_bell_stream, g_cfg.bell_volume);
    g_use_stream = 1;
}

static void bell_play(void) {
    if (!g_audio_ready || g_cfg.mute) return;
    if (g_use_sound) {
        PlaySound(g_bell_sound);
    } else if (g_use_stream) {
        g_bell_pos = 0;
    }
}

static void bell_update(void) {
    if (!g_use_stream || g_bell_pos < 0) return;
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
    if (g_use_sound)  UnloadSound(g_bell_sound);
    if (g_use_stream) UnloadAudioStream(g_bell_stream);
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
    char path[MAX_PATH];
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
    char path[MAX_PATH];
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

    char line[MAX_PATH * 2];
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
            for (int i = 0; i < topic_count; i++)
                if (strcmp(topic_names[i], lbl) == 0) { topic_secs[i] += secs; found = 1; break; }
            if (!found && topic_count < MAX_TOPICS) {
                snprintf(topic_names[topic_count], MAX_LABEL, "%s", lbl);
                topic_secs[topic_count++] = secs;
            }
        }
    }
    fclose(f);

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

/* desktop notification */

static void notify_send(const char *title, const char *body) {
    /* Sanitize body: replace shell-breaking chars so neither notify-send
     * nor osascript choke on apostrophes, quotes, or backslashes.          */
    char safe[512];
    int j = 0;
    for (int i = 0; body[i] && j < (int)sizeof(safe) - 1; i++) {
        char c = body[i];
        safe[j++] = (c == '\'' || c == '"' || c == '\\') ? '-' : c;
    }
    safe[j] = '\0';

    char cmd[1024];

    /* Linux: notify-send, double-quoted (safe[] contains no double-quotes) */
    snprintf(cmd, sizeof cmd,
             "notify-send --app-name=yduts --urgency=normal \"%s\" \"%s\" 2>/dev/null &",
             title, safe);
    { int _r = system(cmd); (void)_r; }

    /* macOS fallback */
    snprintf(cmd, sizeof cmd,
             "osascript -e \"display notification \\\"%s\\\" with title \\\"%s\\\"\" 2>/dev/null &",
             safe, title);
    { int _r = system(cmd); (void)_r; }
}

/* drawing */

#define BG     (Color){ 0,   0,   0,   255 }
#define FG     (Color){ 230, 230, 230, 255 }
#define FG_DIM (Color){ 75,  75,  75,  255 }
#define FG_RED (Color){ 200, 80,  80,  255 }

static Font g_font;
static int  g_font_loaded = 0;

static void load_font(void) {
    const char *path = (g_cfg.font[0] != '\0') ? g_cfg.font : "assets/font.ttf";
    g_font = LoadFontEx(path, 300, NULL, 0);
    if (g_font.texture.id != 0) {
        SetTextureFilter(g_font.texture, TEXTURE_FILTER_BILINEAR);
        g_font_loaded = 1;
    } else if (g_cfg.font[0] != '\0') {
        fprintf(stderr, "yduts: could not load font '%s', using default\n", path);
        g_font = LoadFontEx("assets/font.ttf", 300, NULL, 0);
        if (g_font.texture.id != 0) {
            SetTextureFilter(g_font.texture, TEXTURE_FILTER_BILINEAR);
            g_font_loaded = 1;
        }
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

static void draw_frame(void) {
    int W = GetScreenWidth(), H = GetScreenHeight();
    float cy = H * 0.5f;

    ClearBackground(BG);

    /* main timer */
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

    /* session label above timer */
    if (g_label[0]) {
        float lbl_sz = H * 0.028f;
        if (lbl_sz < 12) lbl_sz = 12;
        draw_centered_text(g_label, cy - font_sz * 0.58f, lbl_sz, FG_DIM);
    }

    /* mode/status line below timer */
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
    if (g_cfg.mute) {
        float msz = H * 0.018f;
        if (msz < 10) msz = 10;
        float spacing = msz * 0.04f;
        Vector2 sz  = MeasureTextEx(g_font, "muted", msz, spacing);
        Vector2 pos = { W - sz.x - W * 0.02f, H * 0.03f };
        DrawTextEx(g_font, "muted", pos, msz, spacing, (Color){40,40,40,255});
    }

    /* key hints (bottom) */
    const char *hints = "SPACE pause · r restart · m mute · f fullscreen · q quit";
    if (g_mode == MODE_POMODORO)
        hints = "SPACE pause · r restart · n next · m mute · f fullscreen · q quit";
    float hint_sz = H * 0.016f;
    if (hint_sz < 10) hint_sz = 10;
    draw_centered_text(hints, H - H * 0.04f, hint_sz, (Color){40,40,40,255});
}

/* LOGIC */

static void restart_session(void) {
    log_session("restarted");
    g_active_secs   = 0.0;
    g_session_start = time(NULL);
    g_finished = 0;
    switch (g_mode) {
        case MODE_STOPWATCH: g_elapsed = 0.0; break;
        case MODE_COUNTDOWN: g_elapsed = g_target; break;
        case MODE_POMODORO:
            g_elapsed = (g_pomo_phase == POMO_WORK)
                        ? g_cfg.pomo_work_secs : g_cfg.pomo_break_secs;
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
    g_elapsed  = (g_pomo_phase == POMO_WORK)
                 ? g_cfg.pomo_work_secs : g_cfg.pomo_break_secs;
}

static void on_finish(void) {
    bell_play();

    /* desktop notification */
    if (g_mode == MODE_POMODORO) {
        const char *phase = (g_pomo_phase == POMO_WORK) ? "Work" : "Break";
        char body[256];
        snprintf(body, sizeof body, "%s session done. Press n to continue.",
                 g_label[0] ? g_label : phase);
        notify_send("yduts", body);
    } else {
        char body[256];
        snprintf(body, sizeof body, "%s - time's up.",
                 g_label[0] ? g_label : "countdown");
        notify_send("yduts", body);
    }
}

static void handle_input(void) {
    if (IsKeyPressed(KEY_SPACE)) g_paused = !g_paused;
    if (IsKeyPressed(KEY_R))     restart_session();
    if (IsKeyPressed(KEY_F))     ToggleFullscreen();
    if (IsKeyPressed(KEY_M))     g_cfg.mute = !g_cfg.mute;
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
                on_finish();
            }
            break;
        case MODE_POMODORO:
            g_elapsed -= dt;
            if (g_elapsed <= 0) {
                g_elapsed  = 0;
                g_finished = 1;
                on_finish();
            }
            break;
    }
}

int main(int argc, char **argv) {
    /* load config before anything else */
    config_init_file();
    config_load();

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
        g_elapsed    = g_cfg.pomo_work_secs;
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
