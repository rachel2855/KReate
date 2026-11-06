// C QUEST - a candy-coloured learning app for C basics. Plain C + raylib only.
// Build (w64devkit, raylib 5.5):
//   gcc main.c -o cquest.exe -I<raylib>/include -L<raylib>/lib -lraylib -lopengl32 -lgdi32 -lwinmm
// Two modes: MCQ (pick the answer) and TYPED (type answers, then a stretch
// "Function Lab" where your code is compiled with gcc and tested).
//
// Learning features:
//   * Level map - replay any unlocked level to improve your stars
//   * BACK / SKIP / clickable question dots - move around a level freely
//   * TRY AGAIN after a wrong answer (wrong MCQ options get crossed out)
//   * Progress Report - accuracy per level, weak spots, last-run review
//   * Weak Spot Practice - a mini level built from the questions you miss most
#include "raylib.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

#define WINDOW_WIDTH 1000
#define WINDOW_HEIGHT 640
#define LEVEL_COUNT 5
#define MAX_QUESTIONS_PER_LEVEL 5
#define MAX_BANK 64
#define MAX_PARTICLES 320
#define MAX_FLOATS 8
#define BG_CANDY_COUNT 14
#define SPARKLE_COUNT 36
#define PROGRESS_FILE "cquest_progress.dat"
#ifdef _WIN32
#define ATTEMPT_EXE_FILE "cquest_attempt.exe"
#define ATTEMPT_RUN_COMMAND "cquest_attempt.exe"
#else
#define ATTEMPT_EXE_FILE "cquest_attempt"
#define ATTEMPT_RUN_COMMAND "./cquest_attempt"
#endif

#define COLOR_BG_TOP  ((Color){104, 58, 214, 255})
#define COLOR_BG_BOT  ((Color){255, 128, 196, 255})
#define COLOR_PANEL   ((Color){44, 30, 92, 255})
#define COLOR_CREAM   ((Color){255, 247, 252, 255})
#define COLOR_INK     ((Color){58, 22, 98, 255})
#define COLOR_YELLOW  ((Color){255, 214, 64, 255})
#define COLOR_PINK    ((Color){255, 92, 168, 255})
#define COLOR_CYAN    ((Color){72, 224, 255, 255})
#define COLOR_GREEN   ((Color){96, 232, 120, 255})
#define COLOR_RED     ((Color){255, 84, 84, 255})
#define COLOR_ORANGE  ((Color){255, 150, 56, 255})
#define COLOR_PURPLE  ((Color){164, 104, 255, 255})
#define COLOR_TEXT    ((Color){240, 240, 255, 255})
#define COLOR_GRAY    ((Color){176, 164, 200, 255})
#define COLOR_CODE_BG ((Color){38, 24, 84, 255})

typedef enum { SCREEN_TITLE, SCREEN_MODE, SCREEN_MAP, SCREEN_QUESTION, SCREEN_RESULTS, SCREEN_REVIEW } ScreenState;
typedef enum { MODE_MCQ, MODE_TYPED, MODE_COUNT } QuizMode;
typedef enum { KIND_MCQ, KIND_SHORT, KIND_CODE } QuestionKind;
typedef enum { ST_UNSEEN, ST_CORRECT, ST_WRONG, ST_SKIPPED, ST_FIXED } QStatus;   // FIXED = wrong first, right on retry
typedef enum { MOOD_HAPPY, MOOD_SAD, MOOD_CHEER, MOOD_THINK } BotMood;

typedef struct {
    int levelIndex;
    QuestionKind kind;
    const char *prompt;
    const char *snippet;
    const char *options[4];
    int correctOption;
    const char *acceptedAnswers;   // typed answers, separated by '|'
    const char *hint;
    const char *explanation;
    const char *starterCode;       // KIND_CODE only
    const char *testHarness;       // KIND_CODE only: a main() printing PASS or FAIL
} Question;

typedef struct { unsigned short attempts, correct, wrong, skipped; } QStat;   // lifetime stats per question

typedef struct {
    int bestGems[MODE_COUNT][LEVEL_COUNT];
    int totalXp;
    QStat stats[MAX_BANK];   // indexed by position in questionBank; older save files simply load without it
} Progress;

typedef struct {
    ScreenState screen;
    float screenTime;
    QuizMode mode;
    int levelIndex;                         // 0..LEVEL_COUNT-1, or -1 for weak spot practice
    int questionIds[MAX_QUESTIONS_PER_LEVEL];
    QStatus qStatus[MAX_QUESTIONS_PER_LEVEL];
    bool qHint[MAX_QUESTIONS_PER_LEVEL];
    int questionCount, current;
    int streak, levelXp, levelGems, correctCount, fixedCount;
    bool lastAnswerCorrect, sheetOpen, confirmQuit;
    float sheetTime;
    int lastChoice;
    bool eliminated[4];
    int runCountdown;
    char typedAnswer[96];
    int typedLength;
    char codeAnswer[1024];
    int codeLength, codeCursor;
    char feedbackHead[32];
    char feedbackText[600], revealText[700];
    bool revealed;
    int selectedLevel;                      // map popup, -1 = closed
    int lastRunCount, lastRunIds[MAX_QUESTIONS_PER_LEVEL];
    QStatus lastRunStatus[MAX_QUESTIONS_PER_LEVEL];
    int reviewTab, reviewExpanded, reviewCount, reviewIds[MAX_BANK];
    QStatus reviewStatus[MAX_BANK];
    float reviewScroll;
    char toast[96];
    float toastTime;
    Progress progress;
} AppState;

typedef struct { Vector2 pos, vel; float life, maxLife, size; Color color; int shape; } Particle;
typedef struct { char text[24]; Vector2 pos; float life; Color color; } FloatText;

static AppState app;
static Particle particles[MAX_PARTICLES];
static FloatText floatTexts[MAX_FLOATS];
static Vector2 sparklePos[SPARKLE_COUNT];
static bool uiLocked, clickConsumed, wantHand;
static const Vector2 nodePos[LEVEL_COUNT] = { {130, 470}, {305, 385}, {480, 480}, {660, 370}, {840, 232} };
static const char *levelNames[LEVEL_COUNT] = {
    "Variables & Types", "Operators & Casting", "Decisions & Loops", "Arrays & Strings", "Function Lab (stretch)"
};
static const char *levelBlurbs[LEVEL_COUNT] = {
    "int, char, const and the printf format specifiers.",
    "Maths operators, integer division, remainders and casting.",
    "if / else, do-while, break and continue.",
    "Array indexes, the string terminator and sizeof.",
    "Write real C functions. They get compiled and tested with gcc!"
};


#define MCQ_Q(lvl, prompt, snippet, a, b, c, d, correct, hint, why) \
    { lvl, KIND_MCQ, prompt, snippet, {a, b, c, d}, correct, "", hint, why, "", "" }
#define SHORT_Q(lvl, prompt, snippet, answers, hint, why) \
    { lvl, KIND_SHORT, prompt, snippet, {"", "", "", ""}, 0, answers, hint, why, "", "" }
#define CODE_Q(lvl, prompt, starter, harness, hint, why) \
    { lvl, KIND_CODE, prompt, "", {"", "", "", ""}, 0, "", hint, why, starter, harness }

static const Question questionBank[] = {
    // ---- Level 1: Variables & Types ----
    MCQ_Q(0, "Which format specifier prints an int?", "int age = 25;",
          "%c", "%d", "%s", "%f", 1, "Think: d for decimal integer.",
          "%d is for integers. %f is float/double, %c is char, %s is a string."),
    MCQ_Q(0, "What value does an uninitialised local variable hold?", "int var;",
          "Always 0", "Whatever was already in memory", "NULL", "It will not compile", 1,
          "C does not clear memory for you.",
          "A local variable that is not initialised gets whatever bytes happened to be in memory."),
    MCQ_Q(0, "Which line makes TAX_RATE unchangeable?", "",
          "const double TAX_RATE = 0.08;", "constant double TAX_RATE = 0.08;",
          "final double TAX_RATE = 0.08;", "double TAX_RATE() = 0.08;", 0,
          "It is a short keyword, five letters.", "const makes a variable read-only. final and constant are not C keywords."),
    SHORT_Q(0, "What does this print?", "char grade = 'A';\nprintf(\"Grade: %c\\n\", grade);",
            "Grade: A", "%c prints a single character.", "%c prints the character stored in grade, so you get: Grade: A"),
    SHORT_Q(0, "Type the full format specifier that prints a float with 2 decimal places.", "",
            "%.2f", "The number after the dot is the decimal count.", "%.2f means a float with exactly 2 digits after the decimal point."),
    // ---- Level 2: Operators & Casting ----
    MCQ_Q(1, "What is 10 / 3 when both are ints?", "int a = 10, b = 3;\nprintf(\"%d\", a / b);",
          "3.33", "3", "4", "1", 1, "Integers cannot hold a fraction.", "Integer division truncates the decimal part, so 10 / 3 is 3."),
    MCQ_Q(1, "What does 10 % 3 give?", "",
          "3", "0", "1", "3.33", 2, "% is the remainder after dividing.", "10 = 3 * 3 + 1, so the remainder is 1."),
    MCQ_Q(1, "What is (float)7 / 2?", "",
          "3", "3.5", "4", "3.0", 1, "The cast happens before the division.", "Casting 7 to float first makes it a float division: 3.5."),
    SHORT_Q(1, "What does this print?", "int num = 10;\nnum += 5;\nnum *= 2;\nprintf(\"%d\", num);",
            "30", "Work line by line: 10, then 15, then...", "num becomes 15 after += 5, then 30 after *= 2."),
    SHORT_Q(1, "What does this print?", "int result = (int)3.9;\nprintf(\"%d\", result);",
            "3", "Casting to int does not round.", "Converting a float to int truncates toward zero, so 3.9 becomes 3."),
    // ---- Level 3: Decisions & Loops ----
    MCQ_Q(2, "Which loop always runs its body at least once?", "",
          "for", "while", "do-while", "if", 2, "The condition is checked at the end.", "do-while runs the body first and checks the condition afterwards."),
    MCQ_Q(2, "What does continue do inside a loop?", "",
          "Exits the loop", "Skips to the next iteration", "Restarts the program", "Ends the function", 1,
          "Think of it as: skip the rest of this lap.", "continue jumps straight to the next iteration of the loop."),
    MCQ_Q(2, "What prints?", "int score = 75;\nif (score >= 80) printf(\"B\");\nelse if (score >= 70) printf(\"C\");\nelse printf(\"F\");",
          "B", "C", "F", "BC", 1, "Only the first true branch runs.", "75 fails the >= 80 test but passes >= 70, so C prints and the else is skipped."),
    SHORT_Q(2, "What does this print? (separate numbers with spaces)",
            "for (int i = 1; i <= 5; i++) {\n    if (i % 2 == 0) continue;\n    printf(\"%d \", i);\n}",
            "1 3 5", "Even numbers are skipped.", "Even values hit continue, so only the odd numbers 1 3 5 are printed."),
    SHORT_Q(2, "Which keyword leaves a loop immediately?", "",
            "break", "Five letters. Also used in switch.", "break exits the nearest loop or switch straight away."),
    // ---- Level 4: Arrays & Strings ----
    MCQ_Q(3, "What is the index of the first element of an array?", "",
          "1", "0", "-1", "Depends on the type", 1, "C counts from zero.", "Array indexes start at 0, so numbers[0] is the first element."),
    MCQ_Q(3, "What character ends every C string?", "",
          "'\\n'", "'\\0'", "' '", "'.'", 1, "It is called the null terminator.", "The '\\0' marks the end. There is no stored length, printf just stops there."),
    MCQ_Q(3, "How do you count the elements in int values[] = {...}?", "",
          "sizeof(values)", "sizeof(values) / sizeof(values[0])", "length(values)", "values.size()", 1,
          "Total bytes divided by bytes per element.", "sizeof(values) is total bytes, so divide by the size of one element."),
    SHORT_Q(3, "What does this print?", "int numbers[5] = {10, 20, 30, 40, 50};\nprintf(\"%d\", numbers[2]);",
            "30", "Index 2 is the third element.", "Indexes run 0, 1, 2... so numbers[2] is 30."),
    SHORT_Q(3, "What does this print?", "char name[20] = \"Alice\";\nint len = 0;\nwhile (name[len] != '\\0') len++;\nprintf(\"%d\", len);",
            "5", "Count the letters before the terminator.", "The loop counts A, l, i, c, e and stops at '\\0', so len is 5."),
    // ---- Level 5: Function Lab (typed mode only, compiled and tested with gcc) ----
    CODE_Q(4, "Write int square(int x) that returns x multiplied by itself.", "int square(int x) {\n    \n}",
           "int main() { if (square(3) == 9 && square(-4) == 16 && square(0) == 0) printf(\"PASS\"); else printf(\"FAIL\"); return 0; }",
           "One line: return x * x;", "Solution: return x * x;"),
    CODE_Q(4, "Write int isEven(int n) that returns 1 if n is even, otherwise 0.", "int isEven(int n) {\n    \n}",
           "int main() { if (isEven(4) && !isEven(7) && isEven(0) && !isEven(-3)) printf(\"PASS\"); else printf(\"FAIL\"); return 0; }",
           "Use the remainder operator and compare with 0.", "Solution: return n % 2 == 0;"),
    CODE_Q(4, "Write int sumArray(int arr[], int size) that returns the sum of all elements.",
           "int sumArray(int arr[], int size) {\n    \n}",
           "int main() { int a[4] = {1, 2, 3, 4}; int b[1] = {7}; if (sumArray(a, 4) == 10 && sumArray(b, 1) == 7 && sumArray(a, 0) == 0) printf(\"PASS\"); else printf(\"FAIL\"); return 0; }",
           "Keep a running total in a for loop.", "Solution: int sum = 0; for (int i = 0; i < size; i++) sum += arr[i]; return sum;"),
};
#define QUESTION_BANK_SIZE ((int)(sizeof(questionBank) / sizeof(questionBank[0])))

typedef char bank_fits_in_progress[(QUESTION_BANK_SIZE <= MAX_BANK) ? 1 : -1];

// ================================================================ small helpers
static float Clampf(float v, float lo, float hi) { return v < lo ? lo : (v > hi ? hi : v); }
static float Smooth(float t) { t = Clampf(t, 0, 1); return t * t * (3 - 2 * t); }
static float EaseOutBack(float t) { t = Clampf(t, 0, 1); float u = t - 1; return 1 + 2.70158f * u * u * u + 1.70158f * u * u; }
static float RandF(float lo, float hi) { return lo + (float)GetRandomValue(0, 1000) / 1000.0f * (hi - lo); }
static Vector2 V2(float x, float y) { return (Vector2){ x, y }; }
static Rectangle Expand(Rectangle r, float a) { return (Rectangle){ r.x - a, r.y - a, r.width + 2 * a, r.height + 2 * a }; }
static Color CandyColor(int i) {
    switch (((i % 6) + 6) % 6) {
        case 0: return COLOR_PINK;   case 1: return COLOR_CYAN;   case 2: return COLOR_GREEN;
        case 3: return COLOR_YELLOW; case 4: return COLOR_ORANGE; default: return COLOR_PURPLE;
    }
}
static Color LevelColor(int level) {
    switch (level) { case 0: return COLOR_PINK; case 1: return COLOR_CYAN; case 2: return COLOR_GREEN; case 3: return COLOR_ORANGE; default: return COLOR_PURPLE; }
}
static const Question *QuestionAt(int bankIndex) { return &questionBank[bankIndex]; }

