/**
 * ════════════════════════════════════════════════════════════
 *  M5Stack Tab5 - Tilt Marble Maze v2
 *  BMI270 IMU → Physics Engine → 32x18 Grid Maze
 *  3 Levels, Countdown Timer, Audio Feedback
 * ════════════════════════════════════════════════════════════
 *
 *  Tilt the tablet to roll the marble through the maze.
 *  Reach the gold goal before time runs out.
 *  3 levels: Easy (90s), Medium (60s), Hard (45s)
 *
 *  Boards: M5Stack 3.2.6
 */

#include <M5Unified.h>

// ── Display & Grid ──────────────────────────────────────────
#define SCREEN_W 1280
#define SCREEN_H  720
#define GRID_COLS   32
#define GRID_ROWS   18
#define CELL_W      40
#define CELL_H      40

// ── Marble ──────────────────────────────────────────────────
#define MARBLE_R 12

// ── Cell types ──────────────────────────────────────────────
#define CELL_PATH  0
#define CELL_WALL  1
#define CELL_GOAL  2
#define CELL_START 3

// ── Game states ─────────────────────────────────────────────
enum GameState {
  STATE_TITLE,
  STATE_PLAYING,
  STATE_WIN,
  STATE_GAMEOVER
};

// ── Level config ────────────────────────────────────────────
#define NUM_LEVELS 3
static const int time_limits[NUM_LEVELS] = { 90, 60, 45 };

// ── Physics constants ───────────────────────────────────────
static const float GRAVITY  = 1.2f;
static const float FRICTION = 0.92f;
static const float BOUNCE   = -0.3f;
static const float MAX_VEL  = 8.0f;

// ── Game variables ──────────────────────────────────────────
GameState state = STATE_TITLE;
int current_level = 0;
float marble_x, marble_y;
float prev_mx, prev_my;
float vel_x = 0, vel_y = 0;
unsigned long start_time;
int last_displayed_sec = -1;
float win_time = 0;

// ── Level 1: Easy - wide corridors ─────────────────────────
static const uint8_t maze1[18][32] = {
  {1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1},
  {1,3,0,0,0,0,0,0,0,0,1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,1},
  {1,0,1,1,1,1,1,1,1,0,1,0,1,1,1,1,1,1,1,1,1,1,0,1,1,1,1,1,1,1,0,1},
  {1,0,0,0,0,0,0,0,1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,1,0,1},
  {1,0,1,1,1,1,1,0,1,1,1,1,1,1,1,0,1,1,1,1,1,1,1,1,1,1,1,1,0,1,0,1},
  {1,0,0,0,0,0,1,0,0,0,0,0,0,0,1,0,0,0,0,0,0,0,0,0,0,0,0,1,0,0,0,1},
  {1,1,1,1,1,0,1,1,1,1,1,1,1,0,1,0,1,1,1,1,1,1,1,1,1,1,0,1,1,1,0,1},
  {1,0,0,0,0,0,0,0,0,0,0,0,1,0,0,0,1,0,0,0,0,0,0,0,0,0,0,0,0,1,0,1},
  {1,0,1,1,1,1,1,1,1,1,1,0,1,1,1,1,1,0,1,1,1,1,1,1,1,1,1,1,0,1,0,1},
  {1,0,0,0,0,0,0,0,0,0,1,0,0,0,0,0,0,0,1,0,0,0,0,0,0,0,0,1,0,0,0,1},
  {1,0,1,1,1,1,1,1,1,0,1,1,1,1,1,1,1,1,1,0,1,1,1,1,1,1,0,1,1,1,1,1},
  {1,0,0,0,0,0,0,0,1,0,0,0,0,0,0,0,0,0,0,0,1,0,0,0,0,0,0,0,0,0,0,1},
  {1,1,1,0,1,1,1,0,1,1,1,1,1,0,1,1,1,1,1,1,1,0,1,1,1,1,1,1,1,1,0,1},
  {1,0,0,0,1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,1},
  {1,0,1,1,1,0,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,0,1,1,1,1,0,1},
  {1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,1,0,0,0,0,1,0,1},
  {1,0,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,0,1,0,1,1,0,0,2,1},
  {1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1}
};

