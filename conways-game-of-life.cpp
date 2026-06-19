#include <SDL.h>
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <iostream>
#include <random>
#include <string>
#include <vector>

struct Color {
    uint8_t r, g, b;
};

static inline float clamp01(float x) {
    return std::max(0.0f, std::min(1.0f, x));
}

static Color lerp(const Color& a, const Color& b, float t) {
    t = clamp01(t);
    return {
        static_cast<uint8_t>(a.r + (b.r - a.r) * t),
        static_cast<uint8_t>(a.g + (b.g - a.g) * t),
        static_cast<uint8_t>(a.b + (b.b - a.b) * t)
    };
}

enum class ViewMode {
    Classic = 0,
    AgeGlow = 1,
    Density = 2,
    Trails = 3
};

struct App {
    static constexpr int GRID_W = 220;
    static constexpr int GRID_H = 160;
    static constexpr int WINDOW_W = 1400;
    static constexpr int WINDOW_H = 900;

    SDL_Window* window = nullptr;
    SDL_Renderer* renderer = nullptr;

    std::vector<uint8_t> grid;
    std::vector<uint8_t> next;
    std::vector<uint16_t> age;
    std::vector<float> trail;

    bool running = true;
    bool paused = false;
    bool showGrid = false;
    bool toroidal = true;
    bool dragging = false;
    bool allowDeath = true;
    int brush = 2;
    int simStepsPerFrame = 1;
    int cellSize = 5;
    int offsetX = 140;
    int offsetY = 60;
    ViewMode viewMode = ViewMode::AgeGlow;

    std::mt19937 rng{std::random_device{}()};

    int idx(int x, int y) const { return y * GRID_W + x; }

    bool inBounds(int x, int y) const {
        return x >= 0 && x < GRID_W && y >= 0 && y < GRID_H;
    }

    uint8_t get(int x, int y) const {
        if (toroidal) {
            x = (x % GRID_W + GRID_W) % GRID_W;
            y = (y % GRID_H + GRID_H) % GRID_H;
            return grid[idx(x, y)];
        }
        if (!inBounds(x, y)) return 0;
        return grid[idx(x, y)];
    }

    int neighbors(int x, int y) const {
        int n = 0;
        for (int dy = -1; dy <= 1; ++dy) {
            for (int dx = -1; dx <= 1; ++dx) {
                if (dx == 0 && dy == 0) continue;
                n += get(x + dx, y + dy);
            }
        }
        return n;
    }

    void clearAll() {
        std::fill(grid.begin(), grid.end(), 0);
        std::fill(next.begin(), next.end(), 0);
        std::fill(age.begin(), age.end(), 0);
        std::fill(trail.begin(), trail.end(), 0.0f);
    }

    void randomize(float fill = 0.18f) {
        std::bernoulli_distribution d(fill);
        for (int y = 0; y < GRID_H; ++y) {
            for (int x = 0; x < GRID_W; ++x) {
                int i = idx(x, y);
                grid[i] = d(rng) ? 1 : 0;
                age[i] = grid[i] ? 1 : 0;
                trail[i] = grid[i] ? 0.2f : 0.0f;
            }
        }
    }

    void randomSymmetric() {
        clearAll();
        std::bernoulli_distribution d(0.22f);
        for (int y = 0; y < GRID_H; ++y) {
            for (int x = 0; x < GRID_W / 2; ++x) {
                uint8_t v = d(rng) ? 1 : 0;
                grid[idx(x, y)] = v;
                grid[idx(GRID_W - 1 - x, y)] = v;
            }
        }
        for (size_t i = 0; i < grid.size(); ++i) {
            age[i] = grid[i] ? 1 : 0;
            trail[i] = grid[i] ? 0.2f : 0.0f;
        }
    }

    void stamp(const std::vector<std::string>& pattern, int gx, int gy) {
        for (int y = 0; y < (int)pattern.size(); ++y) {
            for (int x = 0; x < (int)pattern[y].size(); ++x) {
                if (pattern[y][x] != 'O') continue;
                int px = gx + x;
                int py = gy + y;
                if (!inBounds(px, py)) continue;
                int i = idx(px, py);
                grid[i] = 1;
                age[i] = std::max<uint16_t>(age[i], 1);
                trail[i] = 0.8f;
            }
        }
    }

    void seedCenterExplosion() {
        clearAll();
        stamp({
            "..O..",
            "..O..",
            "OOOOO",
            "..O..",
            "..O.."
        }, GRID_W / 2 - 2, GRID_H / 2 - 2);
    }

