// C QUEST - a retro pixel-art learning app for C basics. Plain C + raylib only.
// Build (w64devkit, raylib 5.5):
//   gcc main.c -o cquest.exe -I<raylib>/include -L<raylib>/lib -lraylib -lopengl32 -lgdi32 -lwinmm
// Two modes: MCQ (pick the answer) and TYPED (type answers, then a stretch
// "Function Lab" where your code is compiled with gcc and tested).
#include "raylib.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

#define WINDOW_WIDTH 1000
#define WINDOW_HEIGHT 640
#define LEVEL_COUNT 5
#define MAX_QUESTIONS_PER_LEVEL 4
#define STAR_COUNT 70
#define PROGRESS_FILE "cquest_progress.dat"
#ifdef _WIN32
#define ATTEMPT_EXE_FILE "cquest_attempt.exe"
#define ATTEMPT_RUN_COMMAND "cquest_attempt.exe"
#else
#define ATTEMPT_EXE_FILE "cquest_attempt"
#define ATTEMPT_RUN_COMMAND "./cquest_attempt"
#endif

#define COLOR_BG      ((Color){22, 14, 46, 255})
#define COLOR_PANEL   ((Color){44, 30, 92, 255})
#define COLOR_INK     ((Color){10, 6, 24, 255})
#define COLOR_YELLOW  ((Color){255, 214, 64, 255})
#define COLOR_PINK    ((Color){255, 92, 168, 255})
#define COLOR_CYAN    ((Color){72, 224, 255, 255})
#define COLOR_GREEN   ((Color){96, 232, 120, 255})
#define COLOR_RED     ((Color){255, 84, 84, 255})
#define COLOR_TEXT    ((Color){240, 240, 255, 255})
#define COLOR_GRAY    ((Color){110, 104, 140, 255})

typedef enum { SCREEN_TITLE, SCREEN_MODE, SCREEN_LEVELS, SCREEN_QUESTION, SCREEN_FEEDBACK, SCREEN_RESULTS } ScreenState;
typedef enum { MODE_MCQ, MODE_TYPED, MODE_COUNT } QuizMode;
typedef enum { KIND_MCQ, KIND_SHORT, KIND_CODE } QuestionKind;

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

typedef struct {
    int bestGems[MODE_COUNT][LEVEL_COUNT];
    int totalXp;
} Progress;

typedef struct {
    ScreenState screen;
    QuizMode mode;
    int levelIndex;
    int questionIds[MAX_QUESTIONS_PER_LEVEL];
    int questionCount, questionNumber;
    int correctCount, streak, levelXp, levelGems;
    bool hintShown, lastAnswerCorrect;
    int runCountdown;
    char typedAnswer[96];
    int typedLength;
    char codeAnswer[1024];
    int codeLength;
    char feedbackText[600];
    Progress progress;
} AppState;

static AppState app;
static Vector2 starPositions[STAR_COUNT];
static const char *levelNames[LEVEL_COUNT] = {
    "Variables & Types", "Operators & Casting", "Decisions & Loops", "Arrays & Strings", "Function Lab (stretch)"
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

// ---------------------------------------------------------------- helpers
static void DrawTextCentered(const char *text, int centerX, int y, int fontSize, Color color) {
    DrawText(text, centerX - MeasureText(text, fontSize) / 2, y, fontSize, color);
}

static int DrawTextWrapped(const char *text, int x, int y, int maxWidth, int fontSize, Color color) {
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
            DrawText(lineBuffer, x, y, fontSize, color);
            y += lineHeight;
            snprintf(lineBuffer, sizeof lineBuffer, "%s", word);
        } else {
            snprintf(lineBuffer, sizeof lineBuffer, "%s", trial);
        }
        if (*cursor == '\n') {
            DrawText(lineBuffer, x, y, fontSize, color);
            y += lineHeight;
            lineBuffer[0] = '\0';
            cursor++;
        } else if (*cursor == ' ') {
            cursor++;
        }
    }
    if (lineBuffer[0]) { DrawText(lineBuffer, x, y, fontSize, color); y += lineHeight; }
    return y;
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
        while (*cursor && *cursor != '\n') cursor++;   // skip any over-long remainder
        if (*cursor == '\n') cursor++; else break;
    }
}