// ── Level 2: Medium - more dead ends ───────────────────────
static const uint8_t maze2[18][32] = {
  {1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1},
  {1,3,0,0,1,0,0,0,0,0,0,0,1,0,0,0,0,0,1,0,0,0,0,0,1,0,0,0,0,0,0,1},
  {1,1,1,0,1,0,1,1,1,1,1,0,1,0,1,1,1,0,1,0,1,1,1,0,1,0,1,1,1,1,0,1},
  {1,0,0,0,0,0,1,0,0,0,1,0,0,0,1,0,0,0,0,0,1,0,0,0,0,0,1,0,0,0,0,1},
  {1,0,1,1,1,1,1,0,1,0,1,1,1,1,1,0,1,1,1,1,1,0,1,1,1,1,1,0,1,1,1,1},
  {1,0,0,0,0,0,0,0,1,0,0,0,0,0,0,0,1,0,0,0,0,0,0,0,0,0,0,0,1,0,0,1},
  {1,1,1,1,1,1,1,0,1,1,1,0,1,1,1,1,1,0,1,1,1,1,1,1,1,1,1,0,1,0,1,1},
  {1,0,0,0,0,0,1,0,0,0,1,0,0,0,0,0,0,0,0,0,0,0,1,0,0,0,0,0,0,0,0,1},
  {1,0,1,1,1,0,1,1,1,0,1,1,1,1,1,0,1,1,1,1,1,0,1,0,1,1,1,1,1,1,0,1},
  {1,0,1,0,0,0,0,0,1,0,0,0,0,0,1,0,0,0,0,0,1,0,0,0,1,0,0,0,0,0,0,1},
  {1,0,1,0,1,1,1,0,1,1,1,1,1,0,1,1,1,0,1,0,1,1,1,1,1,0,1,1,1,0,1,1},
  {1,0,0,0,1,0,0,0,0,0,0,0,1,0,0,0,1,0,1,0,0,0,0,0,0,0,1,0,0,0,0,1},
  {1,1,1,1,1,0,1,1,1,1,1,0,1,1,1,0,1,0,1,1,1,0,1,1,1,0,1,0,1,1,0,1},
  {1,0,0,0,0,0,0,0,0,0,1,0,0,0,0,0,1,0,0,0,1,0,0,0,1,0,0,0,1,0,0,1},
  {1,0,1,0,1,1,1,1,1,0,1,1,1,1,1,1,1,1,1,0,1,1,1,0,1,1,1,1,1,0,1,1},
  {1,0,1,0,0,0,0,0,1,0,0,0,0,0,0,0,0,0,0,0,0,0,1,0,0,0,0,0,0,0,0,1},
  {1,0,1,1,1,1,1,0,1,1,1,1,1,1,1,1,1,1,1,1,1,0,1,1,1,1,1,1,1,0,2,1},
  {1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1}
};