    void stampGlider(int gx, int gy) {
        stamp({
            ".O.",
            "..O",
            "OOO"
        }, gx, gy);
    }

    void stampLWSS(int gx, int gy) {
        stamp({
            ".OO.O",
            "O....",
            "O...O",
            "OOOO."
        }, gx, gy);
    }

    void stampPulsar(int gx, int gy) {
        stamp({
            "..OOO...OOO..",
            "..............",
            "O....O.O....O",
            "O....O.O....O",
            "O....O.O....O",
            "..OOO...OOO..",
            "..............",
            "..OOO...OOO..",
            "O....O.O....O",
            "O....O.O....O",
            "O....O.O....O",
            "..............",
            "..OOO...OOO.."
        }, gx, gy);
    }

    void stampGosperGun(int gx, int gy) {
        stamp({
            "........................O...........",
            "......................O.O...........",
            "............OO......OO............OO",
            "...........O...O....OO............OO",
            "OO........O.....O...OO..............",
            "OO........O...O.OO....O.O...........",
            "..........O.....O.......O...........",
            "...........O...O....................",
            "............OO......................"
        }, gx, gy);
    }

    void updateTitle() {
        std::string mode;
        switch (viewMode) {
            case ViewMode::Classic: mode = "Classic"; break;
            case ViewMode::AgeGlow: mode = "Age Glow"; break;
            case ViewMode::Density: mode = "Density"; break;
            case ViewMode::Trails: mode = "Trails"; break;
        }
        std::string title = "Fancy Conway's Game of Life | " + mode +
                            " | " + (paused ? "Paused" : "Running") +
                            " | Death " + (allowDeath ? "ON" : "OFF") +
                            " | Space pause | 1-4 modes | K death | R random | C clear | G grid";
        SDL_SetWindowTitle(window, title.c_str());
    }

    void step() {
        for (int y = 0; y < GRID_H; ++y) {
            for (int x = 0; x < GRID_W; ++x) {
                int i = idx(x, y);
                int n = neighbors(x, y);
                uint8_t alive = grid[i];
                uint8_t willLive;

                if (allowDeath) {
                    willLive = (alive && (n == 2 || n == 3)) || (!alive && n == 3);
                } else {
                    willLive = alive || (!alive && n == 3);
                }

                next[i] = willLive;

                if (willLive) {
                    age[i] = alive ? std::min<uint16_t>(age[i] + 1, 4095) : 1;
                    trail[i] = std::min(1.0f, trail[i] + 0.28f);
                } else {
                    age[i] = 0;
                    trail[i] *= 0.94f;
                }
            }
        }
        grid.swap(next);
        for (size_t i = 0; i < trail.size(); ++i) {
            if (!grid[i]) trail[i] *= 0.985f;
        }
    }

    Color background() const { return {5, 8, 14}; }

    Color colorForCell(int x, int y) const {
        int i = idx(x, y);
        int n = neighbors(x, y);
        bool alive = grid[i] != 0;

        if (viewMode == ViewMode::Classic) {
            return alive ? Color{235, 245, 255} : background();
        }

        if (viewMode == ViewMode::Density) {
            static const std::array<Color, 9> palette = {{
                {6, 10, 18}, {18, 28, 50}, {25, 48, 84}, {32, 74, 116}, {40, 108, 146},
                {58, 146, 165}, {92, 192, 183}, {162, 230, 220}, {240, 250, 245}
            }};
            Color base = palette[n];
            if (alive) base = lerp(base, Color{255, 255, 255}, 0.25f);
            return base;
        }

        if (viewMode == ViewMode::Trails) {
            float t = clamp01(trail[i]);
            Color dead = background();
            Color ghost = {18, 60, 70};
            Color live = {110, 250, 230};
            Color c = lerp(dead, ghost, t * 0.7f);
            if (alive) c = lerp(c, live, 0.9f);
            return c;
        }

        float a = std::min(1.0f, age[i] / 40.0f);
        Color newborn = {0, 210, 170};
        Color mature = {80, 200, 255};
        Color ancient = {255, 250, 235};
        Color c = lerp(newborn, mature, std::min(1.0f, a * 1.4f));
        if (a > 0.65f) c = lerp(mature, ancient, (a - 0.65f) / 0.35f);
        if (!alive) {
            float t = clamp01(trail[i]);
            c = lerp(background(), Color{20, 70, 78}, t * 0.8f);
        }
        return c;
    }