static void SetScreen(ScreenState screen) { app.screen = screen; app.screenTime = 0; }
static void ShowToast(const char *text) { snprintf(app.toast, sizeof app.toast, "%s", text); app.toastTime = 2.4f; }

// A click is "used up" by the first widget that takes it, so overlapping widgets never both fire.
static bool TakeClick(bool hovered) {
    if (hovered) wantHand = true;
    if (hovered && !clickConsumed && IsMouseButtonReleased(MOUSE_BUTTON_LEFT)) { clickConsumed = true; return true; }
    return false;
}
static bool HoverRect(Rectangle r) { return !uiLocked && CheckCollisionPointRec(GetMousePosition(), r); }

// ================================================================ text
static void DrawTextO(const char *text, int x, int y, int size, Color fill, Color outline) {
    int t = size >= 60 ? 4 : (size >= 30 ? 3 : 2);
    for (int dy = -t; dy <= t; dy += t)
        for (int dx = -t; dx <= t; dx += t)
            if (dx || dy) DrawText(text, x + dx, y + dy, size, outline);
    DrawText(text, x, y, size, fill);
}
static void DrawTextCO(const char *text, int cx, int y, int size, Color fill, Color outline) {
    DrawTextO(text, cx - MeasureText(text, size) / 2, y, size, fill, outline);
}
static void DrawTextCentered(const char *text, int cx, int y, int size, Color color) {
    DrawText(text, cx - MeasureText(text, size) / 2, y, size, color);
}

// Word-wraps text. With draw == false it only measures. Returns the y below the last line.
static int WrapText(const char *text, int x, int y, int maxWidth, int fontSize, Color color, bool draw) {
    char lineBuffer[256] = "";
    int lineHeight = fontSize + 6;
    const char *cursor = text;
    while (*cursor) {
        char word[64];
        int wordLength = 0;
        while (*cursor && *cursor != ' ' && *cursor != '\n' && wordLength < 63) word[wordLength++] = *cursor++;
        word[wordLength] = '\0';
        char trial[320];
        snprintf(trial, sizeof trial, "%s%s%s", lineBuffer, lineBuffer[0] ? " " : "", word);
        if (lineBuffer[0] && MeasureText(trial, fontSize) > maxWidth) {
            if (draw) DrawText(lineBuffer, x, y, fontSize, color);
            y += lineHeight;
            snprintf(lineBuffer, sizeof lineBuffer, "%s", word);
        } else {
            snprintf(lineBuffer, sizeof lineBuffer, "%s", trial);
        }
        if (*cursor == '\n') {
            if (draw) DrawText(lineBuffer, x, y, fontSize, color);
            y += lineHeight;
            lineBuffer[0] = '\0';
            cursor++;
        } else if (*cursor == ' ') {
            cursor++;
        }
    }
    if (lineBuffer[0]) { if (draw) DrawText(lineBuffer, x, y, fontSize, color); y += lineHeight; }
    return y;
}

static int CountLines(const char *text) {
    int lines = 1;
    for (; *text; text++) if (*text == '\n') lines++;
    return lines;
}

static void DrawCodeBlock(const char *code, int x, int y, int fontSize, Color color) {
    const char *cursor = code;
    char line[160];
    for (;;) {
        int length = 0;
        while (*cursor && *cursor != '\n' && length < 159) line[length++] = *cursor++;
        line[length] = '\0';
        DrawText(line, x, y, fontSize, color);
        y += fontSize + 6;
        while (*cursor && *cursor != '\n') cursor++;
        if (*cursor == '\n') cursor++; else break;
    }
}

static void FitText(const char *source, char *dest, int capacity, int maxWidth, int fontSize) {
    snprintf(dest, capacity, "%s", source);
    for (char *c = dest; *c; c++) if (*c == '\n') *c = ' ';
    if (MeasureText(dest, fontSize) <= maxWidth) return;
    int length = (int)strlen(dest);
    while (length > 3) {
        length--;
        dest[length] = '\0';
        char trial[200];
        snprintf(trial, sizeof trial, "%s...", dest);
        if (MeasureText(trial, fontSize) <= maxWidth) { snprintf(dest, capacity, "%s", trial); return; }
    }
}

// ================================================================ shape helpers
// raylib only draws triangles whose corners run counter-clockwise, so fix the order for the caller.
static void DrawTriSafe(Vector2 a, Vector2 b, Vector2 c, Color col) {
    float cross = (b.x - a.x) * (c.y - a.y) - (b.y - a.y) * (c.x - a.x);
    if (cross > 0) DrawTriangle(a, c, b, col); else DrawTriangle(a, b, c, col);
}

static void DrawRound(Rectangle r, float radius, Color c) {
    float m = r.width < r.height ? r.width : r.height;
    float roundness = Clampf(radius * 2 / m, 0, 1);
    DrawRectangleRounded(r, roundness, 10, c);
}
static void DrawRoundOutlined(Rectangle r, float radius, float border, Color fill, Color outline) {
    DrawRound(Expand(r, border), radius + border, outline);
    DrawRound(r, radius, fill);
}

static void DrawStarShape(Vector2 c, float r, float rot, Color col) {
    Vector2 pts[10];
    for (int i = 0; i < 10; i++) {
        float angle = rot + i * 36.0f * DEG2RAD - 90.0f * DEG2RAD;
        float radius = (i % 2 == 0) ? r : r * 0.46f;
        pts[i] = V2(c.x + cosf(angle) * radius, c.y + sinf(angle) * radius);
    }
    for (int i = 0; i < 10; i++) DrawTriSafe(c, pts[i], pts[(i + 1) % 10], col);
}

static void DrawSparkle(Vector2 c, float r, Color col) {
    float w = r * 0.22f;
    DrawTriSafe(V2(c.x - r, c.y), V2(c.x, c.y - w), V2(c.x, c.y + w), col);
    DrawTriSafe(V2(c.x + r, c.y), V2(c.x, c.y - w), V2(c.x, c.y + w), col);
    DrawTriSafe(V2(c.x, c.y - r), V2(c.x - w, c.y), V2(c.x + w, c.y), col);
    DrawTriSafe(V2(c.x, c.y + r), V2(c.x - w, c.y), V2(c.x + w, c.y), col);
}

// One big glossy star. pop 0..1 scales it in with a bounce.
static void DrawBigStar(Vector2 c, float r, bool earned, float pop) {
    if (pop <= 0) return;
    r *= EaseOutBack(pop);
    if (!earned) {
        DrawStarShape(c, r * 1.22f, 0, COLOR_INK);
        DrawStarShape(c, r, 0, (Color){120, 84, 170, 255});
        return;
    }
    DrawStarShape(c, r * 1.22f, 0, COLOR_INK);
    DrawStarShape(c, r, 0, COLOR_YELLOW);
    DrawStarShape(c, r * 0.62f, 0, (Color){255, 238, 150, 255});
    DrawCircleV(V2(c.x - r * 0.22f, c.y - r * 0.3f), r * 0.12f, Fade(WHITE, 0.9f));
}

static void DrawStarRow(float cx, float cy, float r, int earned, float revealTime) {
    for (int i = 0; i < 3; i++) {
        float spread = r * 2.25f;
        float x = cx + (i - 1) * spread;
        float y = cy + (i == 1 ? -r * 0.35f : r * 0.1f);
        DrawBigStar(V2(x, y), r, i < earned, Clampf((revealTime - i * 0.35f) / 0.35f, 0, 1));
    }
}

static void DrawCloud(Vector2 c, float s, Color col) {
    DrawCircleV(V2(c.x - 26 * s, c.y + 4 * s), 18 * s, col);
    DrawCircleV(V2(c.x, c.y - 6 * s), 26 * s, col);
    DrawCircleV(V2(c.x + 28 * s, c.y + 4 * s), 20 * s, col);
    DrawRound((Rectangle){ c.x - 44 * s, c.y + 4 * s, 90 * s, 20 * s }, 10 * s, col);
}

static void DrawLollipop(Vector2 c, float r, Color a, Color b, float tilt) {
    Vector2 end = V2(c.x + sinf(tilt) * r * 3.2f, c.y + cosf(tilt) * r * 3.2f);
    DrawLineEx(c, end, r * 0.34f + 6, COLOR_INK);
    DrawLineEx(c, end, r * 0.34f, COLOR_CREAM);
    DrawCircleV(c, r + 3, COLOR_INK);
    DrawCircleV(c, r, a);
    DrawRing(c, r * 0.12f, r * 0.4f, 0, 280, 24, b);
    DrawRing(c, r * 0.62f, r * 0.88f, 100, 380, 32, b);
    DrawCircleV(V2(c.x - r * 0.4f, c.y - r * 0.45f), r * 0.16f, Fade(WHITE, 0.75f));
}

static void DrawWrappedCandy(Vector2 c, float r, Color col, float rot) {
    Vector2 dir = V2(cosf(rot), sinf(rot)), perp = V2(-dir.y, dir.x);
    Color wrap = ColorBrightness(col, 0.4f);
    for (int side = -1; side <= 1; side += 2) {
        Vector2 p0 = V2(c.x + dir.x * side * r * 0.7f, c.y + dir.y * side * r * 0.7f);
        Vector2 p1 = V2(c.x + dir.x * side * r * 1.9f + perp.x * r * 0.95f, c.y + dir.y * side * r * 1.9f + perp.y * r * 0.95f);
        Vector2 p2 = V2(c.x + dir.x * side * r * 1.9f - perp.x * r * 0.95f, c.y + dir.y * side * r * 1.9f - perp.y * r * 0.95f);
        DrawTriSafe(V2(p0.x + (p0.x - c.x) * 0.0f, p0.y), V2(p1.x + (p1.x - c.x) * 0.12f, p1.y + (p1.y - c.y) * 0.12f), V2(p2.x + (p2.x - c.x) * 0.12f, p2.y + (p2.y - c.y) * 0.12f), COLOR_INK);
        DrawTriSafe(p0, p1, p2, wrap);
    }
    DrawCircleV(c, r + 3, COLOR_INK);
    DrawCircleV(c, r, col);
    DrawRing(c, r * 0.5f, r * 0.72f, 200, 330, 16, ColorBrightness(col, 0.45f));
    DrawCircleV(V2(c.x - r * 0.35f, c.y - r * 0.4f), r * 0.17f, Fade(WHITE, 0.8f));
}

static void DrawBean(Vector2 c, float length, float thick, Color col, float rot) {
    Vector2 dir = V2(cosf(rot), sinf(rot)), perp = V2(-dir.y, dir.x);
    Vector2 a = V2(c.x - dir.x * length / 2, c.y - dir.y * length / 2), b = V2(c.x + dir.x * length / 2, c.y + dir.y * length / 2);
    DrawLineEx(a, b, thick + 6, COLOR_INK);
    DrawCircleV(a, thick / 2 + 3, COLOR_INK);
    DrawCircleV(b, thick / 2 + 3, COLOR_INK);
    DrawLineEx(a, b, thick, col);
    DrawCircleV(a, thick / 2, col);
    DrawCircleV(b, thick / 2, col);
    Vector2 h0 = V2(a.x + perp.x * thick * -0.2f + dir.x * thick * 0.2f, a.y + perp.y * thick * -0.2f + dir.y * thick * 0.2f);
    Vector2 h1 = V2(c.x + perp.x * thick * -0.2f, c.y + perp.y * thick * -0.2f);
    DrawLineEx(h0, h1, thick * 0.2f, Fade(WHITE, 0.6f));
}

static void DrawCupcake(Vector2 base, float s) {
    Vector2 tl = V2(base.x - 22 * s, base.y - 24 * s), tr = V2(base.x + 22 * s, base.y - 24 * s);
    Vector2 bl = V2(base.x - 15 * s, base.y), br = V2(base.x + 15 * s, base.y);
    DrawCircleV(V2(base.x, base.y - 50 * s), 24 * s + 3, COLOR_INK);
    DrawCircleV(V2(base.x, base.y - 36 * s), 25 * s + 3, COLOR_INK);
    DrawTriSafe(V2(tl.x - 3, tl.y - 2), V2(tr.x + 3, tr.y - 2), V2(br.x + 2, br.y + 3), COLOR_INK);
    DrawTriSafe(V2(tl.x - 3, tl.y - 2), V2(br.x + 2, br.y + 3), V2(bl.x - 2, bl.y + 3), COLOR_INK);
    DrawTriSafe(tl, tr, br, COLOR_CYAN);
    DrawTriSafe(tl, br, bl, COLOR_CYAN);
    for (int i = -1; i <= 1; i++) DrawLineEx(V2(base.x + i * 11 * s, base.y - 22 * s), V2(base.x + i * 8 * s, base.y - 2 * s), 3 * s, ColorBrightness(COLOR_CYAN, -0.25f));
    DrawCircleV(V2(base.x, base.y - 34 * s), 25 * s, COLOR_CREAM);
    DrawCircleV(V2(base.x, base.y - 48 * s), 21 * s, COLOR_PINK);
    DrawCircleV(V2(base.x - 7 * s, base.y - 54 * s), 4 * s, Fade(WHITE, 0.7f));
    DrawCircleV(V2(base.x + 2 * s, base.y - 72 * s), 7 * s + 2, COLOR_INK);
    DrawCircleV(V2(base.x + 2 * s, base.y - 72 * s), 7 * s, COLOR_RED);
}

static void DrawCandyShape(int type, Vector2 c, float size, Color col, float rot) {
    switch (type % 5) {
        case 0: DrawLollipop(c, size, col, COLOR_CREAM, sinf(rot) * 0.3f); break;
        case 1: DrawWrappedCandy(c, size * 0.8f, col, rot); break;
        case 2: DrawBean(c, size * 1.8f, size * 0.9f, col, rot); break;
        case 3: DrawStarShape(c, size * 1.15f, rot, COLOR_YELLOW); break;
        default: DrawSparkle(c, size * 1.3f, COLOR_CREAM); break;
    }
}

// ================================================================ background and header
static void DrawBackground(float wash) {
    float t = (float)GetTime();
    DrawRectangleGradientV(0, 0, WINDOW_WIDTH, WINDOW_HEIGHT, COLOR_BG_TOP, COLOR_BG_BOT);
    for (int i = 0; i < SPARKLE_COUNT; i++) {
        float twinkle = 0.5f + 0.5f * sinf(t * 2.2f + i * 1.7f);
        DrawSparkle(sparklePos[i], 3 + 5 * twinkle, Fade(WHITE, 0.25f + 0.5f * twinkle));
    }
    for (int i = 0; i < BG_CANDY_COUNT; i++) {
        float speed = 10 + (i * 7) % 15;
        float x = 40 + (i * 977 % 920) + sinf(t * 0.7f + i) * 16;
        float y = WINDOW_HEIGHT + 40 - fmodf(t * speed + i * 83, WINDOW_HEIGHT + 80);
        DrawCandyShape(i, V2(x, y), 9 + (i * 5) % 9, CandyColor(i), t * 0.8f + i);
    }
    if (wash > 0) DrawRectangle(0, 0, WINDOW_WIDTH, WINDOW_HEIGHT, Fade((Color){150, 90, 220, 255}, wash));
}