// ── Level 3: Hard - narrow paths, tight turns ──────────────
static const uint8_t maze3[18][32] = {
  {1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1},
  {1,3,0,1,0,0,0,1,0,0,0,1,0,0,0,1,0,0,0,1,0,0,0,1,0,0,0,0,0,0,0,1},
  {1,1,0,1,0,1,0,1,0,1,0,1,0,1,0,0,0,1,0,1,0,1,0,1,0,1,1,1,1,1,0,1},
  {1,0,0,0,0,1,0,0,0,1,0,0,0,1,1,1,0,1,0,0,0,1,0,0,0,1,0,0,0,0,0,1},
  {1,0,1,1,1,1,1,1,1,1,1,1,0,0,0,1,0,1,1,1,1,1,1,1,0,1,0,1,1,1,1,1},
  {1,0,0,0,0,0,0,0,0,0,0,1,1,1,0,1,0,0,0,0,0,0,0,1,0,0,0,0,0,0,0,1},
  {1,1,1,0,1,1,1,1,1,1,0,0,0,1,0,1,1,1,0,1,1,1,0,1,1,1,1,1,1,1,0,1},
  {1,0,0,0,0,0,0,0,0,1,1,1,0,1,0,0,0,1,0,1,0,0,0,0,0,0,0,0,0,1,0,1},
  {1,0,1,1,1,1,1,1,0,0,0,1,0,1,1,1,0,1,0,1,0,1,1,1,1,1,1,1,0,1,0,1},
  {1,0,1,0,0,0,0,1,1,1,0,1,0,0,0,0,0,1,0,0,0,1,0,0,0,0,0,1,0,0,0,1},
  {1,0,1,0,1,1,0,0,0,1,0,1,1,1,1,1,0,1,1,1,1,1,0,1,1,1,0,1,1,1,0,1},
  {1,0,0,0,1,0,1,1,0,0,0,0,0,0,0,1,0,0,0,0,0,0,0,1,0,0,0,0,0,1,0,1},
  {1,1,1,0,1,0,0,1,1,1,1,1,1,1,0,1,1,1,1,1,1,1,0,1,0,1,1,1,0,1,0,1},
  {1,0,0,0,1,1,0,0,0,0,0,0,0,1,0,0,0,0,0,0,0,1,0,0,0,1,0,0,0,1,0,1},
  {1,0,1,0,0,1,1,1,1,1,1,1,0,1,1,1,0,1,1,1,0,1,1,1,1,1,0,1,1,1,0,1},
  {1,0,1,1,0,0,0,0,0,0,0,1,0,0,0,1,0,0,0,1,0,0,0,0,0,0,0,1,0,0,0,1},
  {1,0,0,1,1,1,1,1,1,1,0,1,1,1,0,1,1,1,0,1,1,1,1,1,1,1,1,1,0,1,2,1},
  {1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1}
};

// Pointer to current maze
const uint8_t (*current_maze)[32];

// ── Sound functions ─────────────────────────────────────────

void sound_button() {
  M5.Speaker.tone(1200, 50);
}

void sound_win() {
  M5.Speaker.tone(523, 150);  // C5
  delay(160);
  M5.Speaker.tone(659, 150);  // E5
  delay(160);
  M5.Speaker.tone(784, 150);  // G5
  delay(160);
  M5.Speaker.tone(1047, 300); // C6
}

void sound_countdown(int sec) {
  if (sec <= 3) {
    M5.Speaker.tone(1500, 80); // Urgent
  } else if (sec <= 10) {
    M5.Speaker.tone(1000, 40); // Normal tick
  }
}

void sound_gameover() {
  M5.Speaker.tone(400, 200);
  delay(220);
  M5.Speaker.tone(300, 200);
  delay(220);
  M5.Speaker.tone(200, 400);
}

// ── Maze helpers ────────────────────────────────────────────

uint8_t get_cell(int col, int row) {
  if (col < 0 || col >= GRID_COLS || row < 0 || row >= GRID_ROWS)
    return CELL_WALL;
  return current_maze[row][col];
}

bool is_wall(float px, float py) {
  int col = (int)(px / CELL_W);
  int row = (int)(py / CELL_H);
  return get_cell(col, row) == CELL_WALL;
}

bool is_goal(float px, float py) {
  int col = (int)(px / CELL_W);
  int row = (int)(py / CELL_H);
  return get_cell(col, row) == CELL_GOAL;
}

void find_start(float &sx, float &sy) {
  for (int r = 0; r < GRID_ROWS; r++) {
    for (int c = 0; c < GRID_COLS; c++) {
      if (current_maze[r][c] == CELL_START) {
        sx = c * CELL_W + CELL_W / 2;
        sy = r * CELL_H + CELL_H / 2;
        return;
      }
    }
  }
  sx = CELL_W + CELL_W / 2;
  sy = CELL_H + CELL_H / 2;
}

