# aetiss.github.io

Personal site built entirely in C compiled to WebAssembly.

- **Blog** — C/WASM blog engine with markdown-lite parser, hash routing, EM_JS DOM manipulation
- **Games** — 4 retro games written in C with SDL3, rendered on `<canvas>` via WebAssembly

Live at: https://aetiss.github.io/

---

## Prerequisites

You need [Emscripten](https://emscripten.org/) installed and activated.

```sh
# Install emsdk (one-time)
git clone https://github.com/emscripten-core/emsdk.git ~/emsdk
cd ~/emsdk
./emsdk install latest
./emsdk activate latest

# Activate in every new shell session (or add to ~/.zshrc / ~/.bashrc)
source ~/emsdk/emsdk_env.sh
```

Verify it works:
```sh
emcc --version   # should print: emcc (Emscripten gcc/clang-like replacement) 3.x.x / 4.x.x / 5.x.x
```

---

## Build

```sh
# Build everything (blog + all 4 games)
make

# Or build individual targets
make blog
make snake
make breakout
make blocks
make maze

# Clean generated files
make clean

# Rebuild from scratch
make clean && make
```

Output goes to `build/` as `.js` + `.wasm` pairs.

---

## Run locally

> **Important:** WASM modules cannot be loaded via `file://` URLs due to browser CORS restrictions.
> You must serve from a local HTTP server.

```sh
# Start local server at http://localhost:8080
make serve

# Or directly:
python3 -m http.server 8080
```

Then open http://localhost:8080 in your browser.

### Quick links (local)
| Page | URL |
|------|-----|
| Landing | http://localhost:8080 |
| Blog | http://localhost:8080/blog.html |
| Games | http://localhost:8080/games.html |
| Snake | http://localhost:8080/games/snake.html |
| Breakout | http://localhost:8080/games/breakout.html |
| Blocks | http://localhost:8080/games/blocks.html |
| Maze | http://localhost:8080/games/maze.html |

---

## Project structure

```
Makefile                  — build orchestration (emcc targets)
README.md
index.html                — landing page
blog.html                 — blog list + post view (hash routing)
games.html                — game selection grid
games/
  snake.html
  breakout.html
  blocks.html
  maze.html
src/
  blog.c                  — blog engine (EM_JS DOM, markdown parser)
  snake.c                 — Snake (SDL3)
  breakout.c              — Breakout / Arkanoid (SDL3)
  blocks.c                — Tetris-like (SDL3)
  maze.c                  — Wolfenstein-style 3D raycaster (SDL3)
  posts/
    why-c-wasm.h          — blog post (C struct)
    wasm-games.h          — blog post (C struct)
css/
  common.css              — shared nav, theme toggle, reset
  blog.css                — typography, post layout
  games.css               — dark retro grid, glow effects
build/                    — generated .js + .wasm (committed for GitHub Pages)
```

---

## Games — controls

| Game | Controls |
|------|----------|
| **Snake** | Arrow keys / WASD to steer, R to restart |
| **Breakout** | Mouse or ← → / A D to move paddle, R to restart |
| **Blocks** | ← → / A D to move, ↑ / W to rotate, ↓ / S to soft drop, Space for hard drop, R to restart |
| **Maze** | WASD / arrow keys to move, ← → / mouse to rotate |

---

## Tech stack

| Layer | Tech |
|-------|------|
| Language | C (C11) |
| Compiler | [Emscripten](https://emscripten.org/) → WebAssembly |
| Game rendering | SDL3 on `<canvas>` via Emscripten SDL port |
| Blog DOM | `EM_JS` macros (C calling JavaScript) |
| Hosting | GitHub Pages (static, no server) |

---

## Deploying

All `.js` and `.wasm` files in `build/` are committed. GitHub Pages serves them directly — no build step on the server.

```sh
# Build, commit, push
make
git add build/ src/ css/ *.html *.md Makefile
git commit -m "your message"
git push origin wasm-site   # or master for live deploy
```