static bool DrawPixelButton(Rectangle bounds, const char *label, Color fill, bool enabled) {
    bool hovered = enabled && CheckCollisionPointRec(GetMousePosition(), bounds);
    bool pressed = hovered && IsMouseButtonDown(MOUSE_BUTTON_LEFT);
    int shift = pressed ? 4 : 0;
    Rectangle face = { bounds.x + shift, bounds.y + shift, bounds.width, bounds.height };
    DrawRectangle((int)bounds.x + 6, (int)bounds.y + 6, (int)bounds.width, (int)bounds.height, COLOR_INK);
    DrawRectangleRec(face, enabled ? (hovered ? COLOR_TEXT : fill) : COLOR_GRAY);
    DrawRectangleLinesEx(face, 4, COLOR_INK);
    int fontSize = 20;
    DrawText(label, (int)(face.x + face.width / 2) - MeasureText(label, fontSize) / 2,
             (int)(face.y + face.height / 2) - fontSize / 2, fontSize, COLOR_INK);
    return hovered && IsMouseButtonReleased(MOUSE_BUTTON_LEFT);
}

static void DrawGems(int centerX, int centerY, int gemCount, int gemSize) {
    for (int gemIndex = 0; gemIndex < 3; gemIndex++) {
        Vector2 center = { (float)(centerX + (gemIndex - 1) * gemSize * 2.6f), (float)centerY };
        DrawPoly(center, 4, (float)gemSize, 0, gemIndex < gemCount ? COLOR_YELLOW : COLOR_INK);
        DrawPolyLinesEx(center, 4, (float)gemSize, 0, 3, gemIndex < gemCount ? COLOR_TEXT : COLOR_GRAY);
    }
}

// Byte the robot: mood 0 = happy, 1 = sad.
static void DrawBot(int x, int y, int pixel, int mood) {
    y += (int)(sinf((float)GetTime() * 4.0f) * pixel * 0.5f);
    DrawRectangle(x + 3 * pixel, y, 2 * pixel, pixel, COLOR_PINK);
    DrawRectangle(x + 4 * pixel - pixel / 2, y + pixel, pixel, pixel, COLOR_GRAY);
    Rectangle head = { (float)x, (float)(y + 2 * pixel), (float)(8 * pixel), (float)(6 * pixel) };
    DrawRectangleRec(head, COLOR_CYAN);
    DrawRectangleLinesEx(head, (float)pixel / 2, COLOR_INK);
    DrawRectangle(x + 2 * pixel, y + 4 * pixel, pixel, pixel, COLOR_INK);
    DrawRectangle(x + 5 * pixel, y + 4 * pixel, pixel, pixel, COLOR_INK);
    if (mood == 0) {
        DrawRectangle(x + 2 * pixel, y + 6 * pixel, 4 * pixel, pixel, COLOR_INK);
        DrawRectangle(x + pixel, y + 5 * pixel, pixel, pixel, COLOR_INK);
        DrawRectangle(x + 6 * pixel, y + 5 * pixel, pixel, pixel, COLOR_INK);
    } else {
        DrawRectangle(x + 2 * pixel, y + 5 * pixel, 4 * pixel, pixel, COLOR_INK);
        DrawRectangle(x + pixel, y + 6 * pixel, pixel, pixel, COLOR_INK);
        DrawRectangle(x + 6 * pixel, y + 6 * pixel, pixel, pixel, COLOR_INK);
    }
}

static void DrawBackground(void) {
    ClearBackground(COLOR_BG);
    for (int starIndex = 0; starIndex < STAR_COUNT; starIndex++) {
        int alpha = 90 + (int)(80 * sinf((float)GetTime() * 2.0f + starIndex));
        DrawRectangle((int)starPositions[starIndex].x, (int)starPositions[starIndex].y, 4, 4, (Color){255, 255, 255, (unsigned char)alpha});
    }
}

static void DrawScanlines(void) {
    for (int y = 0; y < WINDOW_HEIGHT; y += 4) DrawRectangle(0, y, WINDOW_WIDTH, 1, (Color){0, 0, 0, 40});
}

static void DrawHeader(const char *title) {
    DrawRectangle(0, 0, WINDOW_WIDTH, 56, COLOR_PANEL);
    DrawRectangle(0, 52, WINDOW_WIDTH, 4, COLOR_PINK);
    DrawText(title, 20, 14, 30, COLOR_YELLOW);
    char stats[64];
    snprintf(stats, sizeof stats, "XP %d   STREAK %d", app.progress.totalXp, app.streak);
    DrawText(stats, WINDOW_WIDTH - MeasureText(stats, 20) - 20, 18, 20, COLOR_CYAN);
}