static void DrawStatChip(Rectangle r, bool star, int value) {
    DrawRoundOutlined(r, r.height / 2, 3, (Color){255, 255, 255, 235}, COLOR_INK);
    if (star) DrawStarShape(V2(r.x + 20, r.y + r.height / 2), 11, 0, COLOR_YELLOW);
    else DrawCircleV(V2(r.x + 20, r.y + r.height / 2), 10, COLOR_CYAN);
    char text[24];
    snprintf(text, sizeof text, star ? "%d" : "%d XP", value);
    DrawText(text, (int)r.x + 38, (int)(r.y + r.height / 2) - 10, 20, COLOR_INK);
}

static int TotalStars(void) {
    int total = 0;
    for (int m = 0; m < MODE_COUNT; m++) for (int l = 0; l < LEVEL_COUNT; l++) total += app.progress.bestGems[m][l];
    return total;
}

// Purple bar with cream icing dripping off the bottom edge.
static void DrawHeaderBar(const char *title, int titleX, bool chips) {
    DrawRectangleGradientV(0, 0, WINDOW_WIDTH, 58, (Color){138, 78, 236, 255}, (Color){104, 54, 200, 255});
    DrawRectangle(0, 0, WINDOW_WIDTH, 3, Fade(WHITE, 0.4f));
    DrawRectangle(0, 56, WINDOW_WIDTH, 3, Fade(COLOR_INK, 0.45f));
    DrawRectangle(0, 58, WINDOW_WIDTH, 9, COLOR_CREAM);
    for (int i = 0; i < 27; i++) {
        float x = i * 38.0f + 6;
        float length = 10 + (i * 37 % 19);
        DrawRound((Rectangle){ x, 58, 24, length + 9 }, 12, COLOR_CREAM);
        DrawCircleV(V2(x + 6, 62), 2, Fade(COLOR_PINK, 0.35f));
    }
    if (title) DrawTextO(title, titleX, 14, 30, WHITE, COLOR_INK);
    if (chips) {
        DrawStatChip((Rectangle){ 700, 11, 120, 36 }, true, TotalStars());
        DrawStatChip((Rectangle){ 835, 11, 140, 36 }, false, app.progress.totalXp);
    }
}

// ================================================================ the robot friend, Byte
static void DrawBot(float cx, float footY, float s, BotMood mood) {
    float t = (float)GetTime();
    float bob = sinf(t * 3.0f) * 2.5f * s;
    if (mood == MOOD_CHEER) bob = -fabsf(sinf(t * 7.0f)) * 12.0f * s;
    if (mood == MOOD_SAD) bob = sinf(t * 1.6f) * 1.0f * s;
    Color white = { 248, 250, 255, 255 }, shade = { 202, 212, 240, 255 };
    Color headCol = { 170, 238, 255, 255 }, glow = { 126, 255, 250, 255 };
    float y0 = footY + bob;

    DrawEllipse((int)cx, (int)footY, 38 * s * (1.0f + bob / (60 * s)), 7 * s, Fade(COLOR_INK, 0.28f));

    // feet
    DrawRoundOutlined((Rectangle){ cx - 27 * s, y0 - 12 * s, 23 * s, 12 * s }, 5 * s, 2.5f * s, COLOR_PINK, COLOR_INK);
    DrawRoundOutlined((Rectangle){ cx + 4 * s, y0 - 12 * s, 23 * s, 12 * s }, 5 * s, 2.5f * s, COLOR_PINK, COLOR_INK);

    // arms (angles in degrees, 90 = straight down)
    float shoulderY = y0 - 40 * s;
    float angL = 115, angR = 65;
    if (mood == MOOD_CHEER) { angL = 235 + sinf(t * 9) * 14; angR = 305 - sinf(t * 9) * 14; }
    else if (mood == MOOD_HAPPY) angR = 300 + sinf(t * 8) * 26;
    else if (mood == MOOD_SAD) { angL = 100; angR = 80; }
    else if (mood == MOOD_THINK) angR = 330;
    for (int side = 0; side < 2; side++) {
        float ang = (side == 0 ? angL : angR) * DEG2RAD;
        Vector2 shoulder = V2(cx + (side == 0 ? -26 : 26) * s, shoulderY);
        Vector2 hand = V2(shoulder.x + cosf(ang) * 25 * s, shoulder.y + sinf(ang) * 25 * s);
        DrawLineEx(shoulder, hand, 13 * s, COLOR_INK);
        DrawCircleV(hand, 8.5f * s, COLOR_INK);
        DrawLineEx(shoulder, hand, 8 * s, shade);
        DrawCircleV(hand, 6 * s, white);
    }

    // body
    Rectangle body = { cx - 27 * s, y0 - 52 * s, 54 * s, 44 * s };
    DrawRoundOutlined(body, 14 * s, 3 * s, white, COLOR_INK);
    DrawRound((Rectangle){ body.x + 5 * s, body.y + body.height - 12 * s, body.width - 10 * s, 8 * s }, 4 * s, shade);
    Rectangle panel = { cx - 15 * s, body.y + 9 * s, 30 * s, 22 * s };
    DrawRoundOutlined(panel, 7 * s, 2 * s, COLOR_CODE_BG, COLOR_INK);
    float beat = 1.0f + 0.16f * sinf(t * 6.0f);
    float hy = panel.y + panel.height / 2 - 1 * s;
    Color heartCol = mood == MOOD_SAD ? (Color){110, 150, 255, 255} : COLOR_PINK;
    DrawCircleV(V2(cx - 4.2f * s * beat, hy - 2 * s), 4.6f * s * beat, heartCol);
    DrawCircleV(V2(cx + 4.2f * s * beat, hy - 2 * s), 4.6f * s * beat, heartCol);
    DrawTriSafe(V2(cx - 8.6f * s * beat, hy - 0.2f * s), V2(cx + 8.6f * s * beat, hy - 0.2f * s), V2(cx, hy + 8.5f * s * beat), heartCol);

    // head, ear pods and antenna
    float headTop = body.y - 50 * s;
    Rectangle head = { cx - 42 * s, headTop, 84 * s, 58 * s };
    Vector2 antTop = V2(cx + sinf(t * 2.0f) * 3 * s, headTop - 17 * s);
    if (mood == MOOD_SAD) antTop = V2(cx + 6 * s, headTop - 12 * s);
    DrawLineEx(V2(cx, headTop + 4 * s), antTop, 6 * s, COLOR_INK);
    DrawLineEx(V2(cx, headTop + 4 * s), antTop, 2.6f * s, shade);
    float pulse = 0.5f + 0.5f * sinf(t * 4.0f);
    Color bulb = mood == MOOD_SAD ? (Color){150, 170, 230, 255} : COLOR_YELLOW;
    DrawCircleV(antTop, 12 * s, Fade(bulb, 0.20f + 0.25f * pulse));
    DrawCircleV(antTop, 8.5f * s, COLOR_INK);
    DrawCircleV(antTop, 6.5f * s, bulb);
    DrawCircleV(V2(antTop.x - 2 * s, antTop.y - 2 * s), 2 * s, Fade(WHITE, 0.9f));
    DrawCircleV(V2(cx - 44 * s, headTop + 30 * s), 11 * s, COLOR_INK);
    DrawCircleV(V2(cx + 44 * s, headTop + 30 * s), 11 * s, COLOR_INK);
    DrawCircleV(V2(cx - 44 * s, headTop + 30 * s), 8.5f * s, COLOR_PINK);
    DrawCircleV(V2(cx + 44 * s, headTop + 30 * s), 8.5f * s, COLOR_PINK);
    DrawRoundOutlined(head, 21 * s, 3 * s, headCol, COLOR_INK);
    DrawRound((Rectangle){ head.x + 9 * s, head.y + 4 * s, 28 * s, 5 * s }, 2.5f * s, Fade(WHITE, 0.7f));

    // face screen
    Rectangle screen = { cx - 36 * s, headTop + 8 * s, 72 * s, 43 * s };
    DrawRoundOutlined(screen, 14 * s, 2 * s, COLOR_CODE_BG, COLOR_INK);
    DrawRound((Rectangle){ screen.x + 10 * s, screen.y + 4 * s, screen.width - 20 * s, 3 * s }, 1.5f * s, Fade(WHITE, 0.12f));
    float eyeY = screen.y + 18 * s;
    float eyeX[2] = { cx - 18 * s, cx + 18 * s };
    float lookX = sinf(t * 0.8f) * 1.6f * s, lookY = 0;
    if (mood == MOOD_THINK) { lookX = -3 * s; lookY = -2.5f * s; }
    if (mood == MOOD_SAD) lookY = 2.5f * s;
    float blink = fmodf(t, 3.7f) > 3.58f ? 0.12f : 1.0f;
    for (int i = 0; i < 2; i++) {
        if (mood == MOOD_CHEER) {
            DrawRing(V2(eyeX[i], eyeY + 5 * s), 5.5f * s, 9.5f * s, 200, 340, 18, glow);
        } else {
            float ry = (mood == MOOD_SAD ? 9.0f : 10.5f) * s * blink;
            DrawEllipse((int)(eyeX[i] + lookX), (int)(eyeY + lookY), 8.8f * s, ry < 1 ? 1 : ry, glow);
            if (blink > 0.5f) {
                DrawCircleV(V2(eyeX[i] + lookX - 3 * s, eyeY + lookY - 3.5f * s), 3.4f * s, WHITE);
                DrawCircleV(V2(eyeX[i] + lookX + 3 * s, eyeY + lookY + 3.5f * s), 1.6f * s, Fade(WHITE, 0.85f));
            }
        }
    }
    if (mood == MOOD_SAD) {
        DrawLineEx(V2(eyeX[0] - 9 * s, eyeY - 10 * s), V2(eyeX[0] + 8 * s, eyeY - 16 * s), 2.6f * s, glow);
        DrawLineEx(V2(eyeX[1] - 8 * s, eyeY - 16 * s), V2(eyeX[1] + 9 * s, eyeY - 10 * s), 2.6f * s, glow);
        float drop = fmodf(t * 14.0f, 20.0f);
        DrawCircleV(V2(eyeX[1] + 4 * s, eyeY + (10 + drop) * s), 2.8f * s, (Color){120, 200, 255, 255});
    }
    DrawEllipse((int)(cx - 28 * s), (int)(eyeY + 14 * s), 5.5f * s, 3.2f * s, Fade(COLOR_PINK, 0.85f));
    DrawEllipse((int)(cx + 28 * s), (int)(eyeY + 14 * s), 5.5f * s, 3.2f * s, Fade(COLOR_PINK, 0.85f));
    Vector2 mouth = V2(cx, eyeY + 17 * s);
    if (mood == MOOD_CHEER) DrawCircleSector(V2(mouth.x, mouth.y - 4 * s), 8 * s, 0, 180, 18, COLOR_PINK);
    else if (mood == MOOD_SAD) DrawRing(V2(mouth.x, mouth.y + 5 * s), 4.0f * s, 6.2f * s, 205, 335, 14, glow);
    else if (mood == MOOD_THINK) DrawRing(mouth, 1.8f * s, 4.0f * s, 0, 360, 14, glow);
    else DrawRing(V2(mouth.x, mouth.y - 5 * s), 4.0f * s, 6.4f * s, 25, 155, 14, glow);

    if (mood == MOOD_CHEER)
        for (int i = 0; i < 3; i++) {
            float a = t * 2.5f + i * 2.1f;
            DrawSparkle(V2(cx + cosf(a) * 62 * s, headTop + 10 * s + sinf(a) * 26 * s), (5 + 3 * sinf(t * 8 + i)) * s + 2, COLOR_YELLOW);
        }
}

static void DrawBubble(Rectangle r, const char *text, int fontSize, float tailX) {
    float bottom = r.y + r.height;
    DrawTriSafe(V2(tailX - 14, bottom - 2), V2(tailX + 14, bottom - 2), V2(tailX + 2, bottom + 22), COLOR_INK);
    DrawRoundOutlined(r, 14, 3, COLOR_CREAM, COLOR_INK);
    DrawTriSafe(V2(tailX - 10, bottom - 2), V2(tailX + 10, bottom - 2), V2(tailX + 2, bottom + 14), COLOR_CREAM);
    WrapText(text, (int)r.x + 12, (int)r.y + 10, (int)r.width - 24, fontSize, COLOR_INK, true);
}

// ================================================================ particles and floating text
static void SpawnParticle(Vector2 pos, Vector2 vel, float life, float size, Color color, int shape) {
    for (int i = 0; i < MAX_PARTICLES; i++)
        if (particles[i].life <= 0) { particles[i] = (Particle){ pos, vel, life, life, size, color, shape }; return; }
}
static void SpawnBurst(Vector2 pos, int count) {
    for (int i = 0; i < count; i++) {
        float angle = RandF(0, 2 * PI), speed = RandF(120, 380);
        SpawnParticle(pos, V2(cosf(angle) * speed, sinf(angle) * speed - 80), RandF(0.7f, 1.3f), RandF(4, 9), CandyColor(GetRandomValue(0, 5)), GetRandomValue(0, 1));
    }
}
static void SpawnConfetti(int count) {
    for (int i = 0; i < count; i++)
        SpawnParticle(V2(RandF(0, WINDOW_WIDTH), -12), V2(RandF(-50, 50), RandF(90, 220)), RandF(3.0f, 5.0f), RandF(7, 12), CandyColor(GetRandomValue(0, 5)), 2);
}
static void AddFloatText(const char *text, Vector2 pos, Color color) {
    for (int i = 0; i < MAX_FLOATS; i++)
        if (floatTexts[i].life <= 0) { snprintf(floatTexts[i].text, sizeof floatTexts[i].text, "%s", text); floatTexts[i].pos = pos; floatTexts[i].life = 1.3f; floatTexts[i].color = color; return; }
}
static void UpdateEffects(float dt) {
    for (int i = 0; i < MAX_PARTICLES; i++) {
        Particle *p = &particles[i];
        if (p->life <= 0) continue;
        p->life -= dt;
        p->pos.x += p->vel.x * dt;
        p->pos.y += p->vel.y * dt;
        p->vel.y += (p->shape == 2 ? 60.0f : 520.0f) * dt;
        if (p->shape == 2) p->vel.x += sinf(p->life * 6 + p->size) * 40 * dt;
    }
    for (int i = 0; i < MAX_FLOATS; i++) if (floatTexts[i].life > 0) { floatTexts[i].life -= dt; floatTexts[i].pos.y -= 46 * dt; }
    if (app.toastTime > 0) app.toastTime -= dt;
}
static void DrawEffects(void) {
    for (int i = 0; i < MAX_PARTICLES; i++) {
        Particle *p = &particles[i];
        if (p->life <= 0) continue;
        Color c = Fade(p->color, Clampf(p->life / 0.5f, 0, 1));
        float spin = (p->maxLife - p->life) * (p->size > 8 ? 5.0f : -4.0f);
        if (p->shape == 0) DrawCircleV(p->pos, p->size, c);
        else if (p->shape == 1) DrawStarShape(p->pos, p->size * 1.5f, spin, c);
        else DrawRectanglePro((Rectangle){ p->pos.x, p->pos.y, p->size, p->size * 0.55f }, V2(p->size / 2, p->size * 0.27f), spin * 57.0f, c);
    }
    for (int i = 0; i < MAX_FLOATS; i++)
        if (floatTexts[i].life > 0) DrawTextCO(floatTexts[i].text, (int)floatTexts[i].pos.x, (int)floatTexts[i].pos.y, 30, Fade(floatTexts[i].color, Clampf(floatTexts[i].life / 0.4f, 0, 1)), Fade(COLOR_INK, Clampf(floatTexts[i].life / 0.4f, 0, 1)));
    if (app.toastTime > 0) {
        int w = MeasureText(app.toast, 22) + 50;
        Rectangle r = { (WINDOW_WIDTH - w) / 2.0f, 520, (float)w, 46 };
        float a = Clampf(app.toastTime / 0.4f, 0, 1);
        DrawRoundOutlined(r, 23, 3, Fade(COLOR_CREAM, a), Fade(COLOR_INK, a));
        DrawText(app.toast, (int)r.x + 25, (int)r.y + 12, 22, Fade(COLOR_INK, a));
    }
}

