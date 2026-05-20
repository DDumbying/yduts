# yduts

A minimal studying timer. Black screen. White digits. Nothing else.
Aesthetic inspired by [sowon](https://github.com/tsoding/sowon).

```
00:25:00
```

## Build

> There is no **Ubuntu / Debian** section because I don't like lazy guys.

### Arch and so on

```sh
sudo pacman -S raylib
make
```

### macOS (with Homebrew)

```sh
brew install raylib
# edit Makefile: LDFLAGS = -lraylib -lm
make
```

## Usage

```sh
./yduts                           # stopwatch (count up)
./yduts 25m                       # countdown 25 minutes
./yduts 1h30m "algorithms"        # countdown with label
./yduts pomodoro "linear algebra" # 25m/5m/15m loop with label
./yduts -p 25m "networks"         # start paused
./yduts stats                     # print study summary
```

## Keys

| Key       | Action                                    |
|-----------|-------------------------------------------|
| `SPACE`   | Pause / Resume                            |
| `r`       | Restart current session                   |
| `n`       | Next pomodoro phase (pomodoro only)       |
| `m`       | Mute / unmute bell                        |
| `f`       | Toggle fullscreen                         |
| `q`/`ESC` | Quit — saves session to log              |

## Pomodoro

Runs a standard 25m work / 5m break cycle with a long break every 4 sessions.
Phases auto-advance 3 seconds after the bell. Press `n` to skip the wait.
A row of dots below the timer shows your position in the current cycle.

All durations are configurable — see Config.

## Stats

```sh
./yduts stats
```

```
  yduts · study log
  ─────────────────────────────
  today      01h04m22s  (3 sessions)
  this week  03h24m22s  (6 sessions)
  all time   04h09m22s  (7 sessions)

  by topic:
    algorithms             01h50m00s
    operating systems      00h50m00s
    networks               00h30m00s
  ─────────────────────────────
  log: /home/user/.yduts_log
```

Topics come from the session label you pass as the second argument.

## Log

Every session is appended to `~/.yduts_log` automatically on quit, finish, or restart.

```
2026-05-19T08:00:00 | 00h25m00s | algorithms           | completed
2026-05-19T10:30:00 | 00h14m22s | linear algebra       | interrupted
```

Statuses: `completed` `interrupted` `restarted` `skipped`
Sessions under 5 seconds are not logged.

## Config

Auto-created at `~/.yduts_config` on first run.

```ini
# bell sound: path to a .wav or .ogg file
# omit to use the built-in generated tone
# bell_sound = /home/user/sounds/bell.wav

bell_volume = 1.0
mute        = false

pomodoro_work         = 25m
pomodoro_break        = 5m
pomodoro_longbreak    = 15m
pomodoro_longbreak_every = 4

# custom font: any .ttf file
# font = /home/user/.local/share/fonts/Caveat-Regular.ttf

# theme colors (6-digit hex)
# theme_fg    = e6e6e6
# theme_bg    = 000000
# theme_dim   = 4b4b4b
# theme_alert = c85050
```

### Font

The bundled font is Lora Italic. Any `.ttf` works — handwritten fonts like
[Caveat](https://fonts.google.com/specimen/Caveat) or [Kalam](https://fonts.google.com/specimen/Kalam) match the aesthetic best.

```sh
mkdir -p ~/.local/share/fonts
cp Caveat-Regular.ttf ~/.local/share/fonts/
fc-cache -fv
# then set font = ... in ~/.yduts_config
```

### Theming

Uncomment and edit the `theme_*` keys in the config. Example presets:

```ini
# amber
theme_fg    = f5c97a
theme_bg    = 1a1008
theme_dim   = 5a4a2a
theme_alert = e05555

# green terminal
theme_fg    = 39d353
theme_bg    = 0d1117
theme_dim   = 1e4023
theme_alert = ff6b6b
```

## Notifications

Fires a desktop notification when a session ends.
Uses `notify-send` on Linux, `osascript` on macOS. Silent fail if neither is available.