// ---------------------------------------------------------------- game logic
static void LoadProgress(void) {
    if (!FileExists(PROGRESS_FILE)) return;
    int size = 0;
    unsigned char *data = LoadFileData(PROGRESS_FILE, &size);
    if (data && size == (int)sizeof(Progress)) memcpy(&app.progress, data, sizeof(Progress));
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

static const Question *CurrentQuestion(void) { return &questionBank[app.questionIds[app.questionNumber]]; }

static void LoadQuestionInputs(void) {
    const Question *question = CurrentQuestion();
    app.hintShown = false;
    app.typedAnswer[0] = '\0';
    app.typedLength = 0;
    snprintf(app.codeAnswer, sizeof app.codeAnswer, "%s", question->starterCode);
    app.codeLength = (int)strlen(app.codeAnswer);
}

static void StartLevel(int level) {
    app.levelIndex = level;
    app.questionCount = 0;
    for (int bankIndex = 0; bankIndex < QUESTION_BANK_SIZE && app.questionCount < MAX_QUESTIONS_PER_LEVEL; bankIndex++) {
        const Question *question = &questionBank[bankIndex];
        bool isMcq = question->kind == KIND_MCQ;
        if (question->levelIndex == level && (app.mode == MODE_MCQ) == isMcq) app.questionIds[app.questionCount++] = bankIndex;
    }
    app.questionNumber = 0;
    app.correctCount = 0;
    app.streak = 0;
    app.levelXp = 0;
    app.runCountdown = 0;
    LoadQuestionInputs();
    app.screen = SCREEN_QUESTION;
}

static void SubmitAnswer(const Question *question, bool correct, const char *detail) {
    app.lastAnswerCorrect = correct;
    if (correct) {
        app.correctCount++;
        app.streak++;
        int gain = question->kind == KIND_CODE ? 25 : 10;
        if (app.hintShown) gain /= 2;
        gain += (app.streak - 1 < 4 ? app.streak - 1 : 4) * 2;
        app.levelXp += gain;
        app.progress.totalXp += gain;
        snprintf(app.feedbackText, sizeof app.feedbackText, "%s", question->explanation);
    } else {
        app.streak = 0;
        snprintf(app.feedbackText, sizeof app.feedbackText, "%s\n%s", detail, question->explanation);
    }
    app.screen = SCREEN_FEEDBACK;
}

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

static void HandleTextInput(char *buffer, int *length, int capacity, bool multiline) {
    for (int key = GetCharPressed(); key > 0; key = GetCharPressed())
        if (key >= 32 && key <= 126 && *length < capacity - 1) { buffer[(*length)++] = (char)key; buffer[*length] = '\0'; }
    if ((IsKeyPressed(KEY_BACKSPACE) || IsKeyPressedRepeat(KEY_BACKSPACE)) && *length > 0) buffer[--(*length)] = '\0';
    bool controlHeld = IsKeyDown(KEY_LEFT_CONTROL) || IsKeyDown(KEY_RIGHT_CONTROL);
    if (multiline && IsKeyPressed(KEY_ENTER) && !controlHeld && *length < capacity - 1) { buffer[(*length)++] = '\n'; buffer[*length] = '\0'; }
    if (multiline && IsKeyPressed(KEY_TAB) && *length < capacity - 5) { for (int i = 0; i < 4; i++) buffer[(*length)++] = ' '; buffer[*length] = '\0'; }
}

// ---------------------------------------------------------------- screens
static void ScreenTitle(void) {
    DrawTextCentered("C QUEST", WINDOW_WIDTH / 2 + 6, 126, 100, COLOR_PINK);
    DrawTextCentered("C QUEST", WINDOW_WIDTH / 2, 120, 100, COLOR_YELLOW);
    DrawTextCentered("learn C the retro way", WINDOW_WIDTH / 2, 240, 30, COLOR_CYAN);
    DrawBot(WINDOW_WIDTH / 2 - 56, 310, 14, 0);
    if ((int)(GetTime() * 2) % 2 == 0) DrawTextCentered("PRESS ENTER OR CLICK TO START", WINDOW_WIDTH / 2, 560, 30, COLOR_TEXT);
    if (IsKeyPressed(KEY_ENTER) || IsKeyPressed(KEY_SPACE) || IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) app.screen = SCREEN_MODE;
}

static void ScreenMode(void) {
    DrawHeader("CHOOSE YOUR MODE");
    DrawBot(440, 76, 7, 0);
    if (DrawPixelButton((Rectangle){60, 190, 400, 90}, "MCQ MODE", COLOR_GREEN, true)) { app.mode = MODE_MCQ; app.screen = SCREEN_LEVELS; }
    DrawTextWrapped("Pick the right answer from four choices. Press 1-4 or click. Great for warming up.", 60, 310, 400, 20, COLOR_TEXT);
    if (DrawPixelButton((Rectangle){540, 190, 400, 90}, "TYPED MODE", COLOR_PINK, true)) { app.mode = MODE_TYPED; app.screen = SCREEN_LEVELS; }
    DrawTextWrapped("Type your answers. Then unlock the Function Lab, where your code is compiled and tested with gcc.", 540, 310, 400, 20, COLOR_TEXT);
    DrawText("ESC = back", 20, 600, 20, COLOR_GRAY);
    if (IsKeyPressed(KEY_ESCAPE)) app.screen = SCREEN_TITLE;
}

static void ScreenLevels(void) {
    DrawHeader(app.mode == MODE_MCQ ? "LEVEL MAP - MCQ MODE" : "LEVEL MAP - TYPED MODE");
    for (int level = 0; level < LEVEL_COUNT; level++) {
        Rectangle card = { 60, (float)(80 + level * 100), 880, 84 };
        bool available = IsLevelAvailable(app.mode, level);
        bool unlocked = IsLevelUnlocked(app.mode, level);
        char title[80];
        snprintf(title, sizeof title, "%d. %s", level + 1, levelNames[level]);
        if (DrawPixelButton(card, "", level == LEVEL_COUNT - 1 ? COLOR_PINK : COLOR_CYAN, unlocked)) StartLevel(level);
        DrawText(title, 84, (int)card.y + 16, 30, COLOR_INK);
        const char *status = !available ? "TYPED MODE ONLY" : (!unlocked ? "LOCKED: earn a gem on the level before" : "READY");
        DrawText(status, 84, (int)card.y + 52, 20, COLOR_INK);
        if (available) DrawGems(840, (int)card.y + 42, app.progress.bestGems[app.mode][level], 14);
    }
    DrawText("ESC = back", 20, 600, 20, COLOR_GRAY);
    if (IsKeyPressed(KEY_ESCAPE)) app.screen = SCREEN_MODE;
}

static void ScreenQuestion(void) {
    const Question *question = CurrentQuestion();
    char title[64];
    snprintf(title, sizeof title, "LEVEL %d  %s", app.levelIndex + 1, levelNames[app.levelIndex]);
    DrawHeader(title);
    for (int pip = 0; pip < app.questionCount; pip++)
        DrawRectangle(20 + pip * 40, 66, 32, 10, pip < app.questionNumber ? COLOR_GREEN : (pip == app.questionNumber ? COLOR_YELLOW : COLOR_GRAY));
    DrawBot(850, 78, 7, 0);
    DrawTextWrapped(question->prompt, 40, 90, 780, 20, COLOR_TEXT);
    if (question->snippet[0]) {
        DrawRectangle(40, 150, 920, 180, COLOR_INK);
        DrawRectangleLinesEx((Rectangle){40, 150, 920, 180}, 4, COLOR_CYAN);
        DrawCodeBlock(question->snippet, 60, 164, 20, COLOR_GREEN);
    }
    bool inputBlocked = app.runCountdown > 0;

    if (question->kind == KIND_MCQ) {
        Color optionColors[4] = { COLOR_CYAN, COLOR_PINK, COLOR_GREEN, COLOR_YELLOW };
        int chosen = -1;
        for (int option = 0; option < 4; option++) {
            char label[160];
            snprintf(label, sizeof label, "%c) %s", 'A' + option, question->options[option]);
            if (DrawPixelButton((Rectangle){40, (float)(350 + option * 52), 900, 44}, label, optionColors[option], true) || IsKeyPressed(KEY_ONE + option)) chosen = option;
        }
        if (chosen >= 0) {
            char detail[200];
            snprintf(detail, sizeof detail, "Answer: %c) %s", 'A' + question->correctOption, question->options[question->correctOption]);
            SubmitAnswer(question, chosen == question->correctOption, detail);
            return;
        }
    } else if (question->kind == KIND_SHORT) {
        DrawText("Type your answer, then press ENTER", 40, 350, 20, COLOR_YELLOW);
        HandleTextInput(app.typedAnswer, &app.typedLength, sizeof app.typedAnswer, false);
        DrawRectangle(40, 390, 700, 56, COLOR_INK);
        DrawRectangleLinesEx((Rectangle){40, 390, 700, 56}, 4, COLOR_PINK);
        char shown[110];
        snprintf(shown, sizeof shown, "%s%s", app.typedAnswer, (int)(GetTime() * 2) % 2 == 0 ? "_" : "");
        DrawText(shown, 56, 406, 30, COLOR_TEXT);
        bool submit = DrawPixelButton((Rectangle){760, 390, 180, 56}, "SUBMIT", COLOR_GREEN, true) || IsKeyPressed(KEY_ENTER);
        if (submit && app.typedLength > 0) {
            char detail[200];
            snprintf(detail, sizeof detail, "Expected: %.100s", question->acceptedAnswers);
            SubmitAnswer(question, MatchesAcceptedAnswer(app.typedAnswer, question->acceptedAnswers), detail);
            return;
        }
    } else {
        if (!inputBlocked) HandleTextInput(app.codeAnswer, &app.codeLength, sizeof app.codeAnswer, true);
        DrawRectangle(40, 130, 920, 390, COLOR_INK);
        DrawRectangleLinesEx((Rectangle){40, 130, 920, 390}, 4, COLOR_PINK);
        char shown[1040];
        snprintf(shown, sizeof shown, "%s%s", app.codeAnswer, (int)(GetTime() * 2) % 2 == 0 ? "_" : "");
        DrawCodeBlock(shown, 60, 146, 20, COLOR_GREEN);
        bool run = DrawPixelButton((Rectangle){40, 540, 220, 50}, "RUN TESTS", COLOR_GREEN, !inputBlocked);
        DrawText("CTRL+ENTER runs. TAB indents.", 290, 556, 20, COLOR_GRAY);
        bool controlEnter = (IsKeyDown(KEY_LEFT_CONTROL) || IsKeyDown(KEY_RIGHT_CONTROL)) && IsKeyPressed(KEY_ENTER);
        if ((run || controlEnter) && !inputBlocked) app.runCountdown = 2;
        if (inputBlocked) {
            DrawRectangle(0, 0, WINDOW_WIDTH, WINDOW_HEIGHT, (Color){0, 0, 0, 170});
            DrawTextCentered("COMPILING WITH GCC...", WINDOW_WIDTH / 2, 290, 40, COLOR_YELLOW);
            if (--app.runCountdown == 0) {
                char detail[300];
                bool passed = RunCodeChallenge(question, detail, sizeof detail);
                SubmitAnswer(question, passed, detail);
                return;
            }
        }
    }

    int hintY = question->kind == KIND_CODE ? 540 : 596;
    if (DrawPixelButton((Rectangle){780, (float)(hintY - (question->kind == KIND_CODE ? 0 : 6)), 180, question->kind == KIND_CODE ? 50 : 36}, "HINT (F1)", COLOR_YELLOW, !inputBlocked) || IsKeyPressed(KEY_F1)) app.hintShown = true;
    if (app.hintShown) {
        char hintText[200];
        snprintf(hintText, sizeof hintText, "HINT: %s", question->hint);
        DrawText(hintText, 40, question->kind == KIND_CODE ? 600 : 604, 20, COLOR_YELLOW);
    }
    if (IsKeyPressed(KEY_ESCAPE)) app.screen = SCREEN_LEVELS;
}

static void FinishLevel(void) {
    app.levelGems = GemsForScore(app.correctCount, app.questionCount);
    if (app.levelGems > app.progress.bestGems[app.mode][app.levelIndex]) app.progress.bestGems[app.mode][app.levelIndex] = app.levelGems;
    SaveProgress();
    app.screen = SCREEN_RESULTS;
}

static void ScreenFeedback(void) {
    Color bannerColor = app.lastAnswerCorrect ? COLOR_GREEN : COLOR_RED;
    DrawHeader("FEEDBACK");
    DrawRectangle(0, 90, WINDOW_WIDTH, 100, bannerColor);
    DrawTextCentered(app.lastAnswerCorrect ? "NICE ONE!" : "NOT QUITE...", WINDOW_WIDTH / 2, 118, 60, COLOR_INK);
    DrawBot(WINDOW_WIDTH / 2 - 56, 220, 14, app.lastAnswerCorrect ? 0 : 1);
    DrawRectangle(60, 350, 880, 170, COLOR_INK);
    DrawRectangleLinesEx((Rectangle){60, 350, 880, 170}, 4, COLOR_CYAN);
    DrawTextWrapped(app.feedbackText, 80, 366, 840, 20, COLOR_TEXT);
    if (app.lastAnswerCorrect && app.streak > 1) {
        char streakText[40];
        snprintf(streakText, sizeof streakText, "STREAK x%d", app.streak);
        DrawText(streakText, 80, 490, 20, COLOR_YELLOW);
    }
    if (DrawPixelButton((Rectangle){380, 550, 240, 56}, "NEXT (ENTER)", COLOR_YELLOW, true) || IsKeyPressed(KEY_ENTER)) {
        app.questionNumber++;
        if (app.questionNumber >= app.questionCount) FinishLevel();
        else { LoadQuestionInputs(); app.screen = SCREEN_QUESTION; }
    }
}

static void ScreenResults(void) {
    DrawHeader("CHECKPOINT");
    DrawTextCentered(app.levelGems > 0 ? "LEVEL CLEAR!" : "TRY AGAIN!", WINDOW_WIDTH / 2, 90, 60, app.levelGems > 0 ? COLOR_GREEN : COLOR_RED);
    DrawGems(WINDOW_WIDTH / 2, 200, app.levelGems, 30);
    char scoreText[80];
    snprintf(scoreText, sizeof scoreText, "SCORE %d / %d      +%d XP", app.correctCount, app.questionCount, app.levelXp);
    DrawTextCentered(scoreText, WINDOW_WIDTH / 2, 270, 30, COLOR_TEXT);
    DrawTextCentered(app.levelGems > 0 ? "Checkpoint saved. The next level is open." : "Get at least half right to earn a gem and unlock the next level.", WINDOW_WIDTH / 2, 320, 20, COLOR_CYAN);
    DrawBot(WINDOW_WIDTH / 2 - 56, 360, 8, app.levelGems > 0 ? 0 : 1);
    int nextLevel = app.levelIndex + 1;
    bool nextReady = nextLevel < LEVEL_COUNT && IsLevelUnlocked(app.mode, nextLevel);
    if (DrawPixelButton((Rectangle){60, 520, 260, 60}, "NEXT LEVEL", COLOR_GREEN, nextReady)) StartLevel(nextLevel);
    if (DrawPixelButton((Rectangle){370, 520, 260, 60}, "RETRY", COLOR_YELLOW, true)) StartLevel(app.levelIndex);
    if (DrawPixelButton((Rectangle){680, 520, 260, 60}, "LEVEL MAP", COLOR_CYAN, true)) app.screen = SCREEN_LEVELS;
}

int main(void) {
    InitWindow(WINDOW_WIDTH, WINDOW_HEIGHT, "C Quest");
    SetExitKey(KEY_NULL);   // ESC is used for "back"
    SetTargetFPS(60);
    for (int starIndex = 0; starIndex < STAR_COUNT; starIndex++)
        starPositions[starIndex] = (Vector2){ (float)GetRandomValue(0, WINDOW_WIDTH), (float)GetRandomValue(0, WINDOW_HEIGHT) };
    LoadProgress();

    while (!WindowShouldClose()) {
        BeginDrawing();
        DrawBackground();
        switch (app.screen) {
            case SCREEN_TITLE:    ScreenTitle();    break;
            case SCREEN_MODE:     ScreenMode();     break;
            case SCREEN_LEVELS:   ScreenLevels();   break;
            case SCREEN_QUESTION: ScreenQuestion(); break;
            case SCREEN_FEEDBACK: ScreenFeedback(); break;
            case SCREEN_RESULTS:  ScreenResults();  break;
        }
        DrawScanlines();
        EndDrawing();
    }
    CloseWindow();
    return 0;
}