    void paintAtMouse(int mx, int my, bool aliveValue) {
        int gx = (mx - offsetX) / cellSize;
        int gy = (my - offsetY) / cellSize;
        for (int dy = -brush; dy <= brush; ++dy) {
            for (int dx = -brush; dx <= brush; ++dx) {
                if (dx * dx + dy * dy > brush * brush) continue;
                int x = gx + dx;
                int y = gy + dy;
                if (!inBounds(x, y)) continue;
                int i = idx(x, y);
                grid[i] = aliveValue ? 1 : 0;
                age[i] = aliveValue ? std::max<uint16_t>(age[i], 1) : 0;
                trail[i] = aliveValue ? 1.0f : 0.0f;
            }
        }
    }

    void renderCellRect(int sx, int sy, const Color& c) {
        int pad = std::max(0, cellSize / 8);
        SDL_Rect outer{sx, sy, cellSize, cellSize};
        SDL_SetRenderDrawColor(renderer, c.r / 5, c.g / 5, c.b / 5, 255);
        SDL_RenderFillRect(renderer, &outer);

        SDL_Rect inner{sx + pad, sy + pad, std::max(1, cellSize - 2 * pad), std::max(1, cellSize - 2 * pad)};
        SDL_SetRenderDrawColor(renderer, c.r, c.g, c.b, 255);
        SDL_RenderFillRect(renderer, &inner);

        if (cellSize >= 5) {
            SDL_Rect core{sx + cellSize / 4, sy + cellSize / 4, std::max(1, cellSize / 2), std::max(1, cellSize / 2)};
            SDL_SetRenderDrawColor(renderer,
                std::min(255, c.r + 25),
                std::min(255, c.g + 25),
                std::min(255, c.b + 25),
                255);
            SDL_RenderFillRect(renderer, &core);
        }
    }

    void render() {
        Color bg = background();
        SDL_SetRenderDrawColor(renderer, bg.r, bg.g, bg.b, 255);
        SDL_RenderClear(renderer);

        SDL_Rect panel{offsetX - 24, offsetY - 24, GRID_W * cellSize + 48, GRID_H * cellSize + 48};
        SDL_SetRenderDrawColor(renderer, 12, 18, 28, 255);
        SDL_RenderFillRect(renderer, &panel);
        SDL_SetRenderDrawColor(renderer, 28, 42, 58, 255);
        SDL_RenderDrawRect(renderer, &panel);

        for (int y = 0; y < GRID_H; ++y) {
            for (int x = 0; x < GRID_W; ++x) {
                Color c = colorForCell(x, y);
                if (c.r == bg.r && c.g == bg.g && c.b == bg.b && !showGrid) continue;
                int sx = offsetX + x * cellSize;
                int sy = offsetY + y * cellSize;
                renderCellRect(sx, sy, c);
            }
        }

        if (showGrid && cellSize >= 4) {
            SDL_SetRenderDrawColor(renderer, 24, 32, 48, 255);
            for (int x = 0; x <= GRID_W; ++x) {
                int sx = offsetX + x * cellSize;
                SDL_RenderDrawLine(renderer, sx, offsetY, sx, offsetY + GRID_H * cellSize);
            }
            for (int y = 0; y <= GRID_H; ++y) {
                int sy = offsetY + y * cellSize;
                SDL_RenderDrawLine(renderer, offsetX, sy, offsetX + GRID_W * cellSize, sy);
            }
        }

        SDL_RenderPresent(renderer);
    }

    bool init() {
        if (SDL_Init(SDL_INIT_VIDEO) != 0) {
            std::cerr << "SDL_Init failed: " << SDL_GetError() << "\n";
            return false;
        }

        window = SDL_CreateWindow("Fancy Conway's Game of Life",
                                  SDL_WINDOWPOS_CENTERED,
                                  SDL_WINDOWPOS_CENTERED,
                                  WINDOW_W, WINDOW_H,
                                  SDL_WINDOW_SHOWN);
        if (!window) {
            std::cerr << "SDL_CreateWindow failed: " << SDL_GetError() << "\n";
            return false;
        }

        renderer = SDL_CreateRenderer(window, -1, SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);
        if (!renderer) {
            std::cerr << "SDL_CreateRenderer failed: " << SDL_GetError() << "\n";
            return false;
        }

        grid.resize(GRID_W * GRID_H);
        next.resize(GRID_W * GRID_H);
        age.resize(GRID_W * GRID_H);
        trail.resize(GRID_W * GRID_H);

        randomize(0.17f);
        updateTitle();
        return true;
    }