// ================================================================ candy button
// Glossy button with a chunky 3D base. Returns true when clicked.
static bool DrawCandyButton(Rectangle bounds, const char *label, Color fill, bool enabled, int fontSize) {
    bool hovered = enabled && HoverRect(bounds);
    bool down = hovered && IsMouseButtonDown(MOUSE_BUTTON_LEFT);
    float depth = 7;
    float faceShift = down ? depth - 2 : (hovered ? -2 : 0);
    Color face = enabled ? fill : COLOR_GRAY;
    if (hovered) face = ColorBrightness(face, 0.12f);
    Rectangle slab = { bounds.x, bounds.y + depth, bounds.width, bounds.height - depth };
    Rectangle top = { bounds.x, bounds.y + faceShift, bounds.width, bounds.height - depth };
    float radius = top.height / 2;
    DrawRound((Rectangle){ slab.x - 1, slab.y + 5, slab.width + 2, slab.height }, radius, Fade(BLACK, 0.22f));
    DrawRound(Expand(slab, 3), radius + 3, COLOR_INK);
    DrawRound(slab, radius, ColorBrightness(face, -0.38f));
    DrawRound(Expand(top, 3), radius + 3, COLOR_INK);
    DrawRound(top, radius, face);
    DrawRound((Rectangle){ top.x + top.height * 0.45f, top.y + 4, top.width - top.height * 0.9f, top.height * 0.34f }, top.height * 0.17f, Fade(WHITE, enabled ? 0.38f : 0.2f));
    if (label[0]) {
        int tw = MeasureText(label, fontSize);
        DrawTextO(label, (int)(top.x + top.width / 2) - tw / 2, (int)(top.y + top.height / 2) - fontSize / 2 + 1, fontSize, WHITE, ColorBrightness(face, -0.62f));
    }
    return enabled && TakeClick(hovered);
}

static void DrawPadlock(Vector2 c, float s, Color body) {
    DrawRing(V2(c.x, c.y - 6 * s), 6 * s, 9.5f * s, 180, 360, 14, COLOR_INK);
    DrawRoundOutlined((Rectangle){ c.x - 11 * s, c.y - 6 * s, 22 * s, 18 * s }, 4 * s, 2.5f * s, body, COLOR_INK);
    DrawCircleV(V2(c.x, c.y + 2 * s), 2.6f * s, COLOR_INK);
}

// ================================================================ progress and session logic
static void LoadProgress(void) {
    if (!FileExists(PROGRESS_FILE)) return;
    int size = 0;
    unsigned char *data = LoadFileData(PROGRESS_FILE, &size);
    // Older save files are shorter (no per-question stats yet) and still load fine.
    if (data && size > 0 && size <= (int)sizeof(Progress)) memcpy(&app.progress, data, (size_t)size);
    if (data) UnloadFileData(data);
}

static void SaveProgress(void) { SaveFileData(PROGRESS_FILE, &app.progress, sizeof(Progress)); }

static bool IsLevelAvailable(QuizMode mode, int level) { return !(mode == MODE_MCQ && level == LEVEL_COUNT - 1); }

static bool IsLevelUnlocked(QuizMode mode, int level) {
    return IsLevelAvailable(mode, level) && (level == 0 || app.progress.bestGems[mode][level - 1] >= 1);
}

static int GemsForScore(int correct, int total) {
    int percent = total ? correct * 100 / total : 0;
    return percent == 100 ? 3 : (percent >= 66 ? 2 : (percent >= 50 ? 1 : 0));
}

static const Question *CurrentQuestion(void) { return QuestionAt(app.questionIds[app.current]); }

static bool IsResolved(QStatus status) { return status != ST_UNSEEN && status != ST_SKIPPED; }

static int WeakScore(int bankIndex) {
    QStat s = app.progress.stats[bankIndex];
    return (int)s.wrong * 2 + (int)s.skipped - (int)s.correct;
}

// Fills ids with the weakest questions (worst first). Returns how many.
static int CollectWeak(int *ids, int maxCount) {
    int count = 0;
    for (int bankIndex = 0; bankIndex < QUESTION_BANK_SIZE; bankIndex++) {
        int score = WeakScore(bankIndex);
        if (score <= 0) continue;
        int pos = count < maxCount ? count : maxCount;
        while (pos > 0 && WeakScore(ids[pos - 1]) < score) { if (pos < maxCount) ids[pos] = ids[pos - 1]; pos--; }
        if (pos < maxCount) { ids[pos] = bankIndex; if (count < maxCount) count++; }
    }
    return count;
}

static void LevelReport(int level, int *attempts, int *correct, int *weak) {
    *attempts = *correct = *weak = 0;
    for (int bankIndex = 0; bankIndex < QUESTION_BANK_SIZE; bankIndex++) {
        if (QuestionAt(bankIndex)->levelIndex != level) continue;
        *attempts += app.progress.stats[bankIndex].attempts;
        *correct += app.progress.stats[bankIndex].correct;
        if (WeakScore(bankIndex) > 0) (*weak)++;
    }
}

static void LoadQuestionInputs(void) {
    const Question *question = CurrentQuestion();
    app.typedAnswer[0] = '\0';
    app.typedLength = 0;
    snprintf(app.codeAnswer, sizeof app.codeAnswer, "%s", question->starterCode);
    app.codeLength = (int)strlen(app.codeAnswer);
    const char *lastBrace = strrchr(app.codeAnswer, '}');
    app.codeCursor = lastBrace && lastBrace > app.codeAnswer ? (int)(lastBrace - app.codeAnswer) - 1 : app.codeLength;   // inside the function body
    for (int i = 0; i < 4; i++) app.eliminated[i] = false;
    app.lastChoice = -1;
    app.sheetOpen = false;
    app.revealed = false;
    app.runCountdown = 0;
}

static void BeginSession(void) {
    for (int i = 0; i < app.questionCount; i++) { app.qStatus[i] = ST_UNSEEN; app.qHint[i] = false; }
    app.current = 0;
    app.streak = 0;
    app.levelXp = 0;
    app.fixedCount = 0;
    app.confirmQuit = false;
    app.selectedLevel = -1;
    LoadQuestionInputs();
    SetScreen(SCREEN_QUESTION);
}

static void StartLevel(int level) {
    app.levelIndex = level;
    app.questionCount = 0;
    for (int bankIndex = 0; bankIndex < QUESTION_BANK_SIZE && app.questionCount < MAX_QUESTIONS_PER_LEVEL; bankIndex++) {
        const Question *question = QuestionAt(bankIndex);
        bool isMcq = question->kind == KIND_MCQ;
        if (question->levelIndex == level && (app.mode == MODE_MCQ) == isMcq) app.questionIds[app.questionCount++] = bankIndex;
    }
    BeginSession();
}

static bool StartPractice(void) {
    int ids[MAX_QUESTIONS_PER_LEVEL];
    int count = CollectWeak(ids, MAX_QUESTIONS_PER_LEVEL);
    if (count == 0) { ShowToast("No weak spots yet - nice work!"); return false; }
    app.levelIndex = -1;
    app.questionCount = count;
    for (int i = 0; i < count; i++) app.questionIds[i] = ids[i];
    BeginSession();
    return true;
}

static void GoToQuestion(int index) {
    app.current = index;
    LoadQuestionInputs();
}

// Next question (after the current one, wrapping round) that is still unanswered or skipped.
static int NextUnresolved(int from) {
    for (int step = 1; step < app.questionCount; step++) {
        int index = (from + step) % app.questionCount;
        if (!IsResolved(app.qStatus[index])) return index;
    }
    return -1;
}

static void FinishSession(void) {
    app.correctCount = 0;
    for (int i = 0; i < app.questionCount; i++) if (app.qStatus[i] == ST_CORRECT) app.correctCount++;
    app.levelGems = GemsForScore(app.correctCount, app.questionCount);
    if (app.levelIndex >= 0 && app.levelGems > app.progress.bestGems[app.mode][app.levelIndex]) app.progress.bestGems[app.mode][app.levelIndex] = app.levelGems;
    app.lastRunCount = app.questionCount;
    for (int i = 0; i < app.questionCount; i++) { app.lastRunIds[i] = app.questionIds[i]; app.lastRunStatus[i] = app.qStatus[i]; }
    SaveProgress();
    SetScreen(SCREEN_RESULTS);
    if (app.levelGems > 0) SpawnConfetti(60);
}

static void SkipOrNext(void) {
    if (app.qStatus[app.current] == ST_UNSEEN) {
        app.qStatus[app.current] = ST_SKIPPED;
        int bankIndex = app.questionIds[app.current];
        app.progress.stats[bankIndex].skipped++;
    }
    int next = NextUnresolved(app.current);
    if (next < 0) FinishSession(); else GoToQuestion(next);
}

static void OpenSheet(void) { app.sheetOpen = true; app.sheetTime = 0; }

static void SubmitAnswer(const Question *question, bool correct, const char *detail, Vector2 popAt) {
    static const char *cheers[4] = { "Sweet!", "Tasty!", "Delicious!", "Divine!" };
    QStat *stat = &app.progress.stats[app.questionIds[app.current]];
    stat->attempts++;
    if (correct) stat->correct++; else stat->wrong++;
    QStatus *status = &app.qStatus[app.current];
    app.lastAnswerCorrect = correct;
    app.revealed = false;
    int gain = 0;
    if (*status == ST_UNSEEN || *status == ST_SKIPPED) {
        if (correct) {
            *status = ST_CORRECT;
            app.streak++;
            gain = question->kind == KIND_CODE ? 25 : 10;
            if (app.qHint[app.current]) gain /= 2;
            gain += (app.streak - 1 < 4 ? app.streak - 1 : 4) * 2;
        } else {
            *status = ST_WRONG;
            app.streak = 0;
        }
        snprintf(app.feedbackHead, sizeof app.feedbackHead, "%s", correct ? cheers[GetRandomValue(0, 3)] : "Not quite...");
    } else if (*status == ST_WRONG && correct) {
        *status = ST_FIXED;
        app.fixedCount++;
        snprintf(app.feedbackHead, sizeof app.feedbackHead, "Got it this time!");
    } else {
        snprintf(app.feedbackHead, sizeof app.feedbackHead, "%s", correct ? "Still got it!" : "Not quite...");
    }
    if (gain > 0) {
        app.levelXp += gain;
        app.progress.totalXp += gain;
        char xp[24];
        snprintf(xp, sizeof xp, "+%d XP", gain);
        AddFloatText(xp, V2(popAt.x, popAt.y - 20), COLOR_YELLOW);
    }
    if (correct) {
        snprintf(app.feedbackText, sizeof app.feedbackText, "%s", question->explanation);
        SpawnBurst(popAt, 34);
    } else {
        snprintf(app.feedbackText, sizeof app.feedbackText, "Have another go, or peek at the answer.\nHint: %s", question->hint);
        snprintf(app.revealText, sizeof app.revealText, "%s\n%s", detail, question->explanation);
    }
    SaveProgress();
    OpenSheet();
}

// ================================================================ typing
static void NormalizeText(const char *source, char *dest, int destSize) {
    int length = 0;
    for (; *source && length < destSize - 1; source++) {
        if (*source == ' ' || *source == '\t' || *source == '\n') continue;
        dest[length++] = (*source >= 'A' && *source <= 'Z') ? (char)(*source + 32) : *source;
    }
    dest[length] = '\0';
}

static bool MatchesAcceptedAnswer(const char *typed, const char *acceptedAnswers) {
    char typedNormalized[96], piece[96], pieceNormalized[96];
    NormalizeText(typed, typedNormalized, sizeof typedNormalized);
    const char *cursor = acceptedAnswers;
    while (*cursor) {
        int length = 0;
        while (*cursor && *cursor != '|' && length < 95) piece[length++] = *cursor++;
        piece[length] = '\0';
        if (*cursor == '|') cursor++;
        NormalizeText(piece, pieceNormalized, sizeof pieceNormalized);
        if (strcmp(typedNormalized, pieceNormalized) == 0) return true;
    }
    return false;
}


static bool KeyRepeat(int key) { return IsKeyPressed(key) || IsKeyPressedRepeat(key); }

static void HandleTextInput(char *buffer, int *length, int capacity) {
    for (int key = GetCharPressed(); key > 0; key = GetCharPressed())
        if (key >= 32 && key <= 126 && *length < capacity - 1) { buffer[(*length)++] = (char)key; buffer[*length] = '\0'; }
    if (KeyRepeat(KEY_BACKSPACE) && *length > 0) buffer[--(*length)] = '\0';
}

static void CodeInsert(char c) {
    if (app.codeLength >= (int)sizeof app.codeAnswer - 1) return;
    memmove(app.codeAnswer + app.codeCursor + 1, app.codeAnswer + app.codeCursor, (size_t)(app.codeLength - app.codeCursor + 1));
    app.codeAnswer[app.codeCursor++] = c;
    app.codeLength++;
}

static int CodeLineStart(int pos) { while (pos > 0 && app.codeAnswer[pos - 1] != '\n') pos--; return pos; }
static int CodeLineEnd(int pos) { while (app.codeAnswer[pos] && app.codeAnswer[pos] != '\n') pos++; return pos; }

// A tiny code editor: cursor, arrows, auto-indent, tab = 4 spaces.
static void HandleCodeInput(void) {
    for (int key = GetCharPressed(); key > 0; key = GetCharPressed()) if (key >= 32 && key <= 126) CodeInsert((char)key);
    if (KeyRepeat(KEY_BACKSPACE) && app.codeCursor > 0) {
        memmove(app.codeAnswer + app.codeCursor - 1, app.codeAnswer + app.codeCursor, (size_t)(app.codeLength - app.codeCursor + 1));
        app.codeCursor--; app.codeLength--;
    }
    if (KeyRepeat(KEY_DELETE) && app.codeCursor < app.codeLength) {
        memmove(app.codeAnswer + app.codeCursor, app.codeAnswer + app.codeCursor + 1, (size_t)(app.codeLength - app.codeCursor));
        app.codeLength--;
    }
    bool controlHeld = IsKeyDown(KEY_LEFT_CONTROL) || IsKeyDown(KEY_RIGHT_CONTROL);
    if (!controlHeld && KeyRepeat(KEY_ENTER)) {
        int start = CodeLineStart(app.codeCursor), indent = 0;
        while (app.codeAnswer[start + indent] == ' ' && start + indent < app.codeCursor) indent++;
        CodeInsert('\n');
        for (int i = 0; i < indent; i++) CodeInsert(' ');
    }
    if (KeyRepeat(KEY_TAB)) for (int i = 0; i < 4; i++) CodeInsert(' ');
    if (KeyRepeat(KEY_LEFT) && app.codeCursor > 0) app.codeCursor--;
    if (KeyRepeat(KEY_RIGHT) && app.codeCursor < app.codeLength) app.codeCursor++;
    if (KeyRepeat(KEY_HOME)) app.codeCursor = CodeLineStart(app.codeCursor);
    if (KeyRepeat(KEY_END)) app.codeCursor = CodeLineEnd(app.codeCursor);
    if (KeyRepeat(KEY_UP)) {
        int start = CodeLineStart(app.codeCursor), column = app.codeCursor - start;
        if (start > 0) {
            int prevStart = CodeLineStart(start - 1), prevLength = start - 1 - prevStart;
            app.codeCursor = prevStart + (column < prevLength ? column : prevLength);
        }
    }
    if (KeyRepeat(KEY_DOWN)) {
        int start = CodeLineStart(app.codeCursor), column = app.codeCursor - start, end = CodeLineEnd(app.codeCursor);
        if (app.codeAnswer[end] == '\n') {
            int nextStart = end + 1, nextLength = CodeLineEnd(nextStart) - nextStart;
            app.codeCursor = nextStart + (column < nextLength ? column : nextLength);
        }
    }
}