// ── Drawing functions ───────────────────────────────────────

void draw_maze() {
  M5.Lcd.fillScreen(TFT_BLACK);
  for (int r = 0; r < GRID_ROWS; r++) {
    for (int c = 0; c < GRID_COLS; c++) {
      int x = c * CELL_W;
      int y = r * CELL_H;
      uint8_t cell = current_maze[r][c];
      if (cell == CELL_WALL) {
        M5.Lcd.fillRect(x, y, CELL_W, CELL_H, M5.Lcd.color565(0, 0, 120));
        M5.Lcd.drawRect(x, y, CELL_W, CELL_H, M5.Lcd.color565(0, 0, 80));
      } else if (cell == CELL_GOAL) {
        M5.Lcd.fillRect(x, y, CELL_W, CELL_H, M5.Lcd.color565(255, 200, 0));
      } else if (cell == CELL_START) {
        M5.Lcd.fillRect(x, y, CELL_W, CELL_H, M5.Lcd.color565(0, 180, 0));
      }
    }
  }
}

void draw_marble() {
  // Erase previous position by redrawing affected cells
  int old_col_min = max(0, (int)((prev_mx - MARBLE_R) / CELL_W));
  int old_col_max = min(GRID_COLS - 1, (int)((prev_mx + MARBLE_R) / CELL_W));
  int old_row_min = max(0, (int)((prev_my - MARBLE_R) / CELL_H));
  int old_row_max = min(GRID_ROWS - 1, (int)((prev_my + MARBLE_R) / CELL_H));

  for (int r = old_row_min; r <= old_row_max; r++) {
    for (int c = old_col_min; c <= old_col_max; c++) {
      int x = c * CELL_W;
      int y = r * CELL_H;
      uint8_t cell = current_maze[r][c];
      if (cell == CELL_WALL) {
        M5.Lcd.fillRect(x, y, CELL_W, CELL_H, M5.Lcd.color565(0, 0, 120));
        M5.Lcd.drawRect(x, y, CELL_W, CELL_H, M5.Lcd.color565(0, 0, 80));
      } else if (cell == CELL_GOAL) {
        M5.Lcd.fillRect(x, y, CELL_W, CELL_H, M5.Lcd.color565(255, 200, 0));
      } else if (cell == CELL_START) {
        M5.Lcd.fillRect(x, y, CELL_W, CELL_H, M5.Lcd.color565(0, 180, 0));
      } else {
        M5.Lcd.fillRect(x, y, CELL_W, CELL_H, TFT_BLACK);
      }
    }
  }

  // Draw marble with 3D shading
  int mx = (int)marble_x;
  int my = (int)marble_y;
  M5.Lcd.fillCircle(mx, my, MARBLE_R, M5.Lcd.color565(160, 160, 170));
  M5.Lcd.fillCircle(mx - 3, my - 3, MARBLE_R - 3, M5.Lcd.color565(200, 200, 210));
  M5.Lcd.fillCircle(mx - 5, my - 5, 3, TFT_WHITE);
}

void draw_timer() {
  int elapsed = (millis() - start_time) / 1000;
  int remaining = time_limits[current_level] - elapsed;
  if (remaining < 0) remaining = 0;

  if (remaining == last_displayed_sec) return;
  last_displayed_sec = remaining;

  // Sound
  sound_countdown(remaining);

  // Color coding
  uint16_t color = TFT_WHITE;
  if (remaining <= 10) color = TFT_RED;
  else if (remaining <= 20) color = TFT_YELLOW;

  // Clear timer area and draw
  M5.Lcd.fillRect(SCREEN_W - 160, 0, 160, 36, TFT_BLACK);
  M5.Lcd.setTextColor(color, TFT_BLACK);
  M5.Lcd.setTextSize(3);
  M5.Lcd.setCursor(SCREEN_W - 150, 8);
  M5.Lcd.printf("Time: %02d", remaining);

  // Level indicator
  M5.Lcd.fillRect(0, 0, 160, 36, TFT_BLACK);
  M5.Lcd.setTextColor(TFT_WHITE, TFT_BLACK);
  M5.Lcd.setCursor(10, 8);
  M5.Lcd.printf("Level %d/%d", current_level + 1, NUM_LEVELS);
}

