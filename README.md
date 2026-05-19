# yduts

A minimal studying timer. Black screen. White digits. Nothing else.

Aesthetic inspired by [sowon](https://github.com/tsoding/sowon).

```
00:25:00
```

## Build

> There is not **Ubuntu / Debian** because i don't like lazy guys.

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
./yduts 1h30m "algorithms"        # countdown with session label
./yduts pomodoro "linear algebra" # 25m/5m loop with label
./yduts -p 25m "networks"         # start paused
./yduts stats                     # print study summary
```

## Stats output

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

## Keys

| Key       | Action                              |
|-----------|-------------------------------------|
| `SPACE`   | Pause / Resume                      |
| `r`       | Restart current session             |
| `n`       | Next pomodoro phase (pomodoro only) |
| `f`       | Toggle fullscreen                   |
| `q`/`ESC` | Quit (saves session to log)         |

## Log format

Plain text, one line per session:

```
2026-05-19T08:00:00 | 00h25m00s | algorithms           | completed
2026-05-19T10:30:00 | 00h14m22s | linear algebra       | interrupted
```

Statuses: `completed` · `interrupted` · `restarted` · `skipped`

Sessions under 5 seconds are not logged.

## Font

Place a `.ttf` font at `assets/font.ttf`.
A handwritten or italic serif font is recommended (e.g. [Caveat](https://fonts.google.com/specimen/Caveat), [Lora Italic](https://fonts.google.com/specimen/Lora)).

## Logs

Logs every session automatically to `~/.yduts_log`.