// Writes the learner's function plus the test harness, compiles it with gcc and runs it.
// Writes the learner's function plus the test harness, compiles it with gcc and runs it.
static bool RunCodeChallenge(const Question *question, char *detail, int detailSize) {
    static char sourceText[4096];
    snprintf(sourceText, sizeof sourceText, "#include <stdio.h>\n#include <stdbool.h>\n%s\n%s\n", app.codeAnswer, question->testHarness);
    SaveFileText("cquest_attempt.c", sourceText);
    remove(ATTEMPT_EXE_FILE);
    int compileStatus = system("gcc cquest_attempt.c -o " ATTEMPT_EXE_FILE " 2> cquest_compile.txt");
    if (compileStatus != 0 || !FileExists(ATTEMPT_EXE_FILE)) {
        char *errors = LoadFileText("cquest_compile.txt");
        snprintf(detail, detailSize, "Compile error: %.200s", errors ? errors : "gcc was not found. Is it on your PATH?");
        if (errors) UnloadFileText(errors);
        return false;
    }
    system(ATTEMPT_RUN_COMMAND " > cquest_run.txt");
    char *output = LoadFileText("cquest_run.txt");
    bool passed = output && strstr(output, "PASS") != NULL;
    if (output) UnloadFileText(output);
    snprintf(detail, detailSize, "It compiled, but a test failed. Check edge cases like 0 and negatives.");
    return passed;
}


// ================================================================ screens
static void ScreenTitle(void) {
    float t = (float)GetTime();
    DrawBackground(0);
    DrawEllipse(170, 650, 430, 175, (Color){255, 170, 214, 255});
    DrawEllipse(830, 660, 470, 160, (Color){255, 146, 200, 255});
    DrawEllipse(500, 700, 720, 160, (Color){255, 228, 242, 255});
    DrawCloud(V2(170 + sinf(t * 0.3f) * 20, 150), 1.0f, Fade(WHITE, 0.55f));
    DrawCloud(V2(840 + sinf(t * 0.25f) * 24, 190), 1.2f, Fade(WHITE, 0.55f));
    DrawLollipop(V2(95, 455), 32, COLOR_PINK, COLOR_CREAM, 0.2f);
    DrawLollipop(V2(905, 440), 36, COLOR_CYAN, COLOR_CREAM, -0.25f);
    DrawCupcake(V2(215, 612), 0.95f);
    DrawCupcake(V2(790, 616), 0.95f);
    DrawWrappedCandy(V2(120, 330 + sinf(t * 2) * 8), 18, COLOR_YELLOW, t);
    DrawWrappedCandy(V2(880, 300 + sinf(t * 2 + 1) * 8), 20, COLOR_GREEN, -t);

    const char *logo = "C QUEST";
    int size = 112, spacing = 6, total = 0;
    for (int i = 0; logo[i]; i++) { char one[2] = { logo[i], 0 }; total += MeasureText(one, size) + spacing; }
    int x = WINDOW_WIDTH / 2 - (total - spacing) / 2;
    for (int i = 0; logo[i]; i++) {
        char one[2] = { logo[i], 0 };
        int y = 56 + (int)(sinf(t * 3.2f + i * 0.8f) * 9);
        if (logo[i] != ' ') {
            DrawText(one, x, y + 10, size, Fade(COLOR_INK, 0.55f));
            DrawTextO(one, x, y, size, CandyColor(i), COLOR_INK);
        }
        x += MeasureText(one, size) + spacing;
    }
    DrawSparkle(V2(250 + sinf(t * 2) * 6, 70), 14, WHITE);
    DrawSparkle(V2(760, 92 + sinf(t * 2.4f) * 6), 11, COLOR_YELLOW);
    DrawSparkle(V2(838, 60), 9, WHITE);

    Rectangle ribbon = { 290, 206, 420, 50 };
    DrawRoundOutlined(ribbon, 25, 4, COLOR_PINK, COLOR_INK);
    DrawRound((Rectangle){ ribbon.x + 20, ribbon.y + 5, ribbon.width - 40, 14 }, 7, Fade(WHITE, 0.3f));
    DrawTextCO("Learn C the sweet way!", WINDOW_WIDTH / 2, 218, 26, WHITE, COLOR_INK);

    DrawBot(500, 545, 1.7f, MOOD_HAPPY);
    float pulse = 1.0f + 0.04f * sinf(t * 5);
    Rectangle play = { 500 - 140 * pulse, 568, 280 * pulse, 62 };
    if (DrawCandyButton(play, "PLAY", COLOR_GREEN, true, 38) || IsKeyPressed(KEY_ENTER) || IsKeyPressed(KEY_SPACE)) SetScreen(SCREEN_MODE);
}

static void DrawModeCard(Rectangle r, int mode, Color accent) {
    bool hovered = HoverRect(r);
    if (hovered) r.y -= 6;
    DrawRound((Rectangle){ r.x + 4, r.y + 12, r.width, r.height }, 30, Fade(BLACK, 0.25f));
    DrawRoundOutlined(r, 30, 5, COLOR_CREAM, COLOR_INK);
    Rectangle band = { r.x, r.y, r.width, 74 };
    DrawRound(band, 30, accent);
    DrawRectangle((int)band.x, (int)band.y + 44, (int)band.width, 30, accent);
    DrawRound((Rectangle){ band.x + 24, band.y + 8, band.width - 48, 14 }, 7, Fade(WHITE, 0.35f));
    DrawTextCO(mode == MODE_MCQ ? "MCQ MODE" : "TYPED MODE", (int)(r.x + r.width / 2), (int)r.y + 22, 36, WHITE, COLOR_INK);
    // icon
    Vector2 mid = V2(r.x + r.width / 2, r.y + 130);
    if (mode == MODE_MCQ) {
        for (int i = 0; i < 4; i++) {
            Rectangle o = { mid.x - 84 + (i % 2) * 88, mid.y - 30 + (i / 2) * 36, 80, 30 };
            DrawRoundOutlined(o, 12, 3, CandyColor(i == 3 ? 4 : i), COLOR_INK);
            char letter[2] = { (char)('A' + i), 0 };
            DrawTextCO(letter, (int)(o.x + o.width / 2), (int)o.y + 5, 20, WHITE, COLOR_INK);
        }
    } else {
        Rectangle box = { mid.x - 100, mid.y - 34, 200, 70 };
        DrawRoundOutlined(box, 14, 3, COLOR_CODE_BG, COLOR_INK);
        DrawCircleV(V2(box.x + 16, box.y + 14), 4, COLOR_RED);
        DrawCircleV(V2(box.x + 30, box.y + 14), 4, COLOR_YELLOW);
        DrawCircleV(V2(box.x + 44, box.y + 14), 4, COLOR_GREEN);
        DrawText((int)(GetTime() * 2) % 2 == 0 ? "int x = 5;_" : "int x = 5;", (int)box.x + 16, (int)box.y + 36, 20, COLOR_GREEN);
    }
    WrapText(mode == MODE_MCQ ? "Pick the right answer from four choices. Press 1-4 or click. Great for warming up."
                              : "Type your answers. Then unlock the Function Lab, where your code is compiled and tested with gcc.",
             (int)r.x + 26, (int)r.y + 190, (int)r.width - 52, 20, COLOR_INK, true);
    int stars = 0, maxStars = 0;
    for (int l = 0; l < LEVEL_COUNT; l++) if (IsLevelAvailable((QuizMode)mode, l)) { stars += app.progress.bestGems[mode][l]; maxStars += 3; }
    DrawStarShape(V2(r.x + 40, r.y + r.height - 38), 14, 0, COLOR_YELLOW);
    char text[40];
    snprintf(text, sizeof text, "%d / %d", stars, maxStars);
    DrawText(text, (int)r.x + 62, (int)r.y + (int)r.height - 48, 22, COLOR_INK);
    if (DrawCandyButton((Rectangle){ r.x + r.width - 190, r.y + r.height - 70, 160, 54 }, "PLAY", accent, true, 26) || TakeClick(hovered)) {
        app.mode = (QuizMode)mode;
        SetScreen(SCREEN_MAP);
    }
}

static void ScreenMode(void) {
    DrawBackground(0.15f);
    DrawModeCard((Rectangle){ 60, 104, 410, 340 }, MODE_MCQ, COLOR_GREEN);
    DrawModeCard((Rectangle){ 530, 104, 410, 340 }, MODE_TYPED, COLOR_PINK);
    DrawBubble((Rectangle){ 340, 470, 320, 44 }, "How do you want to learn today?", 18, 500);
    DrawBot(500, 628, 0.8f, MOOD_HAPPY);
    DrawHeaderBar("CHOOSE YOUR MODE", 122, true);
    if (DrawCandyButton((Rectangle){ 12, 8, 100, 44 }, "BACK", COLOR_PINK, true, 20) || IsKeyPressed(KEY_ESCAPE)) SetScreen(SCREEN_TITLE);
}

static void DrawXMark(Rectangle b) {
    Vector2 c = V2(b.x + b.width / 2, b.y + (b.height - 7) / 2 + 1);
    DrawLineEx(V2(c.x - 8, c.y - 8), V2(c.x + 8, c.y + 8), 8, COLOR_INK);
    DrawLineEx(V2(c.x - 8, c.y + 8), V2(c.x + 8, c.y - 8), 8, COLOR_INK);
    DrawLineEx(V2(c.x - 8, c.y - 8), V2(c.x + 8, c.y + 8), 4, WHITE);
    DrawLineEx(V2(c.x - 8, c.y + 8), V2(c.x + 8, c.y - 8), 4, WHITE);
}

// ---------------------------------------------------------------- level map
static int CurrentLevelForMap(void) {
    int last = 0;
    for (int l = 0; l < LEVEL_COUNT; l++) {
        if (!IsLevelUnlocked(app.mode, l)) continue;
        last = l;
        if (app.progress.bestGems[app.mode][l] == 0) return l;
    }
    return last;
}

static void DrawRoad(void) {
    for (int pass = 0; pass < 3; pass++) {
        for (int seg = 0; seg < LEVEL_COUNT - 1; seg++) {
            bool open = IsLevelUnlocked(app.mode, seg + 1);
            Vector2 a = nodePos[seg], b = nodePos[seg + 1];
            for (int i = 0; i <= 30; i++) {
                float f = i / 30.0f;
                Vector2 p = V2(a.x + (b.x - a.x) * f, a.y + (b.y - a.y) * Smooth(f));
                if (pass == 0) DrawCircleV(p, 19, Fade(COLOR_INK, 0.35f));
                else if (pass == 1) DrawCircleV(p, 15, open ? (Color){255, 238, 176, 255} : (Color){222, 200, 244, 255});
                else if (i % 3 == 0) DrawCircleV(p, 3.2f, open ? (Color){255, 190, 70, 255} : (Color){190, 160, 224, 255});
            }
        }
    }
}

static void DrawLevelNode(Vector2 c, int level, bool locked, bool available, int gems, bool isCurrent, bool hovered) {
    float t = (float)GetTime();
    bool boss = level == LEVEL_COUNT - 1;
    float r = boss ? 50.0f : 40.0f;
    if (hovered) r *= 1.07f;
    float bounce = isCurrent ? -fabsf(sinf(t * 4.0f)) * 9.0f : 0;
    DrawEllipse((int)c.x, (int)(c.y + r + 4), r * 0.9f, 9, Fade(COLOR_INK, 0.3f));
    c.y += bounce;
    Color col = locked ? (Color){156, 134, 196, 255} : LevelColor(level);
    if (isCurrent) DrawRing(c, r + 9, r + 12 + sinf(t * 6) * 2, 0, 360, 48, Fade(WHITE, 0.75f));
    DrawCircleV(c, r + 6, COLOR_INK);
    DrawCircleV(c, r + 2, COLOR_CREAM);
    DrawCircleV(c, r - 3, ColorBrightness(col, -0.22f));
    DrawCircleV(V2(c.x, c.y - 3), r - 5, col);
    DrawEllipse((int)(c.x - r * 0.15f), (int)(c.y - r * 0.5f), r * 0.5f, r * 0.24f, Fade(WHITE, 0.45f));
    if (boss) DrawStarShape(V2(c.x, c.y - r - 14), 17, 0, COLOR_INK), DrawStarShape(V2(c.x, c.y - r - 14), 13, 0, locked ? COLOR_GRAY : COLOR_YELLOW);
    if (locked) {
        DrawPadlock(V2(c.x, c.y + 2), boss ? 1.5f : 1.25f, COLOR_GRAY);
    } else {
        char number[4];
        snprintf(number, sizeof number, "%d", level + 1);
        DrawTextCO(number, (int)c.x, (int)c.y - (boss ? 22 : 19), boss ? 46 : 40, WHITE, COLOR_INK);
    }
    if (available && !locked) DrawStarRow(c.x, c.y - bounce + r + 26, 11, gems, 99);
    if (!available) {
        Rectangle tag = { c.x - 44, c.y - bounce + r + 14, 88, 24 };
        DrawRoundOutlined(tag, 12, 2, COLOR_CREAM, COLOR_INK);
        DrawTextCentered("TYPED ONLY", (int)c.x, (int)tag.y + 5, 14, COLOR_INK);
    }
}