void draw_title() {
  M5.Lcd.fillScreen(TFT_BLACK);
  M5.Lcd.setTextColor(TFT_WHITE);
  M5.Lcd.setTextSize(4);
  M5.Lcd.setCursor(380, 200);
  M5.Lcd.print("MARBLE MAZE");

  M5.Lcd.setTextSize(2);
  M5.Lcd.setCursor(440, 280);
  M5.Lcd.print("Tilt to roll. Beat the clock.");

  // START button
  M5.Lcd.fillRoundRect(520, 400, 240, 80, 10, M5.Lcd.color565(0, 150, 0));
  M5.Lcd.setTextColor(TFT_WHITE);
  M5.Lcd.setTextSize(3);
  M5.Lcd.setCursor(580, 425);
  M5.Lcd.print("START");
}

void draw_win_screen() {
  M5.Lcd.fillRect(300, 200, 680, 300, M5.Lcd.color565(0, 100, 0));
  M5.Lcd.drawRect(300, 200, 680, 300, TFT_GREEN);
  M5.Lcd.setTextColor(TFT_WHITE);
  M5.Lcd.setTextSize(4);
  M5.Lcd.setCursor(440, 240);
  M5.Lcd.print("LEVEL CLEAR!");
  M5.Lcd.setTextSize(2);
  M5.Lcd.setCursor(480, 310);
  M5.Lcd.printf("Time: %.1f seconds", win_time);
  M5.Lcd.setCursor(460, 370);
  if (current_level < NUM_LEVELS - 1) {
    M5.Lcd.print("Tap anywhere for next level");
  } else {
    M5.Lcd.print("All levels complete! Tap to replay");
  }
}

void draw_gameover_screen() {
  M5.Lcd.fillRect(300, 200, 680, 300, M5.Lcd.color565(120, 0, 0));
  M5.Lcd.drawRect(300, 200, 680, 300, TFT_RED);
  M5.Lcd.setTextColor(TFT_WHITE);
  M5.Lcd.setTextSize(4);
  M5.Lcd.setCursor(460, 240);
  M5.Lcd.print("TIME'S UP!");
  M5.Lcd.setTextSize(2);
  M5.Lcd.setCursor(470, 330);
  M5.Lcd.print("Tap anywhere to retry");
}

// ── Physics ─────────────────────────────────────────────────

