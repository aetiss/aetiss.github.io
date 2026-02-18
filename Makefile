# ─── Makefile — aetiss.github.io ───────────────────────────────
# Requires Emscripten. Set up with:
#   source ~/emsdk/emsdk_env.sh

EMCC = emcc

# ─── Blog flags (no SDL2, DOM manipulation via EM_JS) ───────────
BLOG_FLAGS = \
    -O2 \
    -sMODULARIZE=1 \
    -sEXPORT_NAME=createBlog \
    -sEXPORTED_FUNCTIONS=_main,_blog_route \
    -sEXPORTED_RUNTIME_METHODS=UTF8ToString,stringToUTF8,lengthBytesUTF8 \
    -sALLOW_MEMORY_GROWTH=1

# ─── Game flags (SDL2, canvas rendering) ────────────────────────
GAME_FLAGS = \
    -O2 \
    -sUSE_SDL=2 \
    -sMODULARIZE=1 \
    -sALLOW_MEMORY_GROWTH=1

# ─── Targets ────────────────────────────────────────────────────
.PHONY: all blog snake breakout blocks maze clean serve

all: blog snake breakout blocks maze

blog: build/blog.js
build/blog.js: src/blog.c src/posts/why-c-wasm.h src/posts/wasm-games.h
	@mkdir -p build
	$(EMCC) src/blog.c -o build/blog.js $(BLOG_FLAGS)
	@echo "✓ blog"

snake: build/snake.js
build/snake.js: src/snake.c
	@mkdir -p build
	$(EMCC) src/snake.c -o build/snake.js $(GAME_FLAGS) -sEXPORT_NAME=createSnake
	@echo "✓ snake"

breakout: build/breakout.js
build/breakout.js: src/breakout.c
	@mkdir -p build
	$(EMCC) src/breakout.c -o build/breakout.js $(GAME_FLAGS) -sEXPORT_NAME=createBreakout -lm
	@echo "✓ breakout"

blocks: build/blocks.js
build/blocks.js: src/blocks.c
	@mkdir -p build
	$(EMCC) src/blocks.c -o build/blocks.js $(GAME_FLAGS) -sEXPORT_NAME=createBlocks
	@echo "✓ blocks"

maze: build/maze.js
build/maze.js: src/maze.c
	@mkdir -p build
	$(EMCC) src/maze.c -o build/maze.js $(GAME_FLAGS) -sEXPORT_NAME=createMaze -lm
	@echo "✓ maze"

clean:
	rm -f build/*.js build/*.wasm

serve:
	python3 -m http.server 8080