static void DrawLevelPopup(void) {
    int level = app.selectedLevel;
    bool unlocked = IsLevelUnlocked(app.mode, level);
    DrawRectangle(0, 0, WINDOW_WIDTH, WINDOW_HEIGHT, Fade(COLOR_INK, 0.62f));
    Rectangle p = { 290, 130, 420, 390 };
    DrawRound((Rectangle){ p.x + 4, p.y + 12, p.width, p.height }, 30, Fade(BLACK, 0.3f));
    DrawRoundOutlined(p, 30, 5, COLOR_CREAM, COLOR_INK);
    Rectangle ribbon = { p.x - 26, p.y - 30, p.width + 52, 64 };
    DrawRoundOutlined(ribbon, 18, 4, LevelColor(level), COLOR_INK);
    DrawRound((Rectangle){ ribbon.x + 20, ribbon.y + 6, ribbon.width - 40, 14 }, 7, Fade(WHITE, 0.3f));
    char title[24];
    snprintf(title, sizeof title, "LEVEL %d", level + 1);
    DrawTextCO(title, (int)(p.x + p.width / 2), (int)ribbon.y + 14, 38, WHITE, COLOR_INK);
    DrawTextCentered(levelNames[level], (int)(p.x + p.width / 2), (int)p.y + 56, 24, COLOR_INK);
    int y = WrapText(levelBlurbs[level], (int)p.x + 34, (int)p.y + 96, (int)p.width - 68, 20, (Color){100, 70, 140, 255}, true);
    DrawStarRow(p.x + p.width / 2, p.y + 190, 28, app.progress.bestGems[app.mode][level], 99);
    int count = 0;
    for (int i = 0; i < QUESTION_BANK_SIZE; i++)
        if (QuestionAt(i)->levelIndex == level && (app.mode == MODE_MCQ) == (QuestionAt(i)->kind == KIND_MCQ)) count++;
    char info[80];
    snprintf(info, sizeof info, "%s MODE  -  %d questions  -  3 stars = all right", app.mode == MODE_MCQ ? "MCQ" : "TYPED", count);
    (void)y;
    DrawTextCentered(info, (int)(p.x + p.width / 2), (int)p.y + 244, 16, (Color){120, 90, 160, 255});
    const char *label = app.progress.bestGems[app.mode][level] > 0 ? "REPLAY" : "PLAY";
    if (DrawCandyButton((Rectangle){ p.x + 90, p.y + 284, 240, 70 }, label, COLOR_GREEN, unlocked, 34) || (unlocked && IsKeyPressed(KEY_ENTER))) StartLevel(level);
    Rectangle closeButton = { p.x + p.width - 44, p.y - 40, 52, 48 };
    bool closed = DrawCandyButton(closeButton, "", COLOR_RED, true, 24);
    DrawXMark(closeButton);
    if (closed || IsKeyPressed(KEY_ESCAPE)) app.selectedLevel = -1;
    DrawBot(180, 540, 1.1f, MOOD_HAPPY);
}

static void ScreenMap(void) {
    float t = (float)GetTime();
    DrawBackground(0.1f);
    DrawCloud(V2(180 + sinf(t * 0.3f) * 18, 140), 0.9f, Fade(WHITE, 0.5f));
    DrawCloud(V2(560 + sinf(t * 0.2f) * 22, 150), 0.7f, Fade(WHITE, 0.45f));
    DrawCloud(V2(930, 410), 0.8f, Fade(WHITE, 0.4f));
    DrawEllipse(120, 660, 330, 150, (Color){255, 172, 216, 255});
    DrawEllipse(880, 670, 380, 130, (Color){255, 150, 202, 255});
    DrawLollipop(V2(60, 300), 26, COLOR_PINK, COLOR_CREAM, 0.15f);
    DrawLollipop(V2(960, 330), 24, COLOR_CYAN, COLOR_CREAM, -0.2f);
    DrawCupcake(V2(910, 612), 0.8f);
    DrawCupcake(V2(60, 620), 0.8f);
    DrawWrappedCandy(V2(430, 250 + sinf(t * 1.6f) * 6), 16, COLOR_YELLOW, t * 0.6f);
    DrawBean(V2(590, 200 + sinf(t * 1.9f) * 5), 40, 18, COLOR_GREEN, 0.6f);
    DrawBean(V2(250, 560 + sinf(t * 1.7f) * 5), 38, 16, COLOR_ORANGE, -0.5f);

    uiLocked = app.selectedLevel >= 0 || app.toastTime < -1;
    DrawRoad();
    int current = CurrentLevelForMap();
    int hoveredLevel = -1;
    for (int level = 0; level < LEVEL_COUNT; level++) {
        bool available = IsLevelAvailable(app.mode, level);
        bool unlocked = IsLevelUnlocked(app.mode, level);
        bool hovered = !uiLocked && CheckCollisionPointCircle(GetMousePosition(), nodePos[level], level == LEVEL_COUNT - 1 ? 52 : 42);
        if (hovered) hoveredLevel = level;
        DrawLevelNode(nodePos[level], level, !unlocked, available, app.progress.bestGems[app.mode][level], level == current && unlocked, hovered);
        if (TakeClick(hovered)) {
            if (!available) ShowToast("Switch to TYPED mode to enter the Function Lab!");
            else if (!unlocked) { char msg[64]; snprintf(msg, sizeof msg, "Earn a star on level %d first!", level); ShowToast(msg); }
            else app.selectedLevel = level;
        }
    }
    if (hoveredLevel >= 0) {
        int w = MeasureText(levelNames[hoveredLevel], 18) + 30;
        Rectangle tip = { nodePos[hoveredLevel].x - w / 2.0f, nodePos[hoveredLevel].y - 98, (float)w, 32 };
        if (hoveredLevel == LEVEL_COUNT - 1) tip.y -= 14;
        DrawRoundOutlined(tip, 16, 3, COLOR_CREAM, COLOR_INK);
        DrawTextCentered(levelNames[hoveredLevel], (int)(tip.x + tip.width / 2), (int)tip.y + 7, 18, COLOR_INK);
    }
    Vector2 botSpot = nodePos[current];
    DrawBot(botSpot.x - 78, botSpot.y + 36, 0.62f, MOOD_HAPPY);

    DrawHeaderBar("LEVEL MAP", 122, true);
    if (DrawCandyButton((Rectangle){ 12, 8, 100, 44 }, "BACK", COLOR_PINK, true, 20)) SetScreen(SCREEN_MODE);
    char modeLabel[32];
    snprintf(modeLabel, sizeof modeLabel, "MODE: %s", app.mode == MODE_MCQ ? "MCQ" : "TYPED");
    if (DrawCandyButton((Rectangle){ 440, 8, 220, 44 }, modeLabel, COLOR_ORANGE, true, 20)) app.mode = app.mode == MODE_MCQ ? MODE_TYPED : MODE_MCQ;

    int weakIds[MAX_QUESTIONS_PER_LEVEL];
    int weakCount = CollectWeak(weakIds, MAX_QUESTIONS_PER_LEVEL);
    char weakLabel[40];
    snprintf(weakLabel, sizeof weakLabel, weakCount ? "WEAK SPOTS (%d)" : "WEAK SPOTS", weakCount);
    if (DrawCandyButton((Rectangle){ 25, 572, 270, 54 }, weakLabel, COLOR_CYAN, true, 22)) StartPractice();
    if (DrawCandyButton((Rectangle){ 705, 572, 270, 54 }, "MY PROGRESS", COLOR_PINK, true, 24)) {
        app.reviewTab = 0;
        app.reviewCount = 0;
        SetScreen(SCREEN_REVIEW);
    }
    if (IsKeyPressed(KEY_ESCAPE) && app.selectedLevel < 0) SetScreen(SCREEN_MODE);

    if (app.selectedLevel >= 0) {
        uiLocked = false;
        DrawLevelPopup();
    }
}

// ---------------------------------------------------------------- questions
static Color StatusColor(QStatus status) {
    switch (status) {
        case ST_CORRECT: return COLOR_GREEN;
        case ST_WRONG:   return COLOR_RED;
        case ST_SKIPPED: return COLOR_YELLOW;
        case ST_FIXED:   return COLOR_CYAN;
        default:         return COLOR_CREAM;
    }
}

static void DrawQuestionPips(void) {
    float spacing = 40, startX = 545 - (app.questionCount - 1) * spacing / 2;
    for (int i = 0; i < app.questionCount; i++) {
        Vector2 c = V2(startX + i * spacing, 29);
        bool isCurrent = i == app.current;
        bool hovered = !uiLocked && CheckCollisionPointCircle(GetMousePosition(), c, 17);
        float r = (isCurrent ? 16.0f : 13.0f) + (hovered ? 2 : 0);
        if (isCurrent) DrawRing(c, r + 4, r + 7, 0, 360, 30, Fade(WHITE, 0.8f));
        DrawCircleV(c, r + 3, COLOR_INK);
        DrawCircleV(c, r, StatusColor(app.qStatus[i]));
        char number[4];
        snprintf(number, sizeof number, "%d", i + 1);
        DrawTextCentered(number, (int)c.x, (int)c.y - 8, 18, COLOR_INK);
        if (TakeClick(hovered) && i != app.current) GoToQuestion(i);
    }
}

static void DrawFeedbackSheet(void) {
    app.sheetTime += GetFrameTime();
    float h = 252, a = Smooth(app.sheetTime / 0.25f);
    float top = WINDOW_HEIGHT - h * a;
    bool correct = app.lastAnswerCorrect;
    Color base = correct ? (Color){216, 255, 226, 255} : (Color){255, 228, 234, 255};
    Color edge = correct ? (Color){40, 170, 80, 255} : (Color){220, 60, 80, 255};
    DrawRoundOutlined((Rectangle){ -12, top, WINDOW_WIDTH + 24, h + 40 }, 30, 5, base, edge);
    DrawRound((Rectangle){ 30, top + 9, 300, 8 }, 4, Fade(WHITE, 0.7f));
    DrawTextO(app.feedbackHead, 40, (int)top + 26, 44, correct ? COLOR_GREEN : COLOR_RED, COLOR_INK);
    const char *text = (!correct && app.revealed) ? app.revealText : app.feedbackText;
    WrapText(text, 40, (int)top + 84, 700, 19, COLOR_INK, true);

    bool ready = app.sheetTime > 0.18f;
    bool goNext = false, tryAgain = false;
    if (correct) {
        goNext = DrawCandyButton((Rectangle){ 790, top + 110, 185, 60 }, "CONTINUE", COLOR_GREEN, true, 24);
    } else {
        tryAgain = DrawCandyButton((Rectangle){ 790, top + 54, 185, 58 }, "TRY AGAIN", COLOR_ORANGE, true, 24);
        if (app.revealed) goNext = DrawCandyButton((Rectangle){ 790, top + 126, 185, 58 }, "CONTINUE", COLOR_GREEN, true, 24);
        else if (DrawCandyButton((Rectangle){ 790, top + 126, 185, 58 }, "SHOW ANSWER", COLOR_CYAN, true, 20)) app.revealed = true;
    }
    if (ready && IsKeyPressed(KEY_ENTER)) { if (correct || app.revealed) goNext = true; else tryAgain = true; }
    if (tryAgain && ready) {
        app.sheetOpen = false;
        app.revealed = false;
        app.lastChoice = -1;
        if (CurrentQuestion()->kind == KIND_SHORT) { app.typedAnswer[0] = '\0'; app.typedLength = 0; }
    } else if (goNext && ready) {
        int next = NextUnresolved(app.current);
        if (next < 0) FinishSession(); else GoToQuestion(next);
    }
}

static void DrawQuitConfirm(void) {
    DrawRectangle(0, 0, WINDOW_WIDTH, WINDOW_HEIGHT, Fade(COLOR_INK, 0.65f));
    Rectangle p = { 300, 190, 400, 240 };
    DrawRoundOutlined(p, 28, 5, COLOR_CREAM, COLOR_INK);
    DrawTextCentered("Leave this level?", 500, 216, 32, COLOR_INK);
    WrapText("Your answers in this run will be lost, but your lifetime stats are kept.", 330, 266, 340, 18, (Color){100, 70, 140, 255}, true);
    if (DrawCandyButton((Rectangle){ 322, 346, 170, 58 }, "STAY", COLOR_GREEN, true, 26) || IsKeyPressed(KEY_ESCAPE)) app.confirmQuit = false;
    if (DrawCandyButton((Rectangle){ 508, 346, 170, 58 }, "LEAVE", COLOR_RED, true, 26)) { app.confirmQuit = false; SetScreen(SCREEN_MAP); }
}