void update_physics(float tilt_x, float tilt_y) {
  vel_x += tilt_x * GRAVITY;
  vel_y += tilt_y * GRAVITY;
  vel_x *= FRICTION;
  vel_y *= FRICTION;

  // Cap velocity
  if (vel_x > MAX_VEL) vel_x = MAX_VEL;
  if (vel_x < -MAX_VEL) vel_x = -MAX_VEL;
  if (vel_y > MAX_VEL) vel_y = MAX_VEL;
  if (vel_y < -MAX_VEL) vel_y = -MAX_VEL;

  prev_mx = marble_x;
  prev_my = marble_y;

  // X movement with collision
  float new_x = marble_x + vel_x;
  bool hit_x = false;
  // Check 6 points around marble edge (X axis)
  float check_pts_x[] = { new_x + MARBLE_R, new_x - MARBLE_R };
  float check_pts_y[] = { marble_y, marble_y - MARBLE_R + 2, marble_y + MARBLE_R - 2 };
  for (int i = 0; i < 2 && !hit_x; i++) {
    for (int j = 0; j < 3 && !hit_x; j++) {
      if (is_wall(check_pts_x[i], check_pts_y[j])) {
        hit_x = true;
      }
    }
  }
  if (hit_x) {
    vel_x *= BOUNCE;
  } else {
    marble_x = new_x;
  }

  // Y movement with collision
  float new_y = marble_y + vel_y;
  bool hit_y = false;
  float check_pts_x2[] = { marble_x, marble_x - MARBLE_R + 2, marble_x + MARBLE_R - 2 };
  float check_pts_y2[] = { new_y + MARBLE_R, new_y - MARBLE_R };
  for (int i = 0; i < 3 && !hit_y; i++) {
    for (int j = 0; j < 2 && !hit_y; j++) {
      if (is_wall(check_pts_x2[i], check_pts_y2[j])) {
        hit_y = true;
      }
    }
  }
  if (hit_y) {
    vel_y *= BOUNCE;
  } else {
    marble_y = new_y;
  }

  // Clamp to screen bounds
  marble_x = constrain(marble_x, MARBLE_R, SCREEN_W - MARBLE_R);
  marble_y = constrain(marble_y, MARBLE_R, SCREEN_H - MARBLE_R);
}

// ── Level management ────────────────────────────────────────

void load_level(int level) {
  switch (level) {
    case 0: current_maze = maze1; break;
    case 1: current_maze = maze2; break;
    case 2: current_maze = maze3; break;
    default: current_maze = maze1; break;
  }
  find_start(marble_x, marble_y);
  prev_mx = marble_x;
  prev_my = marble_y;
  vel_x = 0;
  vel_y = 0;
  start_time = millis();
  last_displayed_sec = -1;
  draw_maze();
}

// ── Setup ───────────────────────────────────────────────────

void setup() {
  auto cfg = M5.config();
  M5.begin(cfg);

  M5.Lcd.setRotation(1);
  M5.Lcd.fillScreen(TFT_BLACK);

  M5.Speaker.begin();
  M5.Speaker.setVolume(180);

  state = STATE_TITLE;
  draw_title();
}

// ── Loop (state machine) ───────────────────────────────────

void loop() {
  M5.update();

  switch (state) {
    case STATE_TITLE: {
      auto t = M5.Touch.getDetail();
      if (t.wasPressed()) {
        if (t.x >= 520 && t.x <= 760 && t.y >= 400 && t.y <= 480) {
          sound_button();
          current_level = 0;
          load_level(current_level);
          state = STATE_PLAYING;
        }
      }
      break;
    }

    case STATE_PLAYING: {
      M5.Imu.update();
      auto imu = M5.Imu.getImuData();

      // Tab5 landscape axis mapping
      float tilt_x = imu.accel.y;
      float tilt_y = -imu.accel.x;

      update_physics(tilt_x, tilt_y);
      draw_marble();
      draw_timer();

      // Check win
      if (is_goal(marble_x, marble_y)) {
        win_time = (millis() - start_time) / 1000.0f;
        sound_win();
        draw_win_screen();
        state = STATE_WIN;
      }

      // Check timeout
      int elapsed = (millis() - start_time) / 1000;
      if (elapsed >= time_limits[current_level]) {
        sound_gameover();
        draw_gameover_screen();
        state = STATE_GAMEOVER;
      }

      delay(16); // ~60 FPS
      break;
    }

    case STATE_WIN: {
      auto t = M5.Touch.getDetail();
      if (t.wasPressed()) {
        sound_button();
        if (current_level < NUM_LEVELS - 1) {
          current_level++;
        } else {
          current_level = 0;
        }
        load_level(current_level);
        state = STATE_PLAYING;
      }
      break;
    }

    case STATE_GAMEOVER: {
      auto t = M5.Touch.getDetail();
      if (t.wasPressed()) {
        sound_button();
        load_level(current_level);
        state = STATE_PLAYING;
      }
      break;
    }
  }
}