    void shutdown() {
        if (renderer) SDL_DestroyRenderer(renderer);
        if (window) SDL_DestroyWindow(window);
        SDL_Quit();
    }

    void handleKey(SDL_Keycode key) {
        switch (key) {
            case SDLK_ESCAPE: running = false; break;
            case SDLK_SPACE: paused = !paused; break;
            case SDLK_c: clearAll(); break;
            case SDLK_r: randomize(0.17f); break;
            case SDLK_t: randomSymmetric(); break;
            case SDLK_e: seedCenterExplosion(); break;
            case SDLK_g: showGrid = !showGrid; break;
            case SDLK_o: toroidal = !toroidal; break;
            case SDLK_k: allowDeath = !allowDeath; break;
            case SDLK_1: viewMode = ViewMode::Classic; break;
            case SDLK_2: viewMode = ViewMode::AgeGlow; break;
            case SDLK_3: viewMode = ViewMode::Density; break;
            case SDLK_4: viewMode = ViewMode::Trails; break;
            case SDLK_EQUALS:
            case SDLK_PLUS: simStepsPerFrame = std::min(simStepsPerFrame + 1, 20); break;
            case SDLK_MINUS: simStepsPerFrame = std::max(simStepsPerFrame - 1, 1); break;
            case SDLK_LEFTBRACKET: cellSize = std::max(2, cellSize - 1); break;
            case SDLK_RIGHTBRACKET: cellSize = std::min(10, cellSize + 1); break;
            case SDLK_COMMA: brush = std::max(1, brush - 1); break;
            case SDLK_PERIOD: brush = std::min(8, brush + 1); break;
            case SDLK_s: if (paused) step(); break;
            case SDLK_q: stampGlider(8, 8); break;
            case SDLK_w: stampLWSS(20, 20); break;
            case SDLK_p: stampPulsar(GRID_W / 2 - 7, GRID_H / 2 - 6); break;
            case SDLK_h: stampGosperGun(5, GRID_H / 2 - 5); break;
            default: break;
        }
        updateTitle();
    }

    void handleEvents() {
        SDL_Event e;
        while (SDL_PollEvent(&e)) {
            if (e.type == SDL_QUIT) running = false;
            if (e.type == SDL_KEYDOWN) handleKey(e.key.keysym.sym);
            if (e.type == SDL_MOUSEBUTTONDOWN) {
                dragging = true;
                if (e.button.button == SDL_BUTTON_LEFT) paintAtMouse(e.button.x, e.button.y, true);
                if (e.button.button == SDL_BUTTON_RIGHT) paintAtMouse(e.button.x, e.button.y, false);
            }
            if (e.type == SDL_MOUSEBUTTONUP) dragging = false;
            if (e.type == SDL_MOUSEMOTION && dragging) {
                uint32_t buttons = SDL_GetMouseState(nullptr, nullptr);
                if (buttons & SDL_BUTTON(SDL_BUTTON_LEFT)) paintAtMouse(e.motion.x, e.motion.y, true);
                if (buttons & SDL_BUTTON(SDL_BUTTON_RIGHT)) paintAtMouse(e.motion.x, e.motion.y, false);
            }
            if (e.type == SDL_MOUSEWHEEL) {
                if (e.wheel.y > 0) brush = std::min(8, brush + 1);
                if (e.wheel.y < 0) brush = std::max(1, brush - 1);
            }
        }
    }

    void run() {
        std::cout << "Fancy Conway's Game of Life controls:\n"
                  << "  Left drag: paint live cells\n"
                  << "  Right drag: erase\n"
                  << "  Space: pause/resume\n"
                  << "  S: single step when paused\n"
                  << "  1..4: rendering modes\n"
                  << "  R: random seed, T: symmetric seed, E: center explosion\n"
                  << "  Q: glider, W: LWSS, P: pulsar, H: Gosper gun\n"
                  << "  +/-: simulation speed\n"
                  << "  [ ]: cell size\n"
                  << "  < >: brush radius\n"
                  << "  K: toggle death rules (starts ON)\n"
                  << "  G: toggle grid, O: toggle toroidal borders, C: clear, Esc: quit\n";

        while (running) {
            handleEvents();
            if (!paused) {
                for (int i = 0; i < simStepsPerFrame; ++i) step();
            }
            render();
            updateTitle();
        }
    }
};

int main() {
    App app;
    if (!app.init()) return 1;
    app.run();
    app.shutdown();
    return 0;
}