static void ScreenQuestion(void) {
    const Question *question = CurrentQuestion();
    bool blocked = app.sheetOpen || app.confirmQuit || app.runCountdown > 0;
    uiLocked = blocked;
    DrawBackground(0.28f);

    // ---- prompt card and snippet
    int x = 30, width = 740, y = 98;
    int textEnd = WrapText(question->prompt, x + 22, y + 14, width - 44, 22, COLOR_INK, false);
    Rectangle card = { (float)x, (float)y, (float)width, (float)(textEnd - (y + 14) + 22) };
    DrawRound((Rectangle){ card.x + 3, card.y + 8, card.width, card.height }, 22, Fade(BLACK, 0.2f));
    DrawRoundOutlined(card, 22, 4, COLOR_CREAM, COLOR_INK);
    WrapText(question->prompt, x + 22, y + 14, width - 44, 22, COLOR_INK, true);
    int contentY = (int)(card.y + card.height) + 14;
    if (question->snippet[0]) {
        Rectangle code = { (float)x, (float)contentY, (float)width, (float)(CountLines(question->snippet) * 26 + 22) };
        DrawRoundOutlined(code, 16, 4, COLOR_CODE_BG, COLOR_INK);
        DrawCodeBlock(question->snippet, x + 22, contentY + 11, 20, (Color){160, 255, 180, 255});
        contentY += (int)code.height + 16;
    }

    Vector2 popAt = GetMousePosition();
    if (question->kind == KIND_MCQ) {
        int chosen = -1;
        for (int option = 0; option < 4; option++) {
            Rectangle r = { (float)x, (float)(contentY + option * 58), (float)width, 50 };
            Color fill = option == 3 ? COLOR_ORANGE : CandyColor(option);
            bool grey = app.eliminated[option];
            if (app.sheetOpen) {
                bool showRight = app.lastAnswerCorrect || app.revealed;
                if (option == app.lastChoice && !app.lastAnswerCorrect) { fill = COLOR_RED; grey = false; }
                else if (showRight && option == question->correctOption) fill = (Color){40, 200, 90, 255};
            }
            if (DrawCandyButton(r, "", fill, !grey, 20) && !blocked) chosen = option;
            if (!blocked && IsKeyPressed(KEY_ONE + option) && !grey) chosen = option;
            float cy = r.y + (r.height - 7) / 2;
            DrawCircleV(V2(r.x + 30, cy), 17, COLOR_INK);
            DrawCircleV(V2(r.x + 30, cy), 14, COLOR_CREAM);
            char letter[2] = { (char)('A' + option), 0 };
            DrawTextCentered(letter, (int)r.x + 30, (int)cy - 9, 20, COLOR_INK);
            DrawTextO(question->options[option], (int)r.x + 62, (int)cy - 11, 22, WHITE, ColorBrightness(fill, -0.6f));
        }
        if (chosen >= 0) {
            app.lastChoice = chosen;
            if (chosen != question->correctOption) app.eliminated[chosen] = true;
            char detail[200];
            snprintf(detail, sizeof detail, "Answer: %c) %s", 'A' + question->correctOption, question->options[question->correctOption]);
            SubmitAnswer(question, chosen == question->correctOption, detail, popAt);
            return;
        }
    } else if (question->kind == KIND_SHORT) {
        DrawTextO("Type your answer, then press ENTER", x, contentY, 22, WHITE, COLOR_INK);
        if (!blocked) HandleTextInput(app.typedAnswer, &app.typedLength, sizeof app.typedAnswer);
        Rectangle box = { (float)x, (float)contentY + 38, (float)width, 64 };
        DrawRoundOutlined(box, 20, 4, COLOR_CREAM, COLOR_INK);
        char shown[110];
        snprintf(shown, sizeof shown, "%s%s", app.typedAnswer, (int)(GetTime() * 2) % 2 == 0 && !blocked ? "_" : "");
        DrawText(shown, x + 22, contentY + 38 + 17, 30, COLOR_INK);
    } else {
        if (!blocked) HandleCodeInput();
        Rectangle editor = { (float)x, (float)contentY, (float)width, (float)(564 - contentY) };
        DrawRoundOutlined(editor, 18, 4, COLOR_CODE_BG, COLOR_INK);
        DrawRound((Rectangle){ editor.x, editor.y, editor.width, 30 }, 18, (Color){70, 48, 140, 255});
        DrawRectangle((int)editor.x, (int)editor.y + 16, (int)editor.width, 14, (Color){70, 48, 140, 255});
        DrawCircleV(V2(editor.x + 20, editor.y + 15), 5, COLOR_RED);
        DrawCircleV(V2(editor.x + 38, editor.y + 15), 5, COLOR_YELLOW);
        DrawCircleV(V2(editor.x + 56, editor.y + 15), 5, COLOR_GREEN);
        DrawText("answer.c", (int)editor.x + 80, (int)editor.y + 7, 16, COLOR_GRAY);
        int lineY = (int)editor.y + 40, caretLine = 0, caretCol = 0;
        for (int i = 0; i < app.codeCursor; i++) { if (app.codeAnswer[i] == '\n') { caretLine++; caretCol = 0; } else caretCol++; }
        BeginScissorMode((int)editor.x + 4, (int)editor.y + 32, (int)editor.width - 8, (int)editor.height - 40);
        int lines = CountLines(app.codeAnswer);
        for (int i = 0; i < lines; i++) {
            char number[8];
            snprintf(number, sizeof number, "%d", i + 1);
            DrawText(number, (int)editor.x + 14, lineY + i * 26, 20, (Color){120, 100, 170, 255});
        }
        DrawCodeBlock(app.codeAnswer, (int)editor.x + 52, lineY, 20, (Color){160, 255, 180, 255});
        const char *lineStart = app.codeAnswer;
        for (int i = 0; i < caretLine; i++) { lineStart = strchr(lineStart, '\n'); lineStart = lineStart ? lineStart + 1 : app.codeAnswer + app.codeLength; }
        char prefix[160];
        int prefixLength = caretCol < 159 ? caretCol : 159;
        memcpy(prefix, lineStart, (size_t)prefixLength);
        prefix[prefixLength] = '\0';
        if ((int)(GetTime() * 2) % 2 == 0 && !blocked)
            DrawRectangle((int)editor.x + 52 + MeasureText(prefix, 20), lineY + caretLine * 26, 3, 22, COLOR_YELLOW);
        EndScissorMode();
    }

    // ---- side column: Byte, speech bubble and info chips
    char bubbleText[160];
    if (app.qHint[app.current]) snprintf(bubbleText, sizeof bubbleText, "Hint: %s", question->hint);
    else if (question->kind == KIND_MCQ) snprintf(bubbleText, sizeof bubbleText, "Tap an answer! Stuck? Ask me for a HINT.");
    else if (question->kind == KIND_SHORT) snprintf(bubbleText, sizeof bubbleText, "Type it in and press ENTER!");
    else snprintf(bubbleText, sizeof bubbleText, "Write the function. CTRL+ENTER runs the tests!");
    int bubbleBottom = WrapText(bubbleText, 0, 0, 161, 16, COLOR_INK, false);
    DrawBubble((Rectangle){ 790, 98, 185, (float)(bubbleBottom + 14) }, bubbleText, 16, 880);
    BotMood mood = MOOD_THINK;
    float footY = 424;
    if (!app.sheetOpen) DrawBot(882, footY, 1.15f, mood);
    (void)footY; (void)mood;
    if (!app.sheetOpen) {
        char info[80];
        Rectangle chip = { 790, 440, 185, 34 };
        DrawRoundOutlined(chip, 17, 3, COLOR_CREAM, COLOR_INK);
        char fitted[48];
        FitText(app.levelIndex >= 0 ? levelNames[app.levelIndex] : "Weak spot practice", fitted, sizeof fitted, 165, 16);
        DrawTextCentered(fitted, 882, 449, 16, COLOR_INK);
        snprintf(info, sizeof info, "Question %d of %d", app.current + 1, app.questionCount);
        DrawTextCO(info, 882, 488, 20, WHITE, COLOR_INK);
        if (app.streak > 1) {
            snprintf(info, sizeof info, "STREAK x%d", app.streak);
            DrawRoundOutlined((Rectangle){ 800, 518, 165, 34 }, 17, 3, COLOR_YELLOW, COLOR_INK);
            DrawTextCentered(info, 882, 526, 20, COLOR_INK);
        }
    }

    // ---- bottom bar
    DrawHeaderBar(app.levelIndex >= 0 ? "LEVEL" : "PRACTICE", 70, true);
    if (app.levelIndex >= 0) {
        char number[8];
        snprintf(number, sizeof number, "%d", app.levelIndex + 1);
        DrawTextO(number, 70 + MeasureText("LEVEL ", 30), 14, 30, COLOR_YELLOW, COLOR_INK);
    }
    DrawQuestionPips();
    Rectangle quitButton = { 12, 8, 50, 44 };
    bool quit = DrawCandyButton(quitButton, "", COLOR_RED, true, 24);
    DrawXMark(quitButton);
    if (quit && !blocked) app.confirmQuit = true;

    if (DrawCandyButton((Rectangle){ 30, 572, 125, 54 }, "BACK", COLOR_PURPLE, app.current > 0, 22)) GoToQuestion(app.current - 1);
    bool hintClicked = DrawCandyButton((Rectangle){ 170, 572, 125, 54 }, "HINT", COLOR_ORANGE, !app.qHint[app.current], 22);
    if ((hintClicked || (!blocked && IsKeyPressed(KEY_F1))) && !app.qHint[app.current]) app.qHint[app.current] = true;
    bool others = NextUnresolved(app.current) >= 0;
    const char *skipLabel = !others ? "FINISH" : (IsResolved(app.qStatus[app.current]) ? "NEXT" : "SKIP");
    if (DrawCandyButton((Rectangle){ 310, 572, 150, 54 }, skipLabel, others ? COLOR_CYAN : COLOR_PINK, true, 22)) SkipOrNext();

    if (question->kind == KIND_SHORT) {
        bool submit = DrawCandyButton((Rectangle){ 790, 572, 185, 54 }, "SUBMIT", COLOR_GREEN, app.typedLength > 0, 24);
        if ((submit || (!blocked && IsKeyPressed(KEY_ENTER))) && app.typedLength > 0) {
            char detail[200];
            snprintf(detail, sizeof detail, "Expected: %.100s", question->acceptedAnswers);
            SubmitAnswer(question, MatchesAcceptedAnswer(app.typedAnswer, question->acceptedAnswers), detail, V2(500, 560));
            return;
        }
    } else if (question->kind == KIND_CODE) {
        bool run = DrawCandyButton((Rectangle){ 790, 572, 185, 54 }, "RUN TESTS", COLOR_GREEN, !blocked, 22);
        bool controlEnter = (IsKeyDown(KEY_LEFT_CONTROL) || IsKeyDown(KEY_RIGHT_CONTROL)) && IsKeyPressed(KEY_ENTER);
        if ((run || controlEnter) && !blocked) app.runCountdown = 2;
        if (app.runCountdown > 0) {
            DrawRectangle(0, 0, WINDOW_WIDTH, WINDOW_HEIGHT, Fade(COLOR_INK, 0.7f));
            DrawTextCO("COMPILING WITH GCC...", WINDOW_WIDTH / 2, 280, 44, COLOR_YELLOW, COLOR_INK);
            DrawBot(500, 470, 1.3f, MOOD_THINK);
            if (--app.runCountdown == 0) {
                char detail[300];
                bool passed = RunCodeChallenge(question, detail, sizeof detail);
                SubmitAnswer(question, passed, detail, V2(500, 400));
                return;
            }
        }
    }

    if (app.sheetOpen) {
        uiLocked = false;
        DrawFeedbackSheet();
        if (app.sheetOpen) DrawBot(882, WINDOW_HEIGHT - 252 * Smooth(app.sheetTime / 0.25f) + 8, 1.15f, app.lastAnswerCorrect ? MOOD_CHEER : MOOD_SAD);
    }
    if (app.confirmQuit) { uiLocked = false; DrawQuitConfirm(); }
    else if (IsKeyPressed(KEY_ESCAPE) && !app.sheetOpen && app.runCountdown == 0) app.confirmQuit = true;
}

// ---------------------------------------------------------------- results
static void ScreenResults(void) {
    float t = app.screenTime;
    bool practice = app.levelIndex < 0;
    bool pass = practice ? app.correctCount * 2 >= app.questionCount : app.levelGems > 0;
    DrawBackground(0.1f);
    DrawEllipse(170, 660, 430, 160, (Color){255, 170, 214, 255});
    DrawEllipse(830, 670, 470, 150, (Color){255, 146, 200, 255});
    if (pass && t < 2.0f && (int)(t * 60) % 4 == 0) SpawnConfetti(2);

    Rectangle card = { 250, 112, 500, 450 };
    DrawRound((Rectangle){ card.x + 4, card.y + 12, card.width, card.height }, 30, Fade(BLACK, 0.28f));
    DrawRoundOutlined(card, 30, 5, COLOR_CREAM, COLOR_INK);
    Rectangle ribbon = { card.x - 30, card.y - 34, card.width + 60, 70 };
    DrawRoundOutlined(ribbon, 20, 4, pass ? COLOR_GREEN : COLOR_PINK, COLOR_INK);
    DrawRound((Rectangle){ ribbon.x + 24, ribbon.y + 7, ribbon.width - 48, 15 }, 7, Fade(WHITE, 0.3f));
    DrawTextCO(practice ? "PRACTICE DONE!" : (pass ? "LEVEL CLEAR!" : "OH NO! TRY AGAIN"), 500, (int)ribbon.y + 16, 40, WHITE, COLOR_INK);

    char line[96];
    if (practice) snprintf(line, sizeof line, "Weak spot practice");
    else snprintf(line, sizeof line, "Level %d - %s", app.levelIndex + 1, levelNames[app.levelIndex]);
    DrawTextCentered(line, 500, (int)card.y + 50, 22, (Color){100, 70, 140, 255});
    if (!practice) DrawStarRow(500, card.y + 128, 36, app.levelGems, t - 0.3f);
    snprintf(line, sizeof line, "SCORE %d / %d      +%d XP", app.correctCount, app.questionCount, app.levelXp);
    DrawTextCentered(line, 500, (int)card.y + (practice ? 110 : 190), 28, COLOR_INK);

    float spacing = 46, startX = 500 - (app.questionCount - 1) * spacing / 2;
    int mistakes = 0;
    for (int i = 0; i < app.questionCount; i++) {
        Vector2 c = V2(startX + i * spacing, card.y + 244);
        if (app.qStatus[i] != ST_CORRECT) mistakes++;
        DrawCircleV(c, 19, COLOR_INK);
        DrawCircleV(c, 16, StatusColor(app.qStatus[i]));
        char number[4];
        snprintf(number, sizeof number, "%d", i + 1);
        DrawTextCentered(number, (int)c.x, (int)c.y - 9, 20, COLOR_INK);
    }
    const char *message;
    if (practice) message = pass ? "Nice! Those tricky ones are getting easier." : "Tricky ones take time. Check the report and go again!";
    else if (app.levelGems == 3) message = "Perfect! Every sweet answer was right.";
    else if (pass) message = "Checkpoint saved. Replay for 3 stars any time!";
    else message = "You need at least half right for a star. Review and retry!";
    WrapText(message, (int)card.x + 40, (int)card.y + 276, 420, 18, (Color){100, 70, 140, 255}, true);
    if (app.fixedCount > 0) {
        snprintf(line, sizeof line, "You fixed %d mistake%s on a retry!", app.fixedCount, app.fixedCount == 1 ? "" : "s");
        DrawTextCentered(line, 500, (int)card.y + 304, 18, (Color){20, 150, 190, 255});
    }

    if (DrawCandyButton((Rectangle){ card.x + 40, card.y + 330, 420, 54 }, mistakes ? "REVIEW MY MISTAKES" : "OPEN MY PROGRESS", COLOR_CYAN, true, 24)) {
        app.reviewTab = mistakes ? 2 : 0;
        app.reviewCount = 0;
        if (mistakes) { app.reviewCount = app.lastRunCount; for (int i = 0; i < app.lastRunCount; i++) { app.reviewIds[i] = app.lastRunIds[i]; app.reviewStatus[i] = app.lastRunStatus[i]; } app.reviewExpanded = -1; app.reviewScroll = 0; }
        SetScreen(SCREEN_REVIEW);
        return;
    }
    int nextLevel = app.levelIndex + 1;
    bool nextReady = !practice && nextLevel < LEVEL_COUNT && IsLevelUnlocked(app.mode, nextLevel);
    float by = card.y + 392;
    if (DrawCandyButton((Rectangle){ card.x + 25, by, 140, 54 }, "MAP", COLOR_PURPLE, true, 22)) { SetScreen(SCREEN_MAP); return; }
    if (DrawCandyButton((Rectangle){ card.x + 180, by, 140, 54 }, practice ? "AGAIN" : "REPLAY", COLOR_ORANGE, true, 22)) {
        if (practice) StartPractice(); else StartLevel(app.levelIndex);
        return;
    }
    if (DrawCandyButton((Rectangle){ card.x + 335, by, 140, 54 }, "NEXT", COLOR_GREEN, nextReady, 22)) { StartLevel(nextLevel); return; }

    DrawBot(125, 572, 1.7f, pass ? MOOD_CHEER : MOOD_SAD);
    DrawLollipop(V2(880, 420), 34, COLOR_PINK, COLOR_CREAM, -0.2f);
    DrawCupcake(V2(880, 612), 0.9f);
}

// ---------------------------------------------------------------- progress report
static int DrawQuestionDetail(const Question *q, int x, int y, int w, bool draw) {
    if (q->snippet[0]) {
        int h = CountLines(q->snippet) * 24 + 16;
        if (draw) {
            DrawRound((Rectangle){ (float)x, (float)y, (float)w, (float)h }, 10, COLOR_CODE_BG);
            DrawCodeBlock(q->snippet, x + 12, y + 8, 18, (Color){160, 255, 180, 255});
        }
        y += h + 10;
    }
    char answer[200] = "";
    if (q->kind == KIND_MCQ) snprintf(answer, sizeof answer, "Answer: %c) %s", 'A' + q->correctOption, q->options[q->correctOption]);
    else if (q->kind == KIND_SHORT) {
        char first[100];
        int n = 0;
        while (q->acceptedAnswers[n] && q->acceptedAnswers[n] != '|' && n < 99) { first[n] = q->acceptedAnswers[n]; n++; }
        first[n] = '\0';
        snprintf(answer, sizeof answer, "Answer: %s", first);
    }
    if (answer[0]) y = WrapText(answer, x, y, w, 20, (Color){20, 130, 70, 255}, draw);
    return WrapText(q->explanation, x, y, w, 18, COLOR_INK, draw);
}

static void BuildReviewList(int tab) {
    app.reviewCount = 0;
    app.reviewExpanded = -1;
    app.reviewScroll = 0;
    if (tab == 1) {
        app.reviewCount = CollectWeak(app.reviewIds, MAX_BANK);
        for (int i = 0; i < app.reviewCount; i++) app.reviewStatus[i] = ST_WRONG;
    } else if (tab == 2) {
        app.reviewCount = app.lastRunCount;
        for (int i = 0; i < app.lastRunCount; i++) { app.reviewIds[i] = app.lastRunIds[i]; app.reviewStatus[i] = app.lastRunStatus[i]; }
    }
}

