# yduts

A minimal studying timer. Black screen. White digits. Nothing else.

Aesthetic inspired by [sowon](https://github.com/tsoding/sowon).

```
00:25:00
```

---

## Build

### Ubuntu / Debian

```sh
sudo apt-get install -y libgl-dev libxrandr-dev libxi-dev libxcursor-dev libxinerama-dev cmake git
git clone --depth 1 --branch 5.0 https://github.com/raysan5/raylib.git /tmp/raylib
cd /tmp/raylib && mkdir build && cd build
cmake -DPLATFORM=Desktop -DCMAKE_BUILD_TYPE=Release -DBUILD_SHARED_LIBS=OFF .. -DCMAKE_INSTALL_PREFIX=/usr/local
make -j$(nproc) && sudo make install
cd /path/to/yduts && make
```

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

---

## Usage

```sh
./yduts                  # stopwatch (count up)
./yduts 25m              # countdown 25 minutes
./yduts 1h30m            # countdown 1 hour 30 minutes
./yduts 90               # countdown 90 seconds (bare number)
./yduts pomodoro         # 25m work / 5m break loop
./yduts -p 25m           # start paused
```

---

## Keys

| Key     | Action                              |
|---------|-------------------------------------|
| `SPACE` | Pause / Resume                      |
| `r`     | Restart current session             |
| `n`     | Next pomodoro phase (pomodoro only) |
| `f`     | Toggle fullscreen                   |
| `q`/`ESC` | Quit                             |

---

## Font

Place a `.ttf` font at `assets/font.ttf`.
A handwritten or italic serif font is recommended (e.g. [Caveat](https://fonts.google.com/specimen/Caveat), [Lora Italic](https://fonts.google.com/specimen/Lora)).