static void DrawCountBadge(float x, float y, int value, Color color) {
    DrawCircleV(V2(x, y), 15, COLOR_INK);
    DrawCircleV(V2(x, y), 12, value > 0 ? color : (Color){225, 215, 238, 255});
    char text[8];
    snprintf(text, sizeof text, "%d", value);
    DrawTextCentered(text, (int)x, (int)y - 7, 16, COLOR_INK);
}

static void DrawReviewOverview(Rectangle panel) {
    int totalAttempts = 0, totalCorrect = 0, totalSkipped = 0;
    for (int i = 0; i < QUESTION_BANK_SIZE; i++) {
        totalAttempts += app.progress.stats[i].attempts;
        totalCorrect += app.progress.stats[i].correct;
        totalSkipped += app.progress.stats[i].skipped;
    }
    int percent = totalAttempts ? totalCorrect * 100 / totalAttempts : 0;
    Vector2 ring = V2(panel.x + 160, panel.y + 118);
    DrawRing(ring, 56, 80, 0, 360, 60, (Color){232, 218, 245, 255});
    if (totalAttempts) DrawRing(ring, 56, 80, 270, 270 + 360.0f * percent / 100.0f, 60, percent >= 80 ? COLOR_GREEN : (percent >= 50 ? COLOR_YELLOW : COLOR_RED));
    char text[64];
    snprintf(text, sizeof text, totalAttempts ? "%d%%" : "--", percent);
    DrawTextCentered(text, (int)ring.x, (int)ring.y - 20, 38, COLOR_INK);
    DrawTextCentered("correct", (int)ring.x, (int)ring.y + 18, 16, (Color){120, 90, 160, 255});
    int stars = TotalStars();
    snprintf(text, sizeof text, "Answers given: %d", totalAttempts);        DrawText(text, (int)panel.x + 30, (int)panel.y + 214, 20, COLOR_INK);
    snprintf(text, sizeof text, "Mistakes: %d", totalAttempts - totalCorrect); DrawText(text, (int)panel.x + 30, (int)panel.y + 238, 20, COLOR_INK);
    snprintf(text, sizeof text, "Skipped: %d", totalSkipped);                DrawText(text, (int)panel.x + 30, (int)panel.y + 262, 20, COLOR_INK);
    snprintf(text, sizeof text, "Stars: %d / 27", stars);                    DrawText(text, (int)panel.x + 30, (int)panel.y + 286, 20, COLOR_INK);

    // robot's advice
    int worstLevel = -1, worstPercent = 101;
    for (int l = 0; l < LEVEL_COUNT; l++) {
        int a, c, w;
        LevelReport(l, &a, &c, &w);
        if (a > 0 && c * 100 / a < worstPercent) { worstPercent = c * 100 / a; worstLevel = l; }
    }
    char advice[160];
    if (totalAttempts == 0) snprintf(advice, sizeof advice, "Play a level and I will track your strengths and weak spots here!");
    else if (worstPercent < 85) snprintf(advice, sizeof advice, "Focus on \"%s\": only %d%% right so far. Hit PLAY to improve it!", levelNames[worstLevel], worstPercent);
    else snprintf(advice, sizeof advice, "Looking strong! Try for 3 stars on every level.");
    Rectangle note = { panel.x + 24, panel.y + 318, 296, 76 };
    DrawRoundOutlined(note, 14, 3, (Color){255, 244, 200, 255}, COLOR_INK);
    WrapText(advice, (int)note.x + 12, (int)note.y + 8, (int)note.width - 24, 16, COLOR_INK, true);

    // per level rows
    DrawTextCentered("YOUR LEVELS", (int)(panel.x + 640), (int)panel.y + 10, 22, COLOR_INK);
    for (int l = 0; l < LEVEL_COUNT; l++) {
        Rectangle row = { panel.x + 350, panel.y + 42 + l * 72.0f, 570, 62 };
        DrawRoundOutlined(row, 18, 3, WHITE, (Color){228, 206, 240, 255});
        int attempts, correct, weak;
        LevelReport(l, &attempts, &correct, &weak);
        int pct = attempts ? correct * 100 / attempts : 0;
        DrawCircleV(V2(row.x + 32, row.y + 31), 22, COLOR_INK);
        DrawCircleV(V2(row.x + 32, row.y + 31), 19, LevelColor(l));
        char number[4];
        snprintf(number, sizeof number, "%d", l + 1);
        DrawTextCO(number, (int)row.x + 32, (int)row.y + 18, 26, WHITE, COLOR_INK);
        DrawText(levelNames[l], (int)row.x + 66, (int)row.y + 7, 20, COLOR_INK);
        Rectangle track = { row.x + 66, row.y + 36, 220, 14 };
        DrawRound(track, 7, (Color){232, 218, 245, 255});
        if (attempts) DrawRound((Rectangle){ track.x, track.y, track.width * pct / 100.0f < 14 ? 14 : track.width * pct / 100.0f, track.height }, 7, pct >= 80 ? COLOR_GREEN : (pct >= 50 ? COLOR_YELLOW : COLOR_RED));
        snprintf(number, sizeof number, "%d%%", pct);
        if (attempts) DrawText(number, (int)track.x + 232, (int)track.y - 3, 20, COLOR_INK);
        else DrawText("not played", (int)track.x + 232, (int)track.y - 1, 16, COLOR_GRAY);
        if (weak) {
            char weakText[24];
            snprintf(weakText, sizeof weakText, "%d weak", weak);
            DrawText(weakText, (int)row.x + 66 + MeasureText(levelNames[l], 20) + 12, (int)row.y + 10, 16, COLOR_RED);
        }
        int best = app.progress.bestGems[0][l] > app.progress.bestGems[1][l] ? app.progress.bestGems[0][l] : app.progress.bestGems[1][l];
        for (int s = 0; s < 3; s++) {
            Vector2 sc = V2(row.x + 392 + s * 28, row.y + 31);
            DrawStarShape(sc, 14, 0, COLOR_INK);
            DrawStarShape(sc, 11, 0, s < best ? COLOR_YELLOW : (Color){210, 195, 230, 255});
        }
        bool open = IsLevelUnlocked(app.mode, l);
        if (DrawCandyButton((Rectangle){ row.x + 474, row.y + 9, 82, 50 }, open ? "PLAY" : "LOCK", COLOR_GREEN, open, 18)) StartLevel(l);
    }
}

static void ScreenReview(void) {
    DrawBackground(0.18f);
    DrawHeaderBar("MY PROGRESS", 122, true);
    if (DrawCandyButton((Rectangle){ 12, 8, 100, 44 }, "BACK", COLOR_PINK, true, 20) || IsKeyPressed(KEY_ESCAPE)) { SetScreen(SCREEN_MAP); return; }

    const char *tabNames[3] = { "OVERVIEW", "WEAK SPOTS", "LAST RUN" };
    for (int tab = 0; tab < 3; tab++) {
        Rectangle r = { 30 + tab * 200.0f, 86, 190, 52 };
        if (DrawCandyButton(r, tabNames[tab], tab == app.reviewTab ? COLOR_PINK : COLOR_PURPLE, true, 22) && tab != app.reviewTab) { app.reviewTab = tab; BuildReviewList(tab); }
    }
    Rectangle panel = { 30, 148, 940, 406 };
    DrawRoundOutlined(panel, 26, 5, COLOR_CREAM, COLOR_INK);

    if (app.reviewTab == 0) {
        DrawReviewOverview(panel);
    } else {
        if (app.reviewCount == 0) {
            DrawTextCentered(app.reviewTab == 1 ? "No weak spots yet!" : "Nothing to review yet!", 500, 300, 32, COLOR_INK);
            DrawTextCentered(app.reviewTab == 1 ? "Keep playing - I will flag anything tricky here." : "Finish a level and its answers show up here.", 500, 346, 20, (Color){120, 90, 160, 255});
        } else {
            DrawText(app.reviewTab == 1 ? "Tricky questions first. Tap one to see the answer and why." : "Tap a question to see the answer and why.", (int)panel.x + 28, (int)panel.y + 14, 18, (Color){120, 90, 160, 255});
            Rectangle view = { panel.x + 20, panel.y + 44, panel.width - 40, panel.height - 56 };
            float total = 0;
            for (int i = 0; i < app.reviewCount; i++) {
                total += 62 + 8;
                if (i == app.reviewExpanded) total += DrawQuestionDetail(QuestionAt(app.reviewIds[i]), 0, 0, (int)view.width - 60, false) + 16;
            }
            float maxScroll = total - view.height > 0 ? total - view.height : 0;
            if (CheckCollisionPointRec(GetMousePosition(), view)) app.reviewScroll -= GetMouseWheelMove() * 50;
            app.reviewScroll = Clampf(app.reviewScroll, 0, maxScroll);
            BeginScissorMode((int)view.x, (int)view.y, (int)view.width, (int)view.height);
            float y = view.y - app.reviewScroll;
            for (int i = 0; i < app.reviewCount; i++) {
                int bankIndex = app.reviewIds[i];
                const Question *q = QuestionAt(bankIndex);
                QStat stat = app.progress.stats[bankIndex];
                bool expanded = i == app.reviewExpanded;
                int detailH = expanded ? DrawQuestionDetail(q, 0, 0, (int)view.width - 60, false) + 16 : 0;
                Rectangle head = { view.x + 4, y, view.width - 14, 62 };
                Rectangle whole = { head.x, head.y, head.width, head.height + detailH };
                DrawRoundOutlined(whole, 18, 3, WHITE, expanded ? LevelColor(q->levelIndex) : (Color){228, 206, 240, 255});
                QStatus status = app.reviewStatus[i];
                DrawCircleV(V2(head.x + 34, head.y + 31), 20, COLOR_INK);
                DrawCircleV(V2(head.x + 34, head.y + 31), 17, app.reviewTab == 2 ? StatusColor(status) : LevelColor(q->levelIndex));
                char tag[8];
                snprintf(tag, sizeof tag, "L%d", q->levelIndex + 1);
                if (app.reviewTab == 2) snprintf(tag, sizeof tag, "%d", i + 1);
                DrawTextCentered(tag, (int)head.x + 34, (int)head.y + 22, 18, COLOR_INK);
                char fitted[200];
                FitText(q->prompt, fitted, sizeof fitted, app.reviewTab == 1 ? 480 : 600, 20);
                DrawText(fitted, (int)head.x + 66, (int)head.y + 8, 20, COLOR_INK);
                if (q->snippet[0] && !expanded) {
                    FitText(q->snippet, fitted, sizeof fitted, 480, 16);
                    DrawText(fitted, (int)head.x + 66, (int)head.y + 36, 16, (Color){40, 150, 80, 255});
                } else if (!q->snippet[0] && !expanded) {
                    DrawText(levelNames[q->levelIndex], (int)head.x + 66, (int)head.y + 36, 16, (Color){120, 90, 160, 255});
                }
                if (app.reviewTab == 1) {
                    DrawCountBadge(head.x + head.width - 150, head.y + 31, stat.wrong, COLOR_RED);
                    DrawCountBadge(head.x + head.width - 110, head.y + 31, stat.skipped, COLOR_YELLOW);
                    DrawCountBadge(head.x + head.width - 70, head.y + 31, stat.correct, COLOR_GREEN);
                } else {
                    const char *label = status == ST_CORRECT ? "RIGHT" : (status == ST_WRONG ? "MISSED" : (status == ST_FIXED ? "FIXED" : "SKIPPED"));
                    Rectangle chip = { head.x + head.width - 126, head.y + 16, 108, 30 };
                    DrawRoundOutlined(chip, 15, 3, StatusColor(status), COLOR_INK);
                    DrawTextCentered(label, (int)(chip.x + chip.width / 2), (int)chip.y + 7, 16, COLOR_INK);
                }
                if (expanded) {
                    DrawLineEx(V2(head.x + 20, head.y + 64), V2(head.x + head.width - 20, head.y + 64), 2, (Color){228, 206, 240, 255});
                    DrawQuestionDetail(q, (int)head.x + 30, (int)head.y + 74, (int)view.width - 60, true);
                }
                bool inView = CheckCollisionPointRec(GetMousePosition(), view);
                if (TakeClick(inView && CheckCollisionPointRec(GetMousePosition(), head))) app.reviewExpanded = expanded ? -1 : i;
                y += 62 + detailH + 8;
            }
            EndScissorMode();
            if (maxScroll > 0) {
                float barH = view.height * view.height / total;
                DrawRound((Rectangle){ view.x + view.width - 8, view.y + (view.height - barH) * (app.reviewScroll / maxScroll), 6, barH }, 3, (Color){200, 170, 225, 255});
            }
        }
    }

    int weakIds[MAX_QUESTIONS_PER_LEVEL];
    int weakCount = CollectWeak(weakIds, MAX_QUESTIONS_PER_LEVEL);
    if (DrawCandyButton((Rectangle){ 30, 568, 230, 54 }, "LEVEL MAP", COLOR_PURPLE, true, 22)) { SetScreen(SCREEN_MAP); return; }
    char label[40];
    snprintf(label, sizeof label, weakCount ? "PRACTICE WEAK SPOTS" : "NO WEAK SPOTS!");
    if (DrawCandyButton((Rectangle){ 690, 568, 280, 54 }, label, weakCount ? COLOR_GREEN : COLOR_GRAY, weakCount > 0, 20)) StartPractice();
    DrawBot(480, 636, 0.62f, weakCount ? MOOD_THINK : MOOD_CHEER);
}

// ================================================================ main loop
static void InitApp(void) {
    SetExitKey(KEY_NULL);   // ESC is used for "back"
    for (int i = 0; i < SPARKLE_COUNT; i++) sparklePos[i] = V2((float)GetRandomValue(0, WINDOW_WIDTH), (float)GetRandomValue(0, WINDOW_HEIGHT));
    memset(&app, 0, sizeof app);
    app.selectedLevel = -1;
    LoadProgress();
}

static void FrameUpdate(void) {
    float dt = GetFrameTime();
    app.screenTime += dt;
    UpdateEffects(dt);
    clickConsumed = false;
    wantHand = false;
    uiLocked = false;
    BeginDrawing();
    switch (app.screen) {
        case SCREEN_TITLE:    ScreenTitle();    break;
        case SCREEN_MODE:     ScreenMode();     break;
        case SCREEN_MAP:      ScreenMap();      break;
        case SCREEN_QUESTION: ScreenQuestion(); break;
        case SCREEN_RESULTS:  ScreenResults();  break;
        case SCREEN_REVIEW:   ScreenReview();   break;
    }
    DrawEffects();
    if (app.screenTime < 0.25f) DrawRectangle(0, 0, WINDOW_WIDTH, WINDOW_HEIGHT, Fade(COLOR_INK, 1.0f - app.screenTime / 0.25f));
    EndDrawing();
    SetMouseCursor(wantHand ? MOUSE_CURSOR_POINTING_HAND : MOUSE_CURSOR_DEFAULT);
}

int main(void) {
    SetConfigFlags(FLAG_MSAA_4X_HINT);
    InitWindow(WINDOW_WIDTH, WINDOW_HEIGHT, "C Quest");
    SetTargetFPS(60);
    InitApp();
    while (!WindowShouldClose()) FrameUpdate();
    CloseWindow();
    return 0;
}
