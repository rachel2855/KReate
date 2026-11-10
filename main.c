// C QUEST - a retro cyberpunk learning app for C basics. Plain C + raylib only.
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
#include "rlgl.h"
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
#define BG_CANDY_COUNT 20
#define SPARKLE_COUNT 36
#define PROGRESS_FILE "cquest_progress.dat"
#ifdef _WIN32
#define ATTEMPT_EXE_FILE "cquest_attempt.exe"
#define ATTEMPT_RUN_COMMAND "cquest_attempt.exe"
#else
#define ATTEMPT_EXE_FILE "cquest_attempt"
#define ATTEMPT_RUN_COMMAND "./cquest_attempt"
#endif

// Calm cyber palette: teal, blues and purples only (wrong-answer rose is the single warm accent).
#define COLOR_BG_TOP  ((Color){6, 10, 30, 255})
#define COLOR_BG_BOT  ((Color){26, 20, 62, 255})
#define COLOR_PANEL   ((Color){18, 24, 54, 255})
#define COLOR_CREAM   ((Color){14, 20, 46, 255})      // dark card fill
#define COLOR_INK     ((Color){3, 6, 18, 255})        // outlines and shadows
#define COLOR_YELLOW  ((Color){150, 198, 255, 255})   // sky blue (stars, highlights)
#define COLOR_PINK    ((Color){196, 128, 232, 255})   // orchid
#define COLOR_CYAN    ((Color){62, 206, 210, 255})    // teal
#define COLOR_GREEN   ((Color){70, 214, 170, 255})    // sea teal (success)
#define COLOR_RED     ((Color){222, 100, 152, 255})   // soft rose (mistakes only)
#define COLOR_ORANGE  ((Color){110, 142, 255, 255})   // cornflower blue
#define COLOR_PURPLE  ((Color){152, 112, 232, 255})
#define COLOR_TEXT    ((Color){214, 230, 246, 255})
#define COLOR_TXT     ((Color){214, 230, 246, 255})   // body text on dark cards
#define COLOR_DIM     ((Color){140, 160, 200, 255})
#define COLOR_NEON    ((Color){62, 190, 208, 255})
#define COLOR_GRAY    ((Color){112, 126, 166, 255})
#define COLOR_CODE_BG ((Color){8, 12, 34, 255})
#define COLOR_CODE_TXT ((Color){158, 232, 226, 255})

typedef enum { SCREEN_TITLE, SCREEN_PROFILES, SCREEN_HUB, SCREEN_NOTES, SCREEN_MAP, SCREEN_QUESTION, SCREEN_RESULTS, SCREEN_REVIEW } ScreenState;
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
    int totalXp;             // shown to the player as HP
    QStat stats[MAX_BANK];   // indexed by position in questionBank; older save files simply load without it
    int boost[3];            // owned boosters: 50/50, shield, 2x HP
    int milestone;           // HP milestones already rewarded
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
    float introTime, qTime, hopTime, unlockTime, popupTime;
    int pendingHop, hopFrom, hopTo, unlockLevel, autoOpenLevel;
    char toast[96];
    float toastTime;
    // study notes + flashcards
    int notesLevel, notesTab, cardIndex;     // notesLevel -1 = module list
    float notesScroll, cardFlip;
    bool cardFlipped;
    // players
    int nameModal;                           // 0 closed, 1 new player, 2 save current progress under a name
    char nameBuf[24];
    int nameLen, confirmDelete;
    float profileScroll;
    // boosters used in this run
    bool shieldOn, doubleOn, used5050[MAX_QUESTIONS_PER_LEVEL];
    int bonusHp;
    char rewardText[96];
    Progress progress;
} AppState;

typedef struct { Vector2 pos, vel; float life, maxLife, size; Color color; int shape; } Particle;
typedef struct { char text[24]; Vector2 pos; float life; Color color; } FloatText;

static AppState app;
static Particle particles[MAX_PARTICLES];
static FloatText floatTexts[MAX_FLOATS];
static Vector2 sparklePos[SPARKLE_COUNT];
static bool uiLocked, clickConsumed, wantHand;
static const int mapNodeCount[MODE_COUNT] = { 4, 5 };   // the Function Lab only exists on the typing map
static const Vector2 nodePos[MODE_COUNT][LEVEL_COUNT] = {
    { {140, 440}, {350, 330}, {575, 440}, {800, 300}, {0, 0} },
    { {120, 450}, {320, 345}, {510, 455}, {690, 335}, {870, 215} },
};
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

// ================================================================ stage names, study notes and flashcards
// Fun district names for each map (C / C++ jokes with a cyberpunk twist).
static const char *stageNames[MODE_COUNT][LEVEL_COUNT] = {
    { "Boot Sector", "Operator Overpass", "Infinite Loop Lane", "Null-Terminator Alley", "Function Foundry" },
    { "Cursor Cove", "Semicolon Strip", "Segfault Slums", "Stack Overflow Heights", "Main() Megatower" },
};

// Notes: every string starts with a tag. '#' heading, '~' paragraph, '-' bullet, '>' code line, '' (empty) = gap.
static const char *notes0[] = {
    "#Variables",
    "~A variable is a named box in memory that holds a value. In C you must say what TYPE of value the box holds when you declare it.",
    ">int age = 25;",
    ">double price = 9.99;",
    ">char grade = 'A';",
    "#The basic types",
    "-int: whole numbers such as 25 or -3.",
    "-float / double: numbers with decimals. double is more precise.",
    "-char: ONE character, written in single quotes like 'A'.",
    "#printf format specifiers",
    "~printf needs a placeholder for each value you print. The placeholder must match the type.",
    "-%d prints an int.",
    "-%f prints a float or double. %.2f prints exactly 2 digits after the decimal point.",
    "-%c prints a single char.",
    "-%s prints a string.",
    ">printf(\"Age: %d, Grade: %c\\n\", age, grade);",
    "#Uninitialised variables",
    "~If you never give a local variable a value, C does NOT set it to 0. It holds whatever bytes were already in that bit of memory (a garbage value). Always give variables a starting value.",
    ">int var;      // garbage!",
    ">int total = 0; // safe",
    "#const",
    "~Put const in front of a declaration to make the variable read-only. Trying to change it is a compile error. (final and constant are NOT C keywords.)",
    ">const double TAX_RATE = 0.08;",
};
static const char *notes1[] = {
    "#Arithmetic operators",
    "~C has + - * / and % (remainder). They work as you expect, with * / % done before + -.",
    "#Integer division",
    "~When BOTH numbers are ints, the result is an int and the fraction is thrown away.",
    ">int a = 10, b = 3;",
    ">printf(\"%d\", a / b);   // 3, not 3.33",
    "#Remainder %",
    "~% gives what is left over after dividing. 10 % 3 is 1 because 10 = 3 * 3 + 1. A handy trick: n % 2 == 0 means n is even.",
    "#Compound assignment",
    "~Shortcuts that change a variable using its own value.",
    ">int num = 10;",
    ">num += 5;   // num is now 15",
    ">num *= 2;   // num is now 30",
    "#Casting",
    "~A cast converts a value to another type. Casting happens BEFORE the maths around it.",
    "-(float)7 / 2 is 3.5, because 7 becomes 7.0 before dividing.",
    "-(int)3.9 is 3. Casting to int truncates (cuts off) the decimals. It does NOT round.",
};
static const char *notes2[] = {
    "#if / else if / else",
    "~Only the FIRST branch whose condition is true runs. The rest are skipped.",
    ">if (score >= 80) printf(\"B\");",
    ">else if (score >= 70) printf(\"C\");",
    ">else printf(\"F\");",
    "~With score = 75 this prints C: it fails >= 80 but passes >= 70.",
    "#for loop",
    "~A for loop has three parts: start; keep going while; step.",
    ">for (int i = 1; i <= 5; i++) {",
    ">    printf(\"%d \", i);",
    ">}",
    "#while and do-while",
    "-while checks the condition BEFORE each lap, so it may run zero times.",
    "-do-while runs the body first and checks AFTER, so it always runs at least once.",
    ">do {",
    ">    n--;",
    ">} while (n > 0);",
    "#break and continue",
    "-break leaves the nearest loop (or switch) immediately.",
    "-continue skips the rest of this lap and jumps to the next iteration.",
    ">for (int i = 1; i <= 5; i++) {",
    ">    if (i % 2 == 0) continue;  // skip evens",
    ">    printf(\"%d \", i);          // 1 3 5",
    ">}",
};
static const char *notes3[] = {
    "#Arrays",
    "~An array is a fixed-size row of values that all have the same type.",
    ">int numbers[5] = {10, 20, 30, 40, 50};",
    "#Indexes start at 0",
    "~The first element is numbers[0]. The third is numbers[2], which is 30. The last of five is numbers[4]. Going past the end is undefined behaviour, so be careful!",
    "#How many elements?",
    "~sizeof gives bytes. Divide the whole array by one element to count the elements.",
    ">int count = sizeof(values) / sizeof(values[0]);",
    "#Strings are char arrays",
    "~A C string is an array of chars that ends with the null terminator '\\0'. There is no stored length. printf and strlen simply stop when they meet '\\0'.",
    ">char name[20] = \"Alice\";",
    "~Counting the length by hand:",
    ">int len = 0;",
    ">while (name[len] != '\\0') len++;   // len is 5",
    "-Always leave room for the extra '\\0' character.",
};
static const char *notes4[] = {
    "#Functions",
    "~A function packages code you can reuse. It has a return type, a name, parameters in brackets and a body.",
    ">int square(int x) {",
    ">    return x * x;",
    ">}",
    "#return",
    "~return sends a value back to the caller and ends the function straight away. The returned value must match the return type.",
    "#Arrays as parameters",
    "~A function cannot know how long an array is, so pass the size as a second parameter.",
    ">int sumArray(int arr[], int size)",
    "#Useful patterns",
    "-Even test: return n % 2 == 0; (this gives 1 for even and 0 for odd).",
    "-Running total: start with int sum = 0, add each element inside a for loop, then return sum.",
    ">int sum = 0;",
    ">for (int i = 0; i < size; i++) sum += arr[i];",
    ">return sum;",
    "#How the Function Lab works",
    "~Your code is compiled with gcc and tested against several inputs. If every test prints PASS you win the question.",
};
static const char **noteLines[LEVEL_COUNT] = { notes0, notes1, notes2, notes3, notes4 };
static const int noteCounts[LEVEL_COUNT] = {
    (int)(sizeof notes0 / sizeof notes0[0]), (int)(sizeof notes1 / sizeof notes1[0]), (int)(sizeof notes2 / sizeof notes2[0]),
    (int)(sizeof notes3 / sizeof notes3[0]), (int)(sizeof notes4 / sizeof notes4[0]),
};

typedef struct { const char *front, *back; } Flashcard;
static const Flashcard cards0[] = {
    { "variable", "A named place in memory that stores a value of a certain type." },
    { "int", "The type for whole numbers, like 25 or -3." },
    { "char", "The type for ONE character, written in single quotes: 'A'." },
    { "%d", "The printf format specifier that prints an int." },
    { "%.2f", "Prints a float or double with exactly 2 digits after the decimal point." },
    { "%c  and  %s", "%c prints one char. %s prints a whole string." },
    { "uninitialised variable", "A local variable with no starting value. It holds leftover garbage from memory, NOT 0." },
    { "const", "A keyword that makes a variable read-only once it has been set." },
};
static const Flashcard cards1[] = {
    { "integer division", "int / int throws away the fraction. 10 / 3 is 3." },
    { "% (remainder)", "What is left after dividing. 10 % 3 is 1." },
    { "cast", "Converts a value to another type, e.g. (float)7. It happens before the surrounding maths." },
    { "(float)7 / 2", "3.5 - the 7 becomes a float first, so the division keeps its decimals." },
    { "(int)3.9", "3 - casting to int truncates (cuts off) the decimals. It does not round." },
    { "+=  and  *=", "Compound assignment. num += 5 means num = num + 5." },
    { "even number test", "n % 2 == 0 is true when n is even." },
    { "truncation", "Cutting off the decimal part of a number without rounding." },
};
static const Flashcard cards2[] = {
    { "if / else if / else", "Only the first branch with a true condition runs. The others are skipped." },
    { "for loop", "A loop with three parts: start; keep going while; step. Great for counting." },
    { "while loop", "Checks its condition before every lap, so it may never run." },
    { "do-while loop", "Runs its body first and checks the condition afterwards: always at least once." },
    { "break", "Leaves the nearest loop (or switch) immediately." },
    { "continue", "Skips the rest of this lap and jumps to the next iteration." },
    { "== vs =", "== compares two values. = assigns a value. Mixing them up is a classic bug." },
    { "i++", "Adds 1 to i. Often used as the step in a for loop." },
};
static const Flashcard cards3[] = {
    { "array", "A fixed-size row of values that all have the same type." },
    { "index", "The position of an element. In C the first index is 0." },
    { "numbers[2]", "The THIRD element, because counting starts at 0." },
    { "'\\0'  (null terminator)", "The character that ends every C string. printf and strlen stop there." },
    { "string in C", "A char array that ends with '\\0'. There is no separate length stored." },
    { "sizeof", "Gives the size in bytes of a variable or type." },
    { "element count", "sizeof(values) / sizeof(values[0]) - total bytes divided by bytes per element." },
    { "out of bounds", "Reading or writing past the end of an array. Undefined behaviour!" },
};
static const Flashcard cards4[] = {
    { "function", "Reusable code with a return type, a name, parameters and a body." },
    { "return type", "The type of value a function sends back, written before its name. int square(...)" },
    { "parameter", "A variable in the function's brackets that receives a value from the caller." },
    { "return", "Sends a value back to the caller and ends the function immediately." },
    { "void", "Return type meaning the function gives nothing back." },
    { "array + size", "A function cannot know an array's length, so pass the size as another parameter." },
    { "running total", "Start with sum = 0, add each element in a loop, then return sum." },
    { "isEven(n)", "return n % 2 == 0; gives 1 for even numbers and 0 for odd ones." },
};
static const Flashcard *cardSets[LEVEL_COUNT] = { cards0, cards1, cards2, cards3, cards4 };
static const int cardCounts[LEVEL_COUNT] = {
    (int)(sizeof cards0 / sizeof cards0[0]), (int)(sizeof cards1 / sizeof cards1[0]), (int)(sizeof cards2 / sizeof cards2[0]),
    (int)(sizeof cards3 / sizeof cards3[0]), (int)(sizeof cards4 / sizeof cards4[0]),
};

// Boosters earned at HP milestones.
typedef enum { BOOST_FIFTY, BOOST_SHIELD, BOOST_DOUBLE, BOOST_COUNT } BoostId;
static const char *boostShort[BOOST_COUNT] = { "50/50", "SHIELD", "2x HP" };
static const char *boostLong[BOOST_COUNT] = {
    "50/50: removes two wrong answers (MCQ) or shows the first letter and length (typing).",
    "SHIELD: your next wrong answer is forgiven. No mistake is recorded.",
    "2x HP: everything you earn for the rest of this level is doubled.",
};
#define MILESTONE_COUNT 7
static const int milestoneHp[MILESTONE_COUNT] = { 40, 100, 180, 280, 400, 550, 750 };
static const int milestoneReward[MILESTONE_COUNT][BOOST_COUNT] = {
    { 1, 0, 0 }, { 0, 1, 0 }, { 0, 0, 1 }, { 2, 0, 0 }, { 0, 1, 1 }, { 0, 2, 0 }, { 1, 1, 1 },
};
#define RANK_COUNT 6
static const int rankHp[RANK_COUNT] = { 0, 60, 150, 300, 500, 800 };
static const char *rankNames[RANK_COUNT] = { "Rookie Coder", "Script Kid", "Byte Runner", "Netrunner", "Kernel Knight", "Root Admin" };

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
        case 0: return COLOR_CYAN;   case 1: return COLOR_GREEN;  case 2: return COLOR_YELLOW;
        case 3: return COLOR_ORANGE; case 4: return COLOR_PURPLE; default: return COLOR_PINK;
    }
}
static Color LevelColor(int level) {
    switch (level) { case 0: return COLOR_CYAN; case 1: return COLOR_GREEN; case 2: return COLOR_YELLOW; case 3: return COLOR_ORANGE; default: return COLOR_PURPLE; }
}
static const Question *QuestionAt(int bankIndex) { return &questionBank[bankIndex]; }

// ================================================================ audio
// Every sound effect and the background loop are synthesised in code when the app starts,
// so there are no asset files to ship. If no audio device exists the app just stays silent.
#define SAMPLE_RATE 22050
typedef enum { FX_CLICK, FX_CORRECT, FX_WRONG, FX_UNLOCK, FX_FANFARE, FX_FAIL, FX_HOP, FX_WHOOSH, FX_POP, FX_STAR, FX_TICK, FX_BLIP, FX_COUNT } FxId;
static Sound fxSounds[FX_COUNT];
static Music musicStream;
static unsigned char *musicFile;
static bool audioReady, musicReady, soundOn = true;
static float musicLevel;

static float *SynthNew(float seconds, int *count) { *count = (int)(seconds * SAMPLE_RATE); return (float *)calloc((size_t)*count, sizeof(float)); }

static float SynthOsc(int shape, float cycles) {
    float p = cycles - floorf(cycles);
    switch (shape) {
        case 0:  return sinf(2 * PI * p);
        case 1:  return p < 0.5f ? 1.0f : -1.0f;
        case 2:  return 2 * p - 1;
        default: return 4 * fabsf(p - 0.5f) - 1;   // triangle
    }
}

// Adds one note. Positions wrap round the buffer so looping tails stay seamless.
static void SynthTone(float *buf, int count, float start, float dur, float f0, float f1, int shape, float vol, float attack, float release, float decay) {
    int first = (int)(start * SAMPLE_RATE), length = (int)(dur * SAMPLE_RATE);
    float phase = 0;
    for (int i = 0; i < length; i++) {
        float t = (float)i / SAMPLE_RATE;
        phase += (f0 + (f1 - f0) * ((float)i / (float)length)) / SAMPLE_RATE;
        float env = (attack > 0 && t < attack ? t / attack : 1.0f) * (release > 0 && dur - t < release ? (dur - t) / release : 1.0f) * expf(-decay * t);
        buf[(first + i) % count] += SynthOsc(shape, phase) * vol * env;
    }
}

static unsigned int synthSeed = 12345u;
static float SynthRand(void) { synthSeed = synthSeed * 1664525u + 1013904223u; return ((synthSeed >> 8) & 0xFFFF) / 32768.0f - 1.0f; }

static void SynthNoise(float *buf, int count, float start, float dur, float vol, float decay, bool highpass) {
    int first = (int)(start * SAMPLE_RATE), length = (int)(dur * SAMPLE_RATE);
    float z = 0;
    for (int i = 0; i < length; i++) {
        float n = SynthRand(), t = (float)i / SAMPLE_RATE;
        z += 0.35f * (n - z);
        buf[(first + i) % count] += (highpass ? n - z : z) * vol * expf(-decay * t);
    }
}

// Rising "swoosh": noise whose brightness sweeps up while its volume swells and fades.
static void SynthWhoosh(float *buf, int count, float dur, float vol) {
    int length = (int)(dur * SAMPLE_RATE);
    float z = 0;
    for (int i = 0; i < length && i < count; i++) {
        float f = (float)i / (float)length, s = sinf(PI * f);
        z += (0.04f + 0.5f * f) * (SynthRand() - z);
        buf[i] += z * vol * s * s;
    }
}

static void SynthLowpass(float *buf, int count, float alpha) {
    float z = 0;
    for (int i = 0; i < count; i++) { z += alpha * (buf[i] - z); buf[i] = z; }
}

static void SynthEcho(float *buf, int count, float delay, float gain) {
    int d = (int)(delay * SAMPLE_RATE);
    for (int i = count - 1; i >= d; i--) buf[i] += buf[i - d] * gain;
}

static void SynthNormalize(float *buf, int count, float peak) {
    float m = 0;
    for (int i = 0; i < count; i++) if (fabsf(buf[i]) > m) m = fabsf(buf[i]);
    if (m > 0) for (int i = 0; i < count; i++) buf[i] *= peak / m;
}

static float *SynthFx(FxId id, int *count) {
    float *b = NULL;
    float peak = 0.5f;
    switch (id) {
        case FX_CLICK:
            b = SynthNew(0.12f, count);
            SynthTone(b, *count, 0, 0.06f, 1100, 1500, 1, 0.6f, 0, 0.01f, 40);
            peak = 0.26f;
            break;
        case FX_CORRECT: {
            static const float notes[4] = { 1046.5f, 1318.5f, 1568.0f, 2093.0f };
            b = SynthNew(0.9f, count);
            for (int i = 0; i < 4; i++) {
                SynthTone(b, *count, i * 0.07f, 0.35f, notes[i], notes[i], 3, 0.6f, 0.003f, 0.05f, 9);
                SynthTone(b, *count, i * 0.07f, 0.35f, notes[i] * 2, notes[i] * 2, 0, 0.15f, 0.003f, 0.05f, 12);
            }
            peak = 0.5f;
        } break;
        case FX_WRONG:
            b = SynthNew(0.7f, count);
            SynthTone(b, *count, 0, 0.22f, 196, 150, 2, 0.6f, 0.005f, 0.05f, 5);
            SynthTone(b, *count, 0.13f, 0.35f, 147, 104, 2, 0.6f, 0.005f, 0.1f, 4);
            SynthLowpass(b, *count, 0.18f);
            peak = 0.5f;
            break;
        case FX_UNLOCK: {
            static const float notes[6] = { 523.25f, 659.25f, 783.99f, 1046.5f, 1318.5f, 1568.0f };
            b = SynthNew(2.0f, count);
            for (int i = 0; i < 6; i++) {
                SynthTone(b, *count, i * 0.085f, 0.5f, notes[i], notes[i], 3, 0.5f, 0.004f, 0.1f, 6);
                SynthTone(b, *count, i * 0.085f, 0.5f, notes[i] * 2, notes[i] * 2, 0, 0.12f, 0.004f, 0.1f, 8);
            }
            SynthTone(b, *count, 0.55f, 1.0f, 1046.5f, 1046.5f, 3, 0.25f, 0.01f, 0.6f, 1.5f);
            SynthTone(b, *count, 0.55f, 1.0f, 1318.5f, 1318.5f, 3, 0.2f, 0.01f, 0.6f, 1.5f);
            SynthTone(b, *count, 0.55f, 1.0f, 1568.0f, 1568.0f, 3, 0.2f, 0.01f, 0.6f, 1.5f);
            SynthEcho(b, *count, 0.2f, 0.35f);
            peak = 0.5f;
        } break;
        case FX_FANFARE: {
            static const float notes[4] = { 783.99f, 1046.5f, 1318.5f, 1568.0f };
            static const float chord[4] = { 523.25f, 659.25f, 783.99f, 1046.5f };
            b = SynthNew(2.1f, count);
            for (int i = 0; i < 4; i++) {
                SynthTone(b, *count, i * 0.11f, 0.3f, notes[i], notes[i], 1, 0.22f, 0.003f, 0.05f, 7);
                SynthTone(b, *count, i * 0.11f, 0.3f, notes[i], notes[i], 3, 0.4f, 0.003f, 0.05f, 7);
            }
            for (int i = 0; i < 4; i++) SynthTone(b, *count, 0.5f, 1.2f, chord[i] * 2, chord[i] * 2, 3, 0.2f, 0.01f, 0.6f, 1.2f);
            SynthEcho(b, *count, 0.22f, 0.3f);
            peak = 0.5f;
        } break;
        case FX_FAIL: {
            static const float notes[3] = { 440.0f, 349.23f, 293.66f };
            b = SynthNew(1.4f, count);
            for (int i = 0; i < 3; i++) SynthTone(b, *count, i * 0.24f, 0.5f, notes[i], notes[i] * 0.97f, 3, 0.6f, 0.01f, 0.2f, 3);
            SynthTone(b, *count, 0.72f, 0.6f, 220, 190, 2, 0.3f, 0.01f, 0.3f, 3);
            SynthLowpass(b, *count, 0.35f);
            peak = 0.45f;
        } break;
        case FX_HOP:
            b = SynthNew(0.2f, count);
            SynthTone(b, *count, 0, 0.12f, 280, 640, 0, 0.8f, 0.003f, 0.02f, 14);
            peak = 0.38f;
            break;
        case FX_WHOOSH:
            b = SynthNew(0.5f, count);
            SynthWhoosh(b, *count, 0.45f, 1.0f);
            peak = 0.22f;
            break;
        case FX_POP:
            b = SynthNew(0.25f, count);
            SynthTone(b, *count, 0, 0.1f, 420, 980, 0, 0.8f, 0.002f, 0.02f, 12);
            peak = 0.38f;
            break;
        case FX_STAR:
            b = SynthNew(0.7f, count);
            SynthTone(b, *count, 0, 0.55f, 1318.5f, 1318.5f, 0, 0.6f, 0.003f, 0.1f, 7);
            SynthTone(b, *count, 0, 0.55f, 2637.0f, 2637.0f, 0, 0.25f, 0.003f, 0.1f, 10);
            peak = 0.42f;
            break;
        case FX_TICK:
            b = SynthNew(0.05f, count);
            SynthTone(b, *count, 0, 0.02f, 2000, 2000, 1, 0.6f, 0, 0.005f, 100);
            peak = 0.16f;
            break;
        default:
            b = SynthNew(0.15f, count);
            SynthTone(b, *count, 0, 0.08f, 1800, 2300, 0, 0.8f, 0.002f, 0.01f, 25);
            peak = 0.32f;
            break;
    }
    SynthNormalize(b, *count, peak);
    return b;
}

// Calm, techy loop: soft pad chords (Am - F - C - G), a plucked arpeggio with echo,
// a sparse bell melody, a deep bass and a very light kick and hi-hat. 8 bars at 100 BPM.
static float *SynthMusic(int *count) {
    const float beat = 0.6f;
    const int bars = 8;
    static const float chords[4][3] = { { 220.0f, 261.63f, 329.63f }, { 174.61f, 220.0f, 261.63f }, { 261.63f, 329.63f, 392.0f }, { 196.0f, 246.94f, 293.66f } };
    static const float bass[4] = { 110.0f, 87.31f, 130.81f, 98.0f };
    static const int arpPattern[8] = { 0, 1, 2, 1, 0, 2, 1, 2 };
    static const float pentatonic[5] = { 440.0f, 523.25f, 587.33f, 659.25f, 783.99f };
    static const int bells[12][2] = { { 6, 3 }, { 10, 4 }, { 14, 2 }, { 22, 1 }, { 26, 3 }, { 30, 2 }, { 38, 4 }, { 42, 3 }, { 46, 0 }, { 54, 2 }, { 58, 3 }, { 62, 1 } };
    float *mix = SynthNew(bars * 4 * beat, count);
    int n = *count;
    float *arp = (float *)calloc((size_t)n, sizeof(float));
    for (int c = 0; c < 4; c++) {
        float start = c * 8 * beat;
        for (int k = 0; k < 3; k++) {
            SynthTone(mix, n, start, 8 * beat + 0.8f, chords[c][k], chords[c][k], 3, 0.055f, 0.7f, 0.8f, 0);
            SynthTone(mix, n, start, 8 * beat + 0.8f, chords[c][k] * 1.004f, chords[c][k] * 1.004f, 0, 0.035f, 0.7f, 0.8f, 0);
        }
        SynthTone(mix, n, start, 8 * beat + 0.8f, bass[c], bass[c], 0, 0.07f, 0.7f, 0.8f, 0);
    }
    for (int bar = 0; bar < bars; bar++) {
        int c = bar / 2;
        for (int hit = 0; hit < 2; hit++) SynthTone(mix, n, (bar * 4 + hit * 2) * beat, 0.6f, bass[c], bass[c], 0, 0.22f, 0.01f, 0.25f, 3.5f);
        SynthTone(mix, n, bar * 4 * beat, 0.22f, 130, 45, 0, 0.16f, 0, 0.06f, 12);
    }
    for (int step = 1; step < bars * 8; step += 2) SynthNoise(mix, n, step * beat / 2, 0.05f, 0.05f, 70, true);
    for (int step = 0; step < bars * 8; step++) {
        float f = chords[step / 16][arpPattern[step % 8]] * 2;
        SynthTone(arp, n, step * beat / 2, 0.4f, f, f, 3, 0.09f, 0.004f, 0.05f, 6);
        SynthTone(arp, n, step * beat / 2, 0.4f, f, f, 1, 0.025f, 0.004f, 0.05f, 9);
    }
    for (int i = 0; i < 12; i++) {
        float f = pentatonic[bells[i][1]], start = bells[i][0] * beat / 2;
        SynthTone(arp, n, start, 1.4f, f, f, 0, 0.09f, 0.004f, 0.3f, 2.6f);
        SynthTone(arp, n, start, 1.4f, f * 2, f * 2, 0, 0.03f, 0.004f, 0.3f, 4.0f);
    }
    int d = (int)(0.45f * SAMPLE_RATE);
    for (int i = 0; i < n; i++) {
        mix[i] += arp[i];
        mix[(i + d) % n] += arp[i] * 0.38f;
        mix[(i + 2 * d) % n] += arp[i] * 0.18f;
    }
    free(arp);
    SynthNormalize(mix, n, 0.85f);
    return mix;
}

static unsigned char *SynthWavFile(const float *buf, int count, int *fileSize) {
    int dataSize = count * 2;
    *fileSize = 44 + dataSize;
    unsigned char *file = (unsigned char *)malloc((size_t)*fileSize);
    unsigned int values[10] = { 0x46464952u, (unsigned int)(36 + dataSize), 0x45564157u, 0x20746d66u, 16u, 0x00010001u, SAMPLE_RATE, SAMPLE_RATE * 2u, 0x00100002u, 0x61746164u };
    memcpy(file, values, 40);
    memcpy(file + 40, &dataSize, 4);
    short *samples = (short *)(file + 44);
    for (int i = 0; i < count; i++) samples[i] = (short)(fmaxf(-1.0f, fminf(1.0f, buf[i])) * 32767.0f);
    return file;
}

static void InitAudio(void) {
    InitAudioDevice();
    audioReady = IsAudioDeviceReady();
    if (!audioReady) return;
    for (int id = 0; id < FX_COUNT; id++) {
        int count;
        float *buf = SynthFx((FxId)id, &count);
        Wave wave = { 0 };
        wave.frameCount = (unsigned int)count;
        wave.sampleRate = SAMPLE_RATE;
        wave.sampleSize = 16;
        wave.channels = 1;
        wave.data = MemAlloc((unsigned int)count * 2);
        short *samples = (short *)wave.data;
        for (int i = 0; i < count; i++) samples[i] = (short)(fmaxf(-1.0f, fminf(1.0f, buf[i])) * 32767.0f);
        fxSounds[id] = LoadSoundFromWave(wave);
        UnloadWave(wave);
        free(buf);
        SetSoundVolume(fxSounds[id], 0.9f);
    }
    int count, size;
    float *music = SynthMusic(&count);
    musicFile = SynthWavFile(music, count, &size);
    free(music);
    musicStream = LoadMusicStreamFromMemory(".wav", musicFile, size);
    musicReady = IsMusicValid(musicStream);
    if (musicReady) { musicStream.looping = true; SetMusicVolume(musicStream, 0); PlayMusicStream(musicStream); }
}

static void ShutdownAudio(void) {
    if (!audioReady) return;
    if (musicReady) UnloadMusicStream(musicStream);
    for (int id = 0; id < FX_COUNT; id++) UnloadSound(fxSounds[id]);
    free(musicFile);
    CloseAudioDevice();
}

static void PlayFx(FxId id) { if (audioReady && soundOn) PlaySound(fxSounds[id]); }

// Music is loudest when you are idle (title, modes, map, reports) and fades away while answering.
static void UpdateAudio(float dt, bool quiet) {
    if (!audioReady || !musicReady) return;
    float target = (soundOn && !quiet) ? 1.0f : 0.0f;
    musicLevel += (target - musicLevel) * (dt * 2.5f > 1 ? 1 : dt * 2.5f);
    SetMusicVolume(musicStream, musicLevel * 0.45f);
    UpdateMusicStream(musicStream);
}

static void SetScreen(ScreenState screen) { app.screen = screen; app.screenTime = 0; PlayFx(FX_WHOOSH); }
static void ShowToast(const char *text) { snprintf(app.toast, sizeof app.toast, "%s", text); app.toastTime = 2.4f; }

// A click is "used up" by the first widget that takes it, so overlapping widgets never both fire.
static bool TakeClick(bool hovered) {
    if (hovered) wantHand = true;
    if (hovered && !clickConsumed && IsMouseButtonReleased(MOUSE_BUTTON_LEFT)) { clickConsumed = true; PlayFx(FX_CLICK); return true; }
    return false;
}
static bool HoverRect(Rectangle r) { return !uiLocked && CheckCollisionPointRec(GetMousePosition(), r); }

// ================================================================ text
// Two plain, easy-to-read fonts (system fonts) are used for questions, answers and study notes.
// Everything else keeps the original retro font. If no system font is found we fall back to the retro font.
static Font readFont, codeFont;
static bool readFontOk, codeFontOk;
static int textMode;   // 0 = retro font, 1 = plain reading font, 2 = plain monospace font (used by WrapText / DrawCodeBlock / TextW / TextDraw)

static bool TryLoadFont(const char *const *paths, Font *font) {
    for (; *paths; paths++) {
        if (!FileExists(*paths)) continue;
        Font f = LoadFontEx(*paths, 40, NULL, 0);
        if (f.texture.id > 0 && f.glyphCount > 0) { SetTextureFilter(f.texture, TEXTURE_FILTER_BILINEAR); *font = f; return true; }
    }
    return false;
}
static void LoadReadingFonts(void) {
    static const char *const sans[] = {
        "C:/Windows/Fonts/segoeui.ttf", "C:/Windows/Fonts/arial.ttf", "C:/Windows/Fonts/verdana.ttf", "C:/Windows/Fonts/calibri.ttf",
        "/System/Library/Fonts/Supplemental/Arial.ttf", "/Library/Fonts/Arial.ttf",
        "/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf", "/usr/share/fonts/truetype/liberation/LiberationSans-Regular.ttf",
        "/usr/share/fonts/TTF/DejaVuSans.ttf", "/usr/share/fonts/dejavu/DejaVuSans.ttf", NULL };
    static const char *const mono[] = {
        "C:/Windows/Fonts/consola.ttf", "C:/Windows/Fonts/cour.ttf", "/System/Library/Fonts/Supplemental/Courier New.ttf",
        "/usr/share/fonts/truetype/dejavu/DejaVuSansMono.ttf", "/usr/share/fonts/truetype/liberation/LiberationMono-Regular.ttf",
        "/usr/share/fonts/TTF/DejaVuSansMono.ttf", "/usr/share/fonts/dejavu/DejaVuSansMono.ttf", NULL };
    readFontOk = TryLoadFont(sans, &readFont);
    codeFontOk = TryLoadFont(mono, &codeFont);
}
static int TextW(const char *text, int size) {
    if (textMode == 1 && readFontOk) return (int)(MeasureTextEx(readFont, text, (float)size, 0.3f).x + 0.5f);
    if (textMode == 2 && codeFontOk) return (int)(MeasureTextEx(codeFont, text, (float)size, 0.0f).x + 0.5f);
    return MeasureText(text, size);
}
static void TextDraw(const char *text, int x, int y, int size, Color color) {
    if (textMode == 1 && readFontOk) DrawTextEx(readFont, text, V2((float)x, (float)y - 1), (float)size, 0.3f, color);
    else if (textMode == 2 && codeFontOk) DrawTextEx(codeFont, text, V2((float)x, (float)y - 1), (float)size, 0.0f, color);
    else DrawText(text, x, y, size, color);
}

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
        if (lineBuffer[0] && TextW(trial, fontSize) > maxWidth) {
            if (draw) TextDraw(lineBuffer, x, y, fontSize, color);
            y += lineHeight;
            snprintf(lineBuffer, sizeof lineBuffer, "%s", word);
        } else {
            snprintf(lineBuffer, sizeof lineBuffer, "%s", trial);
        }
        if (*cursor == '\n') {
            if (draw) TextDraw(lineBuffer, x, y, fontSize, color);
            y += lineHeight;
            lineBuffer[0] = '\0';
            cursor++;
        } else if (*cursor == ' ') {
            cursor++;
        }
    }
    if (lineBuffer[0]) { if (draw) TextDraw(lineBuffer, x, y, fontSize, color); y += lineHeight; }
    return y;
}

static int CountLines(const char *text) {
    int lines = 1;
    for (; *text; text++) if (*text == '\n') lines++;
    return lines;
}

static void DrawCodeBlock(const char *code, int x, int y, int fontSize, Color color) {
    int savedMode = textMode;
    textMode = 2;
    const char *cursor = code;
    char line[160];
    for (;;) {
        int length = 0;
        while (*cursor && *cursor != '\n' && length < 159) line[length++] = *cursor++;
        line[length] = '\0';
        TextDraw(line, x, y, fontSize, color);
        y += fontSize + 6;
        while (*cursor && *cursor != '\n') cursor++;
        if (*cursor == '\n') cursor++; else break;
    }
    textMode = savedMode;
}

static void FitText(const char *source, char *dest, int capacity, int maxWidth, int fontSize) {
    snprintf(dest, capacity, "%s", source);
    for (char *c = dest; *c; c++) if (*c == '\n') *c = ' ';
    if (TextW(dest, fontSize) <= maxWidth) return;
    int length = (int)strlen(dest);
    while (length > 3) {
        length--;
        dest[length] = '\0';
        char trial[200];
        snprintf(trial, sizeof trial, "%s...", dest);
        if (TextW(trial, fontSize) <= maxWidth) { snprintf(dest, capacity, "%s", trial); return; }
    }
}

// ================================================================ shape helpers
// raylib only draws triangles whose corners run counter-clockwise, so fix the order for the caller.
static void DrawTriSafe(Vector2 a, Vector2 b, Vector2 c, Color col) {
    float cross = (b.x - a.x) * (c.y - a.y) - (b.y - a.y) * (c.x - a.x);
    if (cross > 0) DrawTriangle(a, c, b, col); else DrawTriangle(a, b, c, col);
}

static void DrawNgon(Vector2 c, int sides, float r, float rotDeg, Color col) {
    for (int i = 0; i < sides; i++) {
        float a0 = (rotDeg + 360.0f * i / sides) * DEG2RAD, a1 = (rotDeg + 360.0f * (i + 1) / sides) * DEG2RAD;
        DrawTriSafe(c, V2(c.x + cosf(a0) * r, c.y + sinf(a0) * r), V2(c.x + cosf(a1) * r, c.y + sinf(a1) * r), col);
    }
}
static void DrawNgonLines(Vector2 c, int sides, float r, float rotDeg, float thick, Color col) {
    for (int i = 0; i < sides; i++) {
        float a0 = (rotDeg + 360.0f * i / sides) * DEG2RAD, a1 = (rotDeg + 360.0f * (i + 1) / sides) * DEG2RAD;
        DrawLineEx(V2(c.x + cosf(a0) * r, c.y + sinf(a0) * r), V2(c.x + cosf(a1) * r, c.y + sinf(a1) * r), thick, col);
    }
}

// Cut-corner (chamfered) rectangle: the "retro terminal" panel shape used everywhere.
static void DrawChamfer(Rectangle r, float cut, Color c) {
    float m = r.width < r.height ? r.width : r.height;
    if (cut > m / 2) cut = m / 2;
    if (cut < 0) cut = 0;
    DrawRectangleRec((Rectangle){ r.x + cut, r.y, r.width - 2 * cut, r.height }, c);
    DrawRectangleRec((Rectangle){ r.x, r.y + cut, cut, r.height - 2 * cut }, c);
    DrawRectangleRec((Rectangle){ r.x + r.width - cut, r.y + cut, cut, r.height - 2 * cut }, c);
    if (cut <= 0) return;
    float x2 = r.x + r.width, y2 = r.y + r.height;
    DrawTriSafe(V2(r.x + cut, r.y), V2(r.x, r.y + cut), V2(r.x + cut, r.y + cut), c);
    DrawTriSafe(V2(x2 - cut, r.y), V2(x2, r.y + cut), V2(x2 - cut, r.y + cut), c);
    DrawTriSafe(V2(r.x + cut, y2), V2(r.x, y2 - cut), V2(r.x + cut, y2 - cut), c);
    DrawTriSafe(V2(x2 - cut, y2), V2(x2, y2 - cut), V2(x2 - cut, y2 - cut), c);
}
static void DrawRound(Rectangle r, float radius, Color c) { DrawChamfer(r, radius * 0.5f, c); }
// Dark panels get a glowing neon edge; bright shapes keep a dark outline.
static void DrawRoundOutlined(Rectangle r, float radius, float border, Color fill, Color outline) {
    bool inkOutline = outline.r == COLOR_INK.r && outline.g == COLOR_INK.g && outline.b == COLOR_INK.b;
    bool darkFill = (fill.r * 3 + fill.g * 6 + fill.b) / 10 < 90;
    Color edge = outline;
    if (inkOutline && darkFill) {
        edge = COLOR_NEON;
        DrawRound(Expand(r, border + 4), radius + 4, Fade(edge, 0.035f));
        DrawRound(Expand(r, border + 2), radius + 2, Fade(edge, 0.07f));
    }
    DrawRound(Expand(r, border), radius + border, edge);
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
        DrawStarShape(c, r, 0, (Color){52, 58, 104, 255});
        return;
    }
    DrawStarShape(c, r * 1.22f, 0, COLOR_INK);
    DrawStarShape(c, r, 0, COLOR_YELLOW);
    DrawStarShape(c, r * 0.62f, 0, (Color){214, 236, 255, 255});
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

// Floating holo data-strips.
static void DrawCloud(Vector2 c, float s, Color col) {
    Color neon = Fade(COLOR_CYAN, col.a / 255.0f * 0.75f);
    float widths[3] = { 96, 62, 80 };
    for (int i = 0; i < 3; i++) {
        float x = c.x - widths[i] * s / 2 + (i == 1 ? 14 * s : 0), y = c.y + (i - 1) * 13 * s;
        DrawRectangleRec((Rectangle){ x, y, widths[i] * s, 4 * s }, neon);
        DrawRectangleRec((Rectangle){ x - 12 * s, y, 6 * s, 4 * s }, neon);
        DrawRectangleRec((Rectangle){ x + widths[i] * s + 8 * s, y, 3 * s, 4 * s }, neon);
    }
}

// Neon beacon pylon: a pole topped with a pulsing hexagon core.
static void DrawLollipop(Vector2 c, float r, Color a, Color b, float tilt) {
    (void)tilt; (void)b;
    float t = (float)GetTime();
    Vector2 end = V2(c.x, c.y + r * 3.2f);
    DrawLineEx(c, end, 8, COLOR_INK);
    DrawLineEx(c, end, 4, ColorBrightness(a, -0.5f));
    DrawRectangle((int)(c.x - 10), (int)(end.y - 4), 20, 6, ColorBrightness(a, -0.6f));
    DrawNgon(c, 6, r * 1.45f, 0, Fade(a, 0.14f + 0.1f * sinf(t * 3 + r)));
    DrawNgon(c, 6, r + 3, 0, COLOR_INK);
    DrawNgon(c, 6, r, 0, a);
    DrawNgon(c, 6, r * 0.66f, 0, COLOR_INK);
    DrawNgon(c, 6, r * 0.4f, 0, Fade(a, 0.55f + 0.45f * sinf(t * 4 + r)));
}

// Spinning data cube.
static void DrawWrappedCandy(Vector2 c, float r, Color col, float rot) {
    float deg = rot * RAD2DEG;
    DrawNgon(c, 4, r * 1.9f, deg, Fade(col, 0.15f));
    DrawNgon(c, 4, r * 1.5f + 3, deg, COLOR_INK);
    DrawNgon(c, 4, r * 1.5f, deg, col);
    DrawNgon(c, 4, r * 1.05f, deg, COLOR_INK);
    DrawNgon(c, 4, r * 0.45f, deg + 45, col);
}

// Energy cell: a glowing tube.
static void DrawBean(Vector2 c, float length, float thick, Color col, float rot) {
    Vector2 dir = V2(cosf(rot), sinf(rot));
    Vector2 a = V2(c.x - dir.x * length / 2, c.y - dir.y * length / 2), b = V2(c.x + dir.x * length / 2, c.y + dir.y * length / 2);
    DrawLineEx(a, b, thick + 12, Fade(col, 0.16f));
    DrawLineEx(a, b, thick + 5, COLOR_INK);
    DrawLineEx(a, b, thick * 0.55f, col);
    DrawLineEx(a, b, thick * 0.18f, Fade(WHITE, 0.8f));
}

// Blinking server tower. (base = bottom centre)
static void DrawCupcake(Vector2 base, float s) {
    float t = (float)GetTime();
    Rectangle box = { base.x - 22 * s, base.y - 78 * s, 44 * s, 78 * s };
    DrawRoundOutlined(box, 8 * s, 2.5f * s, COLOR_CODE_BG, COLOR_INK);
    for (int i = 0; i < 4; i++) {
        float y = box.y + 9 * s + i * 17 * s;
        DrawRectangleRec((Rectangle){ box.x + 6 * s, y, 32 * s, 11 * s }, (Color){24, 28, 70, 255});
        bool on = ((int)(t * 3 + i * 2) % 3) != 0;
        DrawRectangleRec((Rectangle){ box.x + 9 * s, y + 3 * s, 5 * s, 5 * s }, on ? CandyColor(i + 1) : (Color){40, 44, 90, 255});
        DrawRectangleRec((Rectangle){ box.x + 18 * s, y + 4 * s, 16 * s, 3 * s }, Fade(COLOR_CYAN, 0.5f));
    }
}



// ================================================================ background and header
static unsigned int Hash2(int a, int b) {
    unsigned int h = ((unsigned int)a * 73856093u) ^ ((unsigned int)b * 19349663u);
    h ^= h >> 13; h *= 0x5bd1e995u; h ^= h >> 15;
    return h;
}

// Retro-futuristic night city: sun, skyline, data rain and a scrolling neon grid.
static void DrawBackground(float wash) {
    float t = (float)GetTime();
    const int horizon = 392;
    DrawRectangleGradientV(0, 0, WINDOW_WIDTH, horizon + 10, COLOR_BG_TOP, COLOR_BG_BOT);
    for (int i = 0; i < SPARKLE_COUNT; i++) {
        float twinkle = 0.5f + 0.5f * sinf(t * 2.2f + i * 1.7f);
        if (sparklePos[i].y < horizon - 20) DrawRectangle((int)sparklePos[i].x, (int)sparklePos[i].y, 2 + (i % 2), 2 + (i % 2), Fade(WHITE, 0.2f + 0.6f * twinkle));
    }
    // striped sun
    for (int dy = 0; dy < 130; dy += 3) {
        int stripe = dy / 3;
        if (dy < 90 && stripe % 6 == 5) continue;   // stripe gaps in the lower sun
        float half = sqrtf(130.0f * 130.0f - (float)dy * dy);
        float f = dy / 130.0f;
        Color c = { (unsigned char)(150 - 90 * f), (unsigned char)(96 + 100 * f), (unsigned char)(232 - 20 * f), 255 };
        DrawRectangle((int)(500 - half), horizon - dy - 3, (int)(half * 2), 3, Fade(c, 0.42f));
    }
    // falling data rain
    static const char glyphs[] = "01{}();<>=+*#%&/\\";
    for (int i = 0; i < BG_CANDY_COUNT; i++) {
        float x = 12 + i * 52 + (Hash2(i, 1) % 20);
        float speed = 50 + (Hash2(i, 2) % 70);
        float head = fmodf(t * speed + (Hash2(i, 3) % 600), WINDOW_HEIGHT + 240) - 120;
        for (int k = 0; k < 9; k++) {
            float y = head - k * 17;
            if (y < -20 || y > horizon - 4) continue;
            char one[2] = { glyphs[(Hash2(i * 31 + k, (int)(t * 3)) % (sizeof glyphs - 1))], 0 };
            if (k > 0 && (int)(t * 3 + k * 5 + i) % 7 != 0) one[0] = glyphs[(Hash2(i, k) % (sizeof glyphs - 1))];
            DrawText(one, (int)x, (int)y, 16, k == 0 ? Fade(WHITE, 0.35f) : Fade(COLOR_CYAN, 0.13f * (1.0f - k / 9.0f)));
        }
    }
    // skyline
    for (int i = 0; i < 28; i++) {
        unsigned int h = Hash2(i, 7);
        int w = 30 + (int)(h % 26), height = 40 + (int)((h >> 5) % 100), x = i * 37 - 10;
        Rectangle b = { (float)x, (float)(horizon - height), (float)w, (float)height + 4 };
        DrawRectangleRec(b, (Color){10, 12, 38, 255});
        DrawRectangle(x, horizon - height, w, 2, Fade(COLOR_CYAN, 0.2f));
        if (h % 5 == 0) { DrawRectangle(x + w / 2, horizon - height - 16, 2, 16, (Color){10, 12, 38, 255}); DrawRectangle(x + w / 2 - 1, horizon - height - 18, 4, 4, ((int)(t * 2) % 2) ? Fade(COLOR_PURPLE, 0.8f) : Fade(COLOR_PURPLE, 0.2f)); }
        for (int wy = 8; wy < height - 4; wy += 9)
            for (int wx = 5; wx < w - 5; wx += 8) {
                unsigned int wh = Hash2(i * 100 + wy, wx + (int)(t * 0.4f * ((wy + wx) % 3 == 0)));
                if (wh % 4 == 0) DrawRectangle(x + wx, horizon - height + wy, 3, 4, wh % 3 == 0 ? Fade(COLOR_YELLOW, 0.5f) : Fade(COLOR_CYAN, 0.42f));
            }
    }
    // neon grid floor
    DrawRectangleGradientV(0, horizon, WINDOW_WIDTH, WINDOW_HEIGHT - horizon, (Color){34, 20, 84, 255}, (Color){6, 6, 28, 255});
    DrawRectangle(0, horizon - 1, WINDOW_WIDTH, 2, Fade(COLOR_CYAN, 0.7f));
    DrawRectangle(0, horizon + 1, WINDOW_WIDTH, 6, Fade(COLOR_CYAN, 0.08f));
    float phase = fmodf(t * 0.5f, 1.0f);
    for (int k = 0; k < 12; k++) {
        float z = (k + phase) / 12.0f;
        int y = horizon + (int)((WINDOW_HEIGHT - horizon) * z * z);
        DrawRectangle(0, y, WINDOW_WIDTH, 2, Fade(COLOR_PURPLE, 0.08f + 0.34f * z));
    }
    for (int i = -16; i <= 16; i++)
        DrawLineEx(V2(500 + i * 12.0f, (float)horizon), V2(500 + i * 120.0f, (float)WINDOW_HEIGHT), 1.5f, Fade(COLOR_PURPLE, 0.26f));
    if (wash > 0) DrawRectangle(0, 0, WINDOW_WIDTH, WINDOW_HEIGHT, Fade((Color){4, 6, 24, 255}, Clampf(wash * 2.4f, 0, 0.85f)));
}

static int RankIndex(int hp) { int r = 0; for (int i = 0; i < RANK_COUNT; i++) if (hp >= rankHp[i]) r = i; return r; }
// 0..1 progress towards the next rank (1 when maxed out)
static float RankProgress(int hp) {
    int r = RankIndex(hp);
    if (r >= RANK_COUNT - 1) return 1.0f;
    return (float)(hp - rankHp[r]) / (float)(rankHp[r + 1] - rankHp[r]);
}

static void DrawStatChip(Rectangle r, bool star, int value) {
    DrawRoundOutlined(r, r.height / 2, 3, COLOR_CREAM, COLOR_INK);
    if (star) {
        DrawStarShape(V2(r.x + 20, r.y + r.height / 2), 11, 0, COLOR_YELLOW);
        char text[24];
        snprintf(text, sizeof text, "%d", value);
        DrawText(text, (int)r.x + 38, (int)(r.y + r.height / 2) - 10, 20, COLOR_TXT);
        return;
    }
    // HP counts up smoothly whenever it grows, with a thin bar showing progress to the next rank
    static float shown = -1;
    if (shown < 0 || fabsf(shown - value) < 0.6f) shown = (float)value;
    shown += ((float)value - shown) * Clampf(GetFrameTime() * 5.0f, 0, 1);
    DrawNgon(V2(r.x + 20, r.y + r.height / 2 - 1), 6, 10, 0, COLOR_CYAN);
    DrawNgon(V2(r.x + 20, r.y + r.height / 2 - 1), 6, 5, 0, COLOR_INK);
    char text[24];
    snprintf(text, sizeof text, "%d HP", (int)(shown + 0.5f));
    DrawText(text, (int)r.x + 38, (int)(r.y + r.height / 2) - 12, 20, COLOR_TXT);
    Rectangle bar = { r.x + 38, r.y + r.height - 9, r.width - 54, 4 };
    DrawRectangleRec(bar, Fade(COLOR_CYAN, 0.18f));
    DrawRectangleRec((Rectangle){ bar.x, bar.y, bar.width * RankProgress((int)(shown + 0.5f)), bar.height }, COLOR_CYAN);
}

static int TotalStars(void) {
    int total = 0;
    for (int m = 0; m < MODE_COUNT; m++) for (int l = 0; l < LEVEL_COUNT; l++) total += app.progress.bestGems[m][l];
    return total;
}

// Purple bar with cream icing dripping off the bottom edge.
// HUD bar with a neon underline and a glitchy chromatic title.
static void DrawHeaderBar(const char *title, int titleX, bool chips) {
    DrawRectangleGradientV(0, 0, WINDOW_WIDTH, 60, (Color){8, 12, 38, 255}, (Color){20, 20, 64, 255});
    DrawRectangle(0, 0, WINDOW_WIDTH, 2, Fade(COLOR_PURPLE, 0.7f));
    for (int i = 0; i < WINDOW_WIDTH; i += 16) DrawRectangle(i, 50, 2, (i / 16) % 4 == 0 ? 8 : 4, Fade(COLOR_CYAN, 0.16f));
    DrawRectangle(0, 58, WINDOW_WIDTH, 2, Fade(COLOR_CYAN, 0.8f));
    DrawRectangle(0, 60, WINDOW_WIDTH, 5, Fade(COLOR_CYAN, 0.07f));
    if (title) {
        DrawText(title, titleX + 1, 16, 30, Fade(COLOR_PURPLE, 0.6f));
        DrawText(title, titleX, 15, 30, WHITE);
    }
    if (chips) {
        DrawStatChip((Rectangle){ 700, 11, 120, 36 }, true, TotalStars());
        DrawStatChip((Rectangle){ 835, 11, 140, 36 }, false, app.progress.totalXp);
    }
}

// ================================================================ the robot friend, Byte - a real 3D model
// Built from lit ellipsoids and drawn with rlgl through an orthographic camera, so one world unit is one
// screen pixel. He turns his head to follow the mouse, sways, blinks, waves and reacts to moods.
static float g3Tilt, g3Yaw, g3Roll;   // rotations (degrees) mirrored on the CPU so surfaces can be lit
static int botLayer;                  // later robots in a frame are pushed toward the camera so they never z-fight

static Vector3 Rot3(Vector3 n) {
    float r = g3Roll * DEG2RAD, y = g3Yaw * DEG2RAD, t = g3Tilt * DEG2RAD;
    Vector3 a = { n.x * cosf(r) - n.y * sinf(r), n.x * sinf(r) + n.y * cosf(r), n.z };
    Vector3 b = { a.x * cosf(y) + a.z * sinf(y), a.y, -a.x * sinf(y) + a.z * cosf(y) };
    return (Vector3){ b.x, b.y * cosf(t) - b.z * sinf(t), b.y * sinf(t) + b.z * cosf(t) };
}

#define EL_RINGS 8
#define EL_SLICES 12
// A shaded ellipsoid. glow 0..1 makes it self-lit (eyes, bulb, core). roll rotates it around Z.
static void Ellipsoid(Vector3 c, Vector3 r, Color col, float roll, float glow) {
    static Vector3 unit[EL_RINGS + 1][EL_SLICES + 1];
    static bool ready;
    if (!ready) {
        for (int i = 0; i <= EL_RINGS; i++)
            for (int j = 0; j <= EL_SLICES; j++) {
                float phi = -PI / 2 + PI * i / EL_RINGS, th = 2 * PI * j / EL_SLICES;
                unit[i][j] = (Vector3){ cosf(phi) * sinf(th), sinf(phi), cosf(phi) * cosf(th) };
            }
        ready = true;
    }
    const Vector3 light = { -0.42f, 0.62f, 0.66f }, half = { -0.21f, 0.31f, 0.83f };
    Vector3 pos[EL_RINGS + 1][EL_SLICES + 1];
    Color shade[EL_RINGS + 1][EL_SLICES + 1];
    float savedRoll = g3Roll;
    g3Roll += roll;
    for (int i = 0; i <= EL_RINGS; i++)
        for (int j = 0; j <= EL_SLICES; j++) {
            Vector3 u = unit[i][j];
            pos[i][j] = (Vector3){ u.x * r.x, u.y * r.y, u.z * r.z };
            Vector3 n = { u.x / r.x, u.y / r.y, u.z / r.z };
            float len = sqrtf(n.x * n.x + n.y * n.y + n.z * n.z);
            n = (Vector3){ n.x / len, n.y / len, n.z / len };
            n = Rot3(n);
            float d = n.x * light.x + n.y * light.y + n.z * light.z;
            float k = 0.40f + 0.60f * (d > 0 ? d : 0);
            float h = n.x * half.x + n.y * half.y + n.z * half.z;
            float spec = h > 0 ? powf(h, 18.0f) * 0.42f : 0;
            k = k * (1.0f - glow) + glow;
            float rr = col.r * k + 255 * spec, gg = col.g * k + 255 * spec, bb = col.b * k + 255 * spec;
            shade[i][j] = (Color){ (unsigned char)(rr > 255 ? 255 : rr), (unsigned char)(gg > 255 ? 255 : gg), (unsigned char)(bb > 255 ? 255 : bb), col.a };
        }
    g3Roll = savedRoll;
    rlCheckRenderBatchLimit(EL_RINGS * EL_SLICES * 6 + 8);
    rlPushMatrix();
    rlTranslatef(c.x, c.y, c.z);
    if (roll != 0) rlRotatef(roll, 0, 0, 1);
    rlBegin(RL_TRIANGLES);
    for (int i = 0; i < EL_RINGS; i++)
        for (int j = 0; j < EL_SLICES; j++) {
            int a[6][2] = { { i, j }, { i + 1, j }, { i + 1, j + 1 }, { i, j }, { i + 1, j + 1 }, { i, j + 1 } };
            for (int v = 0; v < 6; v++) {
                Color sc = shade[a[v][0]][a[v][1]];
                Vector3 p = pos[a[v][0]][a[v][1]];
                rlColor4ub(sc.r, sc.g, sc.b, sc.a);
                rlVertex3f(p.x, p.y, p.z);
            }
        }
    rlEnd();
    rlPopMatrix();
}

static Vector3 V3(float x, float y, float z) { return (Vector3){ x, y, z }; }

// cx = horizontal centre, footY = ground line on screen, s = size (1.0 is about 165 px tall).
static void DrawBot(float cx, float footY, float s, BotMood mood) {
    float t = (float)GetTime();
    Vector2 mouse = GetMousePosition();
    float up = sinf(t * 3.0f) * 2.5f;
    if (mood == MOOD_CHEER) up = fabsf(sinf(t * 7.0f)) * 12.0f;
    if (mood == MOOD_SAD) up = sinf(t * 1.6f) * 1.0f;
    if (mood == MOOD_HAPPY) { float ph = fmodf(t + s, 8.0f); if (ph < 0.7f) up += sinf(ph / 0.7f * PI) * 14.0f; }   // little idle hop

    // soft floor glow (2D, behind the robot)
    DrawEllipse((int)cx, (int)footY, 36 * s * (1.0f - up / (80 * s)), 7 * s, Fade(COLOR_CYAN, 0.22f));

    float yaw = sinf(t * 0.6f) * 16.0f, tilt = 9.0f;
    float headYaw = Clampf((mouse.x - cx) / 340.0f, -1, 1) * 32.0f;
    float headPitch = Clampf((mouse.y - (footY - 110 * s)) / 320.0f, -1, 1) * 11.0f, headRoll = 0;
    if (mood == MOOD_CHEER) yaw += sinf(t * 6.0f) * 28.0f;
    if (mood == MOOD_SAD) { headPitch = 15; headYaw *= 0.4f; }
    if (mood == MOOD_THINK) { headRoll = -7; headYaw = -14 + headYaw * 0.3f; headPitch = -6; }

    Color white = { 222, 232, 252, 255 }, shadeCol = { 150, 168, 222, 255 };
    Color headCol = { 170, 226, 244, 255 }, glow = { 120, 255, 238, 255 };
    Color dark = { 14, 18, 44, 255 }, foot = { 120, 128, 232, 255 };

    BeginMode3D((Camera3D){ { 0, 0, 1400 }, { 0, 0, 0 }, { 0, 1, 0 }, (float)WINDOW_HEIGHT, CAMERA_ORTHOGRAPHIC });
    rlSetClipPlanes(1.0, 4000.0);
    rlDisableBackfaceCulling();
    rlPushMatrix();
    rlTranslatef(cx - WINDOW_WIDTH / 2.0f, WINDOW_HEIGHT / 2.0f - footY + up * s, 220.0f * (float)botLayer);
    botLayer++;
    rlScalef(s, s, s);
    rlRotatef(tilt, 1, 0, 0);
    rlRotatef(yaw, 0, 1, 0);
    g3Tilt = tilt; g3Yaw = yaw; g3Roll = 0;

    // feet and legs
    for (int side = -1; side <= 1; side += 2) {
        Ellipsoid(V3(side * 15.0f, 7, 5), V3(13, 7, 16), foot, 0, 0);
        Ellipsoid(V3(side * 15.0f, 22, 0), V3(6.5f, 11, 6.5f), shadeCol, 0, 0);
    }
    // torso, belly plate and glowing core
    Ellipsoid(V3(0, 52, 0), V3(27, 25, 22), white, 0, 0);
    Ellipsoid(V3(0, 33, 1), V3(21, 6, 18), shadeCol, 0, 0);
    Ellipsoid(V3(0, 55, 17), V3(14, 11, 6), dark, 0, 0);
    float beat = 1.0f + 0.16f * sinf(t * 6.0f);
    Color core = mood == MOOD_SAD ? (Color){ 130, 150, 255, 255 } : (Color){ 150, 120, 255, 255 };
    Ellipsoid(V3(0, 55, 21), V3(6 * beat, 6 * beat, 4), core, 0, 0.75f + 0.25f * sinf(t * 6.0f));

    // arms: 0 = hanging down, 90 = straight out, 180 = straight up
    float angL = 22, angR = 22;
    if (mood == MOOD_CHEER) { angL = 160 + sinf(t * 9) * 14; angR = 160 - sinf(t * 9) * 14; }
    else if (mood == MOOD_HAPPY) angR = 150 + sinf(t * 8) * 22;
    else if (mood == MOOD_SAD) { angL = 10; angR = 12; }
    else if (mood == MOOD_THINK) angR = 128;
    for (int side = -1; side <= 1; side += 2) {
        float ang = side < 0 ? angL : angR;
        float dx = side * sinf(ang * DEG2RAD), dy = -cosf(ang * DEG2RAD);
        Vector3 shoulder = V3(side * 29.0f, 62, 0);
        Ellipsoid(V3(shoulder.x + dx * 11, shoulder.y + dy * 11, 0), V3(6.5f, 12.5f, 6.5f), shadeCol, 180.0f + side * ang, 0);
        Ellipsoid(V3(shoulder.x + dx * 24, shoulder.y + dy * 24, 0), V3(8, 8, 8), white, 0, 0);
    }

    // head group: turns toward the mouse
    rlPushMatrix();
    rlTranslatef(0, 74, 0);
    rlRotatef(headRoll, 0, 0, 1);
    rlRotatef(headPitch, 1, 0, 0);
    rlRotatef(headYaw, 0, 1, 0);
    rlTranslatef(0, -74, 0);
    float sTilt = g3Tilt, sYaw = g3Yaw, sRoll = g3Roll;
    g3Tilt += headPitch; g3Yaw += headYaw; g3Roll += headRoll;

    Ellipsoid(V3(0, 104, 0), V3(44, 34, 38), headCol, 0, 0);                  // shell
    Ellipsoid(V3(-45, 104, 0), V3(9, 15, 15), (Color){ 122, 138, 240, 255 }, 0, 0);   // ear pods
    Ellipsoid(V3(45, 104, 0), V3(9, 15, 15), (Color){ 122, 138, 240, 255 }, 0, 0);
    Ellipsoid(V3(0, 105, 23), V3(35, 25, 16), dark, 0, 0);                    // face screen
    float blink = fmodf(t, 3.7f) > 3.58f ? 0.15f : 1.0f;
    float lookX = Clampf((mouse.x - cx) / 500.0f, -1, 1) * 2.0f, lookY = mood == MOOD_THINK ? 2.5f : (mood == MOOD_SAD ? -2.0f : 0);
    for (int side = -1; side <= 1; side += 2) {
        float ex = side * 15.0f + lookX, ey = 110 + lookY;
        if (mood == MOOD_CHEER) {   // happy arcs ^ ^
            for (int k = -2; k <= 2; k++) Ellipsoid(V3(ex + k * 3.2f, ey - 3 + (1 - (k * k) / 4.0f) * 4.0f, 40), V3(2.3f, 2.3f, 2), glow, 0, 1);
        } else {
            Ellipsoid(V3(ex, ey, 40), V3(7.5f, 9.5f * blink, 3), glow, 0, 1);
            if (blink > 0.5f) Ellipsoid(V3(ex - 2.5f, ey + 3, 43), V3(2.4f, 2.4f, 1.5f), WHITE, 0, 1);
        }
        Ellipsoid(V3(side * 27.0f, 97, 36), V3(5, 3, 2), (Color){ 196, 128, 232, 255 }, 0, 0.55f);   // blush
    }
    if (mood == MOOD_SAD) for (int k = -2; k <= 2; k++) Ellipsoid(V3(k * 3.0f, 89 + (1 - k * k / 4.0f) * 3.5f, 40), V3(1.8f, 1.8f, 1.5f), glow, 0, 1);
    else if (mood == MOOD_THINK) Ellipsoid(V3(0, 90, 40), V3(3.2f, 3.2f, 2), glow, 0, 1);
    else if (mood == MOOD_CHEER) Ellipsoid(V3(0, 91, 40), V3(7, 4.5f, 2.5f), (Color){ 180, 130, 255, 255 }, 0, 0.85f);
    else for (int k = -3; k <= 3; k++) Ellipsoid(V3(k * 2.6f, 90 + k * k * 0.5f, 40), V3(1.8f, 1.8f, 1.5f), glow, 0, 1);

    // antenna
    float sway = sinf(t * 2.0f) * 3.0f;
    if (mood == MOOD_SAD) sway = 7;
    Ellipsoid(V3(sway * 0.5f, 142, 0), V3(2.2f, 10, 2.2f), shadeCol, -sway * 1.4f, 0);
    Color bulb = mood == MOOD_SAD ? (Color){ 150, 170, 235, 255 } : (Color){ 160, 220, 255, 255 };
    Ellipsoid(V3(sway, 153, 0), V3(7, 7, 7), bulb, 0, 0.55f + 0.45f * sinf(t * 4.0f));
    g3Tilt = sTilt; g3Yaw = sYaw; g3Roll = sRoll;
    rlPopMatrix();

    rlPopMatrix();
    rlEnableBackfaceCulling();
    EndMode3D();

    if (mood == MOOD_CHEER)
        for (int i = 0; i < 3; i++) {
            float a = t * 2.5f + i * 2.1f;
            DrawSparkle(V2(cx + cosf(a) * 62 * s, footY - 100 * s + sinf(a) * 26 * s), (5 + 3 * sinf(t * 8 + i)) * s + 2, COLOR_YELLOW);
        }
}

static void DrawBubble(Rectangle r, const char *text, int fontSize, float tailX) {
    float bottom = r.y + r.height;
    DrawTriSafe(V2(tailX - 14, bottom - 2), V2(tailX + 14, bottom - 2), V2(tailX + 2, bottom + 22), COLOR_NEON);
    DrawRoundOutlined(r, 14, 3, COLOR_CREAM, COLOR_INK);
    DrawTriSafe(V2(tailX - 10, bottom - 2), V2(tailX + 10, bottom - 2), V2(tailX + 2, bottom + 14), COLOR_CREAM);
    DrawRectangle((int)tailX - 10, (int)bottom - 3, 20, 4, COLOR_CREAM);
    WrapText(text, (int)r.x + 12, (int)r.y + 10, (int)r.width - 24, fontSize, COLOR_TXT, true);
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
        DrawText(app.toast, (int)r.x + 25, (int)r.y + 12, 22, Fade(COLOR_TXT, a));
    }
}

// ================================================================ candy button
// Glossy button with a chunky 3D base. Returns true when clicked.
static Color Mix(Color c, float f) { return (Color){ (unsigned char)(c.r * f), (unsigned char)(c.g * f), (unsigned char)(c.b * f), 255 }; }

// Arcade-style neon button: dark glass face, glowing edge, chunky base. Returns true when clicked.
static bool DrawCandyButton(Rectangle bounds, const char *label, Color fill, bool enabled, int fontSize) {
    bool hovered = enabled && HoverRect(bounds);
    bool down = hovered && IsMouseButtonDown(MOUSE_BUTTON_LEFT);
    float depth = 7;
    float faceShift = down ? depth - 2 : 0;
    Color neon = enabled ? fill : (Color){84, 90, 126, 255};
    Rectangle slab = { bounds.x + 2, bounds.y + depth, bounds.width, bounds.height - depth };
    Rectangle top = { bounds.x, bounds.y + faceShift, bounds.width, bounds.height - depth };
    float cut = top.height * 0.3f;
    DrawChamfer(slab, cut, Mix(neon, 0.38f));
    if (hovered) DrawChamfer(Expand(top, 5), cut + 3, Fade(neon, 0.14f));
    DrawChamfer(Expand(top, 1), cut, COLOR_INK);
    DrawChamfer(top, cut, neon);
    DrawChamfer(Expand(top, -3), cut - 1.5f, enabled ? Mix(neon, hovered ? 0.5f : 0.17f) : (Color){22, 22, 44, 255});
    DrawRectangle((int)(top.x + cut + 4), (int)top.y + 5, (int)(top.width - 2 * cut - 8), 2, Fade(WHITE, enabled ? 0.28f : 0.1f));
    if (label[0]) {
        int tw = MeasureText(label, fontSize);
        Color text = enabled ? (hovered ? WHITE : ColorBrightness(neon, 0.35f)) : (Color){110, 116, 150, 255};
        DrawTextO(label, (int)(top.x + top.width / 2) - tw / 2, (int)(top.y + top.height / 2) - fontSize / 2 + 1, fontSize, text, COLOR_INK);
    }
    return enabled && TakeClick(hovered);
}

static void DrawPadlock(Vector2 c, float s, Color body) {
    DrawRing(V2(c.x, c.y - 6 * s), 6 * s, 9.5f * s, 180, 360, 14, COLOR_INK);
    DrawRoundOutlined((Rectangle){ c.x - 11 * s, c.y - 6 * s, 22 * s, 18 * s }, 4 * s, 2.5f * s, body, COLOR_INK);
    DrawCircleV(V2(c.x, c.y + 2 * s), 2.6f * s, COLOR_INK);
}

// ================================================================ progress and session logic
// ---- players: any number of named profiles live in one file; the guest slot has its own small file
#define PROFILE_FILE "cquest_players.dat"
#define NAME_CAP 24
typedef struct { char name[NAME_CAP]; Progress progress; } Profile;
static Profile *profiles;
static int profileCount, profileCap, activeProfile = -1;   // -1 = guest

static void SaveProfiles(void) {
    int record = NAME_CAP + (int)sizeof(Progress), size = 12 + profileCount * record;
    unsigned char *buffer = (unsigned char *)calloc((size_t)size, 1);
    if (!buffer) return;
    memcpy(buffer, "CQP1", 4);
    int count = profileCount, progressSize = (int)sizeof(Progress);
    memcpy(buffer + 4, &count, 4);
    memcpy(buffer + 8, &progressSize, 4);
    for (int i = 0; i < profileCount; i++) {
        memcpy(buffer + 12 + i * record, profiles[i].name, NAME_CAP);
        memcpy(buffer + 12 + i * record + NAME_CAP, &profiles[i].progress, sizeof(Progress));
    }
    SaveFileData(PROFILE_FILE, buffer, size);
    free(buffer);
}

static void LoadProfiles(void) {
    profileCount = 0;
    if (!FileExists(PROFILE_FILE)) return;
    int size = 0;
    unsigned char *data = LoadFileData(PROFILE_FILE, &size);
    if (data && size >= 12 && memcmp(data, "CQP1", 4) == 0) {
        int count, progressSize;
        memcpy(&count, data + 4, 4);
        memcpy(&progressSize, data + 8, 4);
        int record = NAME_CAP + progressSize;
        if (count > 0 && progressSize > 0 && 12 + (long long)count * record <= size) {
            profileCap = count + 8;
            profiles = (Profile *)calloc((size_t)profileCap, sizeof(Profile));
            for (int i = 0; i < count; i++) {
                memcpy(profiles[i].name, data + 12 + i * record, NAME_CAP);
                profiles[i].name[NAME_CAP - 1] = '\0';
                memcpy(&profiles[i].progress, data + 12 + i * record + NAME_CAP, (size_t)(progressSize < (int)sizeof(Progress) ? progressSize : (int)sizeof(Progress)));
            }
            profileCount = count;
        }
    }
    if (data) UnloadFileData(data);
}

static bool NameEqual(const char *a, const char *b) {
    for (; *a && *b; a++, b++) {
        char x = *a, y = *b;
        if (x >= 'A' && x <= 'Z') x = (char)(x + 32);
        if (y >= 'A' && y <= 'Z') y = (char)(y + 32);
        if (x != y) return false;
    }
    return *a == *b;
}

static int FindProfile(const char *name) {
    for (int i = 0; i < profileCount; i++) if (NameEqual(profiles[i].name, name)) return i;
    return -1;
}

static int AddProfile(const char *name, const Progress *progress) {
    if (profileCount == profileCap) {
        profileCap = profileCap ? profileCap * 2 : 8;
        profiles = (Profile *)realloc(profiles, (size_t)profileCap * sizeof(Profile));
    }
    Profile *p = &profiles[profileCount];
    memset(p, 0, sizeof *p);
    snprintf(p->name, NAME_CAP, "%s", name);
    p->progress = *progress;
    SaveProfiles();
    return profileCount++;
}

static void LoadGuestProgress(void);

static void DeleteProfile(int index) {
    for (int i = index; i < profileCount - 1; i++) profiles[i] = profiles[i + 1];
    profileCount--;
    bool wasActive = activeProfile == index;
    if (activeProfile > index) activeProfile--;
    SaveProfiles();
    if (wasActive) LoadGuestProgress();   // the deleted player's progress must not carry over to the guest
}

static void LoadGuestProgress(void) {
    memset(&app.progress, 0, sizeof app.progress);
    activeProfile = -1;
    if (!FileExists(PROGRESS_FILE)) return;
    int size = 0;
    unsigned char *data = LoadFileData(PROGRESS_FILE, &size);
    // Older save files are shorter (no per-question stats / boosters yet) and still load fine.
    if (data && size > 0 && size <= (int)sizeof(Progress)) memcpy(&app.progress, data, (size_t)size);
    if (data) UnloadFileData(data);
}

static bool HasProgress(void) {
    if (app.progress.totalXp > 0) return true;
    for (int m = 0; m < MODE_COUNT; m++) for (int l = 0; l < LEVEL_COUNT; l++) if (app.progress.bestGems[m][l] > 0) return true;
    return false;
}

// Named players are saved to their profile; the guest is auto-saved to a small file of its own.
static void SaveProgress(void) {
    if (activeProfile >= 0 && activeProfile < profileCount) { profiles[activeProfile].progress = app.progress; SaveProfiles(); }
    else SaveFileData(PROGRESS_FILE, &app.progress, sizeof(Progress));
}

static void SelectProfile(int index) {
    activeProfile = index;
    app.progress = profiles[index].progress;
}

// Saves whatever the guest has achieved under a name and switches to that player (the guest slot is cleared).
static int CreateProfile(const char *name, bool keepCurrentProgress) {
    Progress fresh;
    memset(&fresh, 0, sizeof fresh);
    int index = AddProfile(name, keepCurrentProgress ? &app.progress : &fresh);
    if (keepCurrentProgress && activeProfile < 0) remove(PROGRESS_FILE);
    SelectProfile(index);
    return index;
}

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

// ---- HP milestones hand out boosters
static void CheckMilestones(void) {
    while (app.progress.milestone < MILESTONE_COUNT && app.progress.totalXp >= milestoneHp[app.progress.milestone]) {
        int m = app.progress.milestone++;
        char text[96] = "NEW BOOSTER: ";
        for (int b = 0; b < BOOST_COUNT; b++) {
            if (milestoneReward[m][b] <= 0) continue;
            app.progress.boost[b] += milestoneReward[m][b];
            char part[32];
            snprintf(part, sizeof part, "%s x%d  ", boostShort[b], milestoneReward[m][b]);
            strncat(text, part, sizeof text - strlen(text) - 1);
        }
        snprintf(app.rewardText, sizeof app.rewardText, "%s", text);
        ShowToast(text);
        PlayFx(FX_UNLOCK);
        SpawnBurst(V2(500, 300), 50);
    }
}

// Spends one booster. Returns true when it was used.
static bool UseBooster(int id) {
    if (app.progress.boost[id] <= 0) { ShowToast("None left - earn more at HP milestones!"); return false; }
    const Question *question = CurrentQuestion();
    if (id == BOOST_FIFTY) {
        if (app.used5050[app.current]) { ShowToast("Already used on this question"); return false; }
        if (question->kind == KIND_MCQ) {
            int removed = 0;
            for (int i = 0; i < 4 && removed < 2; i++)
                if (i != question->correctOption && !app.eliminated[i]) { app.eliminated[i] = true; removed++; }
            if (removed == 0) { ShowToast("Nothing left to remove!"); return false; }
        } else if (question->kind == KIND_CODE) {
            app.qHint[app.current] = true;
        }
        app.used5050[app.current] = true;
    } else if (id == BOOST_SHIELD) {
        if (app.shieldOn) { ShowToast("Shield is already on"); return false; }
        app.shieldOn = true;
    } else {
        if (app.doubleOn) { ShowToast("2x HP is already on"); return false; }
        app.doubleOn = true;
    }
    app.progress.boost[id]--;
    SaveProgress();
    PlayFx(FX_UNLOCK);
    SpawnBurst(V2(560, 560), 22);
    return true;
}

static void BeginSession(void) {
    for (int i = 0; i < app.questionCount; i++) { app.qStatus[i] = ST_UNSEEN; app.qHint[i] = false; }
    app.current = 0;
    app.streak = 0;
    app.levelXp = 0;
    app.fixedCount = 0;
    app.confirmQuit = false;
    app.selectedLevel = -1;
    app.shieldOn = app.doubleOn = false;
    for (int i = 0; i < MAX_QUESTIONS_PER_LEVEL; i++) app.used5050[i] = false;
    app.bonusHp = 0;
    app.rewardText[0] = '\0';
    app.introTime = 2.1f;
    app.qTime = 0;
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
    app.qTime = 0;
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
    app.bonusHp = 0;
    if (app.levelIndex >= 0) {   // clearing a level for the first time (and 3-starring it) lifts your HP
        int old = app.progress.bestGems[app.mode][app.levelIndex];
        if (old == 0 && app.levelGems > 0) app.bonusHp += 25;
        if (old < 3 && app.levelGems == 3) app.bonusHp += 15;
        app.levelXp += app.bonusHp;
        app.progress.totalXp += app.bonusHp;
        CheckMilestones();
    }
    bool nextWasOpen = app.levelIndex >= 0 && app.levelIndex + 1 < LEVEL_COUNT && IsLevelUnlocked(app.mode, app.levelIndex + 1);
    if (app.levelIndex >= 0 && app.levelGems > app.progress.bestGems[app.mode][app.levelIndex]) app.progress.bestGems[app.mode][app.levelIndex] = app.levelGems;
    if (app.levelIndex >= 0 && app.levelIndex + 1 < LEVEL_COUNT && !nextWasOpen && IsLevelUnlocked(app.mode, app.levelIndex + 1)) app.pendingHop = app.levelIndex;
    app.lastRunCount = app.questionCount;
    for (int i = 0; i < app.questionCount; i++) { app.lastRunIds[i] = app.questionIds[i]; app.lastRunStatus[i] = app.qStatus[i]; }
    SaveProgress();
    SetScreen(SCREEN_RESULTS);
    bool passed = app.levelIndex < 0 ? app.correctCount * 2 >= app.questionCount : app.levelGems > 0;
    PlayFx(passed ? FX_FANFARE : FX_FAIL);
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
    QStatus *status = &app.qStatus[app.current];
    if (!correct && app.shieldOn && (*status == ST_UNSEEN || *status == ST_SKIPPED)) {   // the shield forgives this one
        app.shieldOn = false;
        app.lastAnswerCorrect = false;
        app.revealed = false;
        snprintf(app.feedbackHead, sizeof app.feedbackHead, "Shield saved you!");
        snprintf(app.feedbackText, sizeof app.feedbackText, "Your shield absorbed that mistake - nothing was recorded. Have another go!\nHint: %s", question->hint);
        snprintf(app.revealText, sizeof app.revealText, "%s\n%s", detail, question->explanation);
        SpawnBurst(popAt, 26);
        PlayFx(FX_UNLOCK);
        OpenSheet();
        return;
    }
    stat->attempts++;
    if (correct) stat->correct++; else stat->wrong++;
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
            if (app.doubleOn) gain *= 2;
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
        snprintf(xp, sizeof xp, "+%d HP", gain);
        AddFloatText(xp, V2(popAt.x, popAt.y - 20), COLOR_YELLOW);
    }
    if (correct) {
        snprintf(app.feedbackText, sizeof app.feedbackText, "%s", question->explanation);
        SpawnBurst(popAt, 34);
    } else {
        snprintf(app.feedbackText, sizeof app.feedbackText, "Have another go, or peek at the answer.\nHint: %s", question->hint);
        snprintf(app.revealText, sizeof app.revealText, "%s\n%s", detail, question->explanation);
    }
    PlayFx(correct ? FX_CORRECT : FX_WRONG);
    CheckMilestones();
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
        if (key >= 32 && key <= 126 && *length < capacity - 1) { buffer[(*length)++] = (char)key; buffer[*length] = '\0'; PlayFx(FX_TICK); }
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
    for (int key = GetCharPressed(); key > 0; key = GetCharPressed()) if (key >= 32 && key <= 126) { CodeInsert((char)key); PlayFx(FX_TICK); }
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
static bool titleSeen;
static float titleBootShift, titleLeave = -1.0f;

static void ScreenTitle(void) {
    float t = (float)GetTime(), dt = GetFrameTime();
    float e = app.screenTime + (titleSeen ? 1.9f : titleBootShift);   // first launch plays a short boot sequence
    bool skip = IsMouseButtonPressed(MOUSE_BUTTON_LEFT) || IsKeyPressed(KEY_ENTER) || IsKeyPressed(KEY_SPACE);
    if (e < 1.9f && skip) { titleBootShift += 1.9f - e; e = 1.9f; }
    static float prevE;
    if (prevE < 1.9f && e >= 1.9f) { PlayFx(FX_WHOOSH); PlayFx(FX_POP); }
    prevE = e;

    if (e < 1.9f) {   // ---- boot terminal
        DrawBackground(0.55f);
        Rectangle box = { 240, 250, 520, 190 };
        DrawRoundOutlined(box, 16, 3, COLOR_CREAM, COLOR_INK);
        static const char *lines[3] = { "> BOOTING C QUEST v1.0", "> LOADING LEVELS ...", "> WAKING UP BYTE ..." };
        int typed = 0;
        for (int i = 0; i < 3; i++) {
            int length = (int)strlen(lines[i]);
            int chars = (int)((e - (0.1f + i * 0.5f)) * 40.0f);
            chars = chars < 0 ? 0 : (chars > length ? length : chars);
            char text[40];
            memcpy(text, lines[i], (size_t)chars);
            text[chars] = '\0';
            DrawText(text, (int)box.x + 24, (int)box.y + 22 + i * 30, 20, i == 2 ? COLOR_GREEN : COLOR_TXT);
            typed += chars;
        }
        static int prevTyped;
        if (typed / 3 != prevTyped / 3) PlayFx(FX_TICK);
        prevTyped = typed;
        float fill = Smooth(e / 1.7f);
        Rectangle bar = { box.x + 24, box.y + 124, box.width - 48, 24 };
        DrawRound(bar, 6, (Color){14, 16, 50, 255});
        DrawRound((Rectangle){ bar.x + 3, bar.y + 3, (bar.width - 6) * fill, bar.height - 6 }, 4, COLOR_CYAN);
        for (float x = bar.x + 14; x < bar.x + bar.width; x += 14) DrawRectangle((int)x, (int)bar.y + 3, 2, (int)bar.height - 6, Fade(COLOR_INK, 0.6f));
        char percent[16];
        snprintf(percent, sizeof percent, "%d%%", (int)(fill * 100));
        DrawText(percent, (int)(box.x + box.width - 24) - MeasureText(percent, 18), (int)box.y + 156, 18, COLOR_CYAN);
        if ((int)(t * 3) % 2 == 0) DrawText("_", (int)box.x + 24, (int)box.y + 156, 18, COLOR_GREEN);
        DrawBot(500, 232, 0.7f, MOOD_THINK);
        if (e > 1.75f) DrawRectangle(0, 0, WINDOW_WIDTH, WINDOW_HEIGHT, Fade(COLOR_TXT, (e - 1.75f) / 0.15f * 0.5f));
        return;
    }

    float a = e - 1.9f;   // seconds since the title revealed itself
    if (titleLeave >= 0) {
        titleLeave += dt;
        if (titleLeave > 0.95f) { titleLeave = -1.0f; titleSeen = true; SetScreen(profileCount > 0 ? SCREEN_PROFILES : SCREEN_HUB); return; }
    }
    DrawBackground(0);
    DrawCloud(V2(170 + sinf(t * 0.3f) * 20, 150), 1.0f, Fade(WHITE, 0.6f));
    DrawCloud(V2(840 + sinf(t * 0.25f) * 24, 190), 1.2f, Fade(WHITE, 0.6f));
    DrawLollipop(V2(95, 455), 32, COLOR_CYAN, COLOR_CREAM, 0.2f);
    DrawLollipop(V2(905, 440), 36, COLOR_YELLOW, COLOR_CREAM, -0.25f);
    DrawCupcake(V2(215, 612), 0.95f);
    DrawCupcake(V2(790, 616), 0.95f);
    DrawWrappedCandy(V2(120, 330 + sinf(t * 2) * 8), 18, COLOR_YELLOW, t);
    DrawWrappedCandy(V2(880, 300 + sinf(t * 2 + 1) * 8), 20, COLOR_GREEN, -t);

    // logo letters drop in one by one, then bob gently
    const char *logo = "C QUEST";
    int size = 112, spacing = 6, total = 0;
    for (int i = 0; logo[i]; i++) { char one[2] = { logo[i], 0 }; total += MeasureText(one, size) + spacing; }
    int x = WINDOW_WIDTH / 2 - (total - spacing) / 2;
    for (int i = 0; logo[i]; i++) {
        char one[2] = { logo[i], 0 };
        float k = Clampf((a - 0.08f * i) / 0.6f, 0, 1);
        float bob = a > 1.4f ? sinf(t * 3.2f + i * 0.8f) * 9.0f * Smooth((a - 1.4f) / 0.5f) : 0;
        if (titleLeave >= 0) bob -= fabsf(sinf(titleLeave * 9.0f + i * 0.5f)) * 14.0f;
        int y = (int)(56 + bob - (1.0f - EaseOutBack(k)) * 340.0f);
        if (k > 0 && logo[i] != ' ') {
            DrawText(one, x, y + 8, size, Fade(COLOR_INK, 0.5f));
            DrawText(one, x - 2, y, size, Fade(COLOR_CYAN, 0.45f));
            DrawText(one, x + 2, y, size, Fade(COLOR_PURPLE, 0.45f));
            DrawTextO(one, x, y, size, WHITE, COLOR_INK);
        }
        x += MeasureText(one, size) + spacing;
    }
    if (a > 0.9f) {
        DrawSparkle(V2(250 + sinf(t * 2) * 6, 70), 14, WHITE);
        DrawSparkle(V2(760, 92 + sinf(t * 2.4f) * 6), 11, COLOR_YELLOW);
        DrawSparkle(V2(838, 60), 9, WHITE);
    }

    Rectangle ribbon = { 290 - (1.0f - Smooth((a - 0.6f) / 0.5f)) * 800.0f, 206, 420, 50 };
    DrawRoundOutlined(ribbon, 25, 4, COLOR_CREAM, COLOR_INK);
    DrawTextCO("> learn C the retro way", (int)(ribbon.x + 210), 218, 26, COLOR_GREEN, COLOR_INK);

    // Byte rises from the floor, and hops with joy after PLAY
    float rise = 1.0f - Smooth((a - 0.3f) / 0.7f);
    float jump = titleLeave >= 0 ? fabsf(sinf(titleLeave * PI * 2.4f)) * 130.0f * (1.0f - titleLeave / 1.1f) : 0;
    DrawBot(500, 545 + rise * 230 - jump, 1.7f, titleLeave >= 0 ? MOOD_CHEER : MOOD_HAPPY);

    float popIn = EaseOutBack(Clampf((a - 1.0f) / 0.4f, 0, 1));
    float pulse = (1.0f + 0.04f * sinf(t * 5)) * popIn;
    bool ready = a > 1.3f && titleLeave < 0;
    if (popIn > 0.02f) {
        Rectangle play = { 500 - 140 * pulse, 568 + (1 - popIn) * 40, 280 * pulse, 62 };
        bool clicked = DrawCandyButton(play, "PLAY", COLOR_GREEN, ready, 38);
        if (ready && (clicked || skip)) {
            titleLeave = 0.0001f;
            SpawnBurst(V2(500, 400), 46);
            PlayFx(FX_UNLOCK);
        }
    }
    if (a < 0.2f) DrawRectangle(0, 0, WINDOW_WIDTH, WINDOW_HEIGHT, Fade(COLOR_TXT, 0.5f * (1.0f - a / 0.2f)));   // soft flash after boot
}

// ================================================================ shared widgets
static void DrawXMark(Rectangle b) {
    Vector2 c = V2(b.x + b.width / 2, b.y + (b.height - 7) / 2 + 1);
    DrawLineEx(V2(c.x - 8, c.y - 8), V2(c.x + 8, c.y + 8), 8, COLOR_INK);
    DrawLineEx(V2(c.x - 8, c.y + 8), V2(c.x + 8, c.y - 8), 8, COLOR_INK);
    DrawLineEx(V2(c.x - 8, c.y - 8), V2(c.x + 8, c.y + 8), 4, WHITE);
    DrawLineEx(V2(c.x - 8, c.y + 8), V2(c.x + 8, c.y - 8), 4, WHITE);
}

static const char *PlayerName(void) { return activeProfile >= 0 && activeProfile < profileCount ? profiles[activeProfile].name : "Guest"; }

static void DrawBoostIcon(int kind, Vector2 c, float s, Color col) {
    if (kind == BOOST_FIFTY) {
        DrawCircleSector(c, 9 * s, 90, 270, 16, col);
        DrawRing(c, 9 * s, 11 * s, 0, 360, 24, col);
    } else if (kind == BOOST_SHIELD) {
        DrawTriSafe(V2(c.x - 9 * s, c.y - 9 * s), V2(c.x + 9 * s, c.y - 9 * s), V2(c.x, c.y + 11 * s), col);
        DrawRectangle((int)(c.x - 9 * s), (int)(c.y - 11 * s), (int)(18 * s), (int)(10 * s), col);
        DrawTriSafe(V2(c.x - 4 * s, c.y - 5 * s), V2(c.x + 4 * s, c.y - 5 * s), V2(c.x, c.y + 4 * s), COLOR_INK);
    } else {
        DrawText("2x", (int)(c.x - 10 * s), (int)(c.y - 8 * s), (int)(18 * s), col);
    }
}

// The player's booster inventory. Hover a chip to read what it does.
static void DrawBoosterTray(Rectangle area, bool showTip) {
    const char *tip = NULL;
    float w = (area.width - 16) / 3.0f;
    for (int b = 0; b < BOOST_COUNT; b++) {
        Rectangle r = { area.x + b * (w + 8), area.y, w, area.height };
        bool have = app.progress.boost[b] > 0;
        bool hovered = HoverRect(r);
        if (hovered) { tip = boostLong[b]; wantHand = false; }
        Color col = have ? (b == 0 ? COLOR_CYAN : (b == 1 ? COLOR_PURPLE : COLOR_ORANGE)) : COLOR_GRAY;
        DrawRoundOutlined(r, 14, 2, (Color){ 14, 20, 48, 255 }, have ? Fade(col, 0.8f) : (Color){ 52, 60, 104, 255 });
        DrawBoostIcon(b, V2(r.x + 24, r.y + r.height / 2), 1.0f, col);
        DrawText(boostShort[b], (int)r.x + 44, (int)r.y + 7, 14, have ? COLOR_TXT : COLOR_GRAY);
        char count[8];
        snprintf(count, sizeof count, "x%d", app.progress.boost[b]);
        DrawText(count, (int)r.x + 44, (int)r.y + r.height - 20, 16, col);
    }
    if (showTip && tip) {
        int tw = MeasureText("W", 14) * (int)strlen(tip) / 1;
        (void)tw;
        Rectangle t = { area.x - 20, area.y - 34, area.width + 40, 28 };
        DrawRoundOutlined(t, 10, 2, COLOR_CREAM, COLOR_NEON);
        char fitted[140];
        FitText(tip, fitted, sizeof fitted, (int)t.width - 20, 13);
        DrawText(fitted, (int)t.x + 10, (int)t.y + 8, 13, COLOR_TXT);
    }
}

static Color AvatarColor(const char *name) {
    unsigned int h = 0;
    for (; *name; name++) h = h * 31u + (unsigned char)*name;
    return CandyColor((int)(h % 6u));
}

static void OpenNameModal(int mode) { app.nameModal = mode; app.nameBuf[0] = '\0'; app.nameLen = 0; }

// Pop-up that asks for a player name (new player, or "save my guest progress as ...").
static void DrawNameModal(void) {
    if (!app.nameModal) return;
    uiLocked = false;
    DrawRectangle(0, 0, WINDOW_WIDTH, WINDOW_HEIGHT, Fade(COLOR_INK, 0.75f));
    Rectangle p = { 270, 150, 460, 330 };
    DrawRoundOutlined(p, 26, 4, COLOR_CREAM, COLOR_INK);
    DrawTextCentered(app.nameModal == 1 ? "NEW PLAYER" : "SAVE MY PROGRESS", 500, 176, 30, COLOR_CYAN);
    WrapText(app.nameModal == 1 ? "Choose a name. Your stars, HP and boosters are saved under it."
                                : "Choose a name to keep your stars, HP and boosters. Next time, just pick it on the player screen.",
             (int)p.x + 30, 222, 400, 16, COLOR_DIM, true);
    HandleTextInput(app.nameBuf, &app.nameLen, 21);
    Rectangle box = { p.x + 30, 296, 400, 54 };
    DrawRoundOutlined(box, 14, 3, COLOR_CODE_BG, COLOR_NEON);
    char shown[40];
    snprintf(shown, sizeof shown, "%s%s", app.nameBuf, (int)(GetTime() * 2) % 2 == 0 ? "_" : "");
    DrawText(shown, (int)box.x + 16, (int)box.y + 13, 28, COLOR_TXT);
    DrawText("Up to 20 characters. ENTER to confirm.", (int)box.x, (int)box.y + 62, 14, COLOR_GRAY);

    bool cancel = DrawCandyButton((Rectangle){ p.x + 30, p.y + 250, 180, 56 }, "CANCEL", COLOR_ORANGE, true, 22) || IsKeyPressed(KEY_ESCAPE);
    bool ok = DrawCandyButton((Rectangle){ p.x + 250, p.y + 250, 180, 56 }, "SAVE", COLOR_GREEN, true, 24) || IsKeyPressed(KEY_ENTER);
    if (cancel) { app.nameModal = 0; return; }
    if (ok) {
        char name[NAME_CAP];
        snprintf(name, sizeof name, "%s", app.nameBuf);
        for (int i = (int)strlen(name) - 1; i >= 0 && name[i] == ' '; i--) name[i] = '\0';
        const char *start = name;
        while (*start == ' ') start++;
        if (!*start) { ShowToast("Type a name first"); return; }
        if (FindProfile(start) >= 0) { ShowToast("That name is taken - pick another"); return; }
        int mode = app.nameModal;
        app.nameModal = 0;
        CreateProfile(start, mode == 2);
        if (mode == 1) SetScreen(SCREEN_HUB);
        else { char msg[64]; snprintf(msg, sizeof msg, "Saved! Welcome, %s", start); ShowToast(msg); }
        PlayFx(FX_UNLOCK);
        SpawnBurst(V2(500, 300), 40);
    }
}

// ================================================================ player select
static void DrawDeleteConfirm(void) {
    if (!app.confirmDelete) return;
    uiLocked = false;
    int index = app.confirmDelete - 1;
    DrawRectangle(0, 0, WINDOW_WIDTH, WINDOW_HEIGHT, Fade(COLOR_INK, 0.75f));
    Rectangle p = { 290, 190, 420, 250 };
    DrawRoundOutlined(p, 26, 4, COLOR_CREAM, COLOR_INK);
    DrawTextCentered("DELETE PLAYER?", 500, 214, 30, COLOR_RED);
    char text[128];
    snprintf(text, sizeof text, "%s and all of their progress will be removed. This cannot be undone.", index < profileCount ? profiles[index].name : "");
    WrapText(text, (int)p.x + 30, 262, 360, 17, COLOR_TXT, true);
    if (DrawCandyButton((Rectangle){ p.x + 30, p.y + 170, 170, 56 }, "KEEP", COLOR_GREEN, true, 24) || IsKeyPressed(KEY_ESCAPE)) app.confirmDelete = 0;
    if (DrawCandyButton((Rectangle){ p.x + 220, p.y + 170, 170, 56 }, "DELETE", COLOR_RED, true, 24)) {
        if (index < profileCount) DeleteProfile(index);
        app.confirmDelete = 0;
    }
}

static void ScreenProfiles(void) {
    float e = app.screenTime;
    bool modal = app.nameModal || app.confirmDelete;
    uiLocked = modal;
    DrawBackground(0.4f);
    DrawHeaderBar("WHO'S PLAYING?", 122, false);
    if (DrawCandyButton((Rectangle){ 12, 8, 100, 44 }, "BACK", COLOR_ORANGE, true, 20) || (!modal && IsKeyPressed(KEY_ESCAPE))) SetScreen(SCREEN_TITLE);

    Rectangle panel = { 30, 84, 620, 472 };
    DrawRoundOutlined(panel, 24, 4, COLOR_CREAM, COLOR_INK);
    DrawText("SAVED PLAYERS", (int)panel.x + 24, (int)panel.y + 16, 20, COLOR_CYAN);
    char count[40];
    snprintf(count, sizeof count, "%d saved", profileCount);
    DrawText(count, (int)(panel.x + panel.width - 24) - MeasureText(count, 16), (int)panel.y + 19, 16, COLOR_GRAY);
    Rectangle view = { panel.x + 14, panel.y + 50, panel.width - 28, panel.height - 64 };
    const float rowH = 74;
    float total = profileCount * rowH;
    float maxScroll = total - view.height > 0 ? total - view.height : 0;
    if (!modal && CheckCollisionPointRec(GetMousePosition(), view)) app.profileScroll -= GetMouseWheelMove() * 50;
    app.profileScroll = Clampf(app.profileScroll, 0, maxScroll);
    if (profileCount == 0) {
        DrawTextCentered("No saved players yet", (int)(view.x + view.width / 2), (int)view.y + 110, 26, COLOR_TXT);
        WrapText("Create a player to keep your stars and HP, or jump in as a guest. You can save a guest's progress under a name later.", (int)view.x + 90, (int)view.y + 160, (int)view.width - 180, 17, COLOR_DIM, true);
    }
    BeginScissorMode((int)view.x, (int)view.y, (int)view.width, (int)view.height);
    int chosen = -1, deleted = -1;
    for (int i = 0; i < profileCount; i++) {
        float k = EaseOutBack(Clampf((e - 0.1f - i * 0.05f) / 0.4f, 0, 1));
        if (k <= 0.01f) continue;
        Rectangle row = { view.x + 4 + (1.0f - k) * 120.0f, view.y + i * rowH - app.profileScroll, view.width - 16, rowH - 8 };
        if (row.y + row.height < view.y || row.y > view.y + view.height) continue;
        bool inView = CheckCollisionPointRec(GetMousePosition(), view);
        Rectangle del = { row.x + row.width - 52, row.y + (row.height - 38) / 2, 38, 38 };
        bool hovered = inView && !modal && CheckCollisionPointRec(GetMousePosition(), row);
        bool delHover = inView && !modal && CheckCollisionPointRec(GetMousePosition(), del);
        DrawRoundOutlined(row, 16, 3, hovered ? (Color){ 24, 34, 74, 255 } : (Color){ 18, 24, 56, 255 }, hovered ? COLOR_CYAN : (Color){ 60, 66, 128, 255 });
        Color av = AvatarColor(profiles[i].name);
        DrawCircleV(V2(row.x + 36, row.y + row.height / 2), 22, COLOR_INK);
        DrawCircleV(V2(row.x + 36, row.y + row.height / 2), 19, av);
        char initial[2] = { (char)((profiles[i].name[0] >= 'a' && profiles[i].name[0] <= 'z') ? profiles[i].name[0] - 32 : profiles[i].name[0]), 0 };
        DrawTextCentered(initial, (int)row.x + 36, (int)(row.y + row.height / 2) - 12, 24, COLOR_INK);
        char fitted[40];
        FitText(profiles[i].name, fitted, sizeof fitted, 270, 24);
        DrawText(fitted, (int)row.x + 70, (int)row.y + 9, 24, COLOR_TXT);
        int hp = profiles[i].progress.totalXp, stars = 0;
        for (int m = 0; m < MODE_COUNT; m++) for (int l = 0; l < LEVEL_COUNT; l++) stars += profiles[i].progress.bestGems[m][l];
        char info[80];
        snprintf(info, sizeof info, "%s  -  %d HP  -  %d stars", rankNames[RankIndex(hp)], hp, stars);
        DrawText(info, (int)row.x + 70, (int)row.y + 40, 15, COLOR_DIM);
        DrawRoundOutlined(del, 10, 2, delHover ? (Color){ 60, 24, 60, 255 } : (Color){ 20, 24, 56, 255 }, delHover ? COLOR_RED : (Color){ 60, 66, 128, 255 });
        DrawLineEx(V2(del.x + 12, del.y + 12), V2(del.x + 26, del.y + 26), 3, delHover ? COLOR_RED : COLOR_GRAY);
        DrawLineEx(V2(del.x + 12, del.y + 26), V2(del.x + 26, del.y + 12), 3, delHover ? COLOR_RED : COLOR_GRAY);
        if (TakeClick(delHover)) deleted = i;
        else if (TakeClick(hovered)) chosen = i;
    }
    EndScissorMode();
    if (maxScroll > 0) {
        float barH = view.height * view.height / total;
        DrawRound((Rectangle){ view.x + view.width - 6, view.y + (view.height - barH) * (app.profileScroll / maxScroll), 5, barH }, 2, COLOR_CYAN);
    }
    if (chosen >= 0) { SelectProfile(chosen); SetScreen(SCREEN_HUB); PlayFx(FX_UNLOCK); }
    if (deleted >= 0) app.confirmDelete = deleted + 1;

    // right column: robot and the two big buttons
    DrawBubble((Rectangle){ 690, 96, 280, 66 }, profileCount ? "Welcome back! Pick your name to continue where you stopped." : "Hi! I'm Byte. Make a player to keep your progress.", 15, 830);
    DrawBot(830, 400, 1.55f, MOOD_HAPPY);
    if (DrawCandyButton((Rectangle){ 690, 440, 280, 58 }, "+ NEW PLAYER", COLOR_GREEN, true, 26)) OpenNameModal(1);
    if (DrawCandyButton((Rectangle){ 690, 506, 280, 50 }, "PLAY AS GUEST", COLOR_PURPLE, true, 22)) {
        LoadGuestProgress();
        SetScreen(SCREEN_HUB);
    }
    DrawNameModal();
    DrawDeleteConfirm();
}

// ================================================================ hub: Notes, MCQ map, Typing map
static void DrawHubCard(Rectangle r, int kind, Color accent) {
    bool hovered = HoverRect(r);
    if (hovered) r.y -= 6;
    DrawRound((Rectangle){ r.x + 4, r.y + 12, r.width, r.height }, 26, Fade(BLACK, 0.25f));
    DrawRoundOutlined(r, 26, 4, COLOR_CREAM, hovered ? accent : COLOR_INK);
    Rectangle band = { r.x, r.y, r.width, 62 };
    DrawRound(band, 26, Mix(accent, 0.62f));
    DrawRectangle((int)band.x, (int)band.y + 34, (int)band.width, 28, Mix(accent, 0.62f));
    DrawRectangle((int)band.x, (int)band.y + 60, (int)band.width, 2, accent);
    const char *titles[3] = { "NOTES", "MCQ MAP", "TYPING MAP" };
    DrawTextCO(titles[kind], (int)(r.x + r.width / 2), (int)r.y + 17, 30, WHITE, COLOR_INK);
    Vector2 mid = V2(r.x + r.width / 2, r.y + 126);
    if (kind == 0) {   // open book
        for (int side = -1; side <= 1; side += 2) {
            Rectangle page = { mid.x + (side < 0 ? -64.0f : 2.0f), mid.y - 36, 62, 74 };
            DrawRoundOutlined(page, 8, 2, (Color){ 206, 224, 246, 255 }, COLOR_INK);
            for (int i = 0; i < 5; i++) DrawRectangle((int)page.x + 10, (int)page.y + 12 + i * 12, i == 4 ? 26 : 42, 3, Fade(accent, 0.8f));
        }
        DrawRectangle((int)mid.x - 2, (int)mid.y - 38, 4, 78, COLOR_INK);
    } else if (kind == 1) {
        for (int i = 0; i < 4; i++) {
            Rectangle o = { mid.x - 84 + (i % 2) * 88, mid.y - 30 + (i / 2) * 36, 80, 30 };
            DrawRoundOutlined(o, 12, 2, (Color){ 20, 28, 64, 255 }, i == 1 ? COLOR_GREEN : (Color){ 70, 84, 150, 255 });
            char letter[2] = { (char)('A' + i), 0 };
            DrawTextCentered(letter, (int)(o.x + o.width / 2), (int)o.y + 7, 18, i == 1 ? COLOR_GREEN : COLOR_TXT);
        }
    } else {
        Rectangle box = { mid.x - 94, mid.y - 34, 188, 70 };
        DrawRoundOutlined(box, 12, 3, COLOR_CODE_BG, COLOR_INK);
        DrawCircleV(V2(box.x + 16, box.y + 14), 4, COLOR_RED);
        DrawCircleV(V2(box.x + 30, box.y + 14), 4, COLOR_YELLOW);
        DrawCircleV(V2(box.x + 44, box.y + 14), 4, COLOR_GREEN);
        DrawText((int)(GetTime() * 2) % 2 == 0 ? "int x = 5;_" : "int x = 5;", (int)box.x + 16, (int)box.y + 38, 20, COLOR_CODE_TXT);
    }
    const char *descriptions[3] = {
        "Study each module: clear notes plus flashcards with the key terms.",
        "Pick the right answer from four choices. Travel the Neon Skyline.",
        "Type your answers, then compile real code in the Function Lab.",
    };
    WrapText(descriptions[kind], (int)r.x + 22, (int)r.y + 188, (int)r.width - 44, 17, COLOR_TXT, true);
    char text[40];
    if (kind == 0) snprintf(text, sizeof text, "%d modules", LEVEL_COUNT);
    else {
        int stars = 0, maxStars = 0;
        for (int l = 0; l < LEVEL_COUNT; l++) if (IsLevelAvailable((QuizMode)(kind - 1), l)) { stars += app.progress.bestGems[kind - 1][l]; maxStars += 3; }
        snprintf(text, sizeof text, "%d / %d", stars, maxStars);
        DrawStarShape(V2(r.x + 34, r.y + r.height - 38), 12, 0, COLOR_YELLOW);
    }
    DrawText(text, (int)r.x + (kind == 0 ? 24 : 54), (int)r.y + (int)r.height - 48, 20, COLOR_TXT);
    if (DrawCandyButton((Rectangle){ r.x + r.width - 150, r.y + r.height - 66, 130, 50 }, kind == 0 ? "OPEN" : "PLAY", accent, true, 24) || TakeClick(hovered)) {
        if (kind == 0) { app.notesLevel = -1; app.notesTab = 0; SetScreen(SCREEN_NOTES); }
        else { app.mode = (QuizMode)(kind - 1); SetScreen(SCREEN_MAP); }
    }
}

static void ScreenHub(void) {
    float e = app.screenTime;
    uiLocked = app.nameModal != 0;
    DrawBackground(0.3f);
    Color accents[3] = { COLOR_CYAN, COLOR_ORANGE, COLOR_PURPLE };
    for (int i = 0; i < 3; i++) {   // cards slide up one after the other
        float k = EaseOutBack(Clampf((e - 0.1f - i * 0.12f) / 0.55f, 0, 1));
        if (k <= 0.01f) continue;
        DrawHubCard((Rectangle){ 40 + i * 315.0f, 86 + (1.0f - k) * 540.0f, 290, 340 }, i, accents[i]);
    }

    // player + rank plate (left), next reward (right)
    int hp = app.progress.totalXp, rank = RankIndex(hp);
    Rectangle plate = { 40, 446, 290, 74 };
    DrawRoundOutlined(plate, 16, 3, COLOR_CREAM, COLOR_INK);
    Color av = activeProfile >= 0 ? AvatarColor(PlayerName()) : COLOR_GRAY;
    DrawCircleV(V2(plate.x + 38, plate.y + 37), 24, COLOR_INK);
    DrawCircleV(V2(plate.x + 38, plate.y + 37), 21, av);
    char initial[2] = { PlayerName()[0], 0 };
    DrawTextCentered(initial, (int)plate.x + 38, (int)plate.y + 25, 24, COLOR_INK);
    char fitted[40];
    FitText(PlayerName(), fitted, sizeof fitted, 190, 22);
    DrawText(fitted, (int)plate.x + 74, (int)plate.y + 10, 22, COLOR_TXT);
    DrawText(rankNames[rank], (int)plate.x + 74, (int)plate.y + 38, 16, COLOR_CYAN);
    Rectangle bar = { plate.x + 74, plate.y + 58, 196, 6 };
    DrawRectangleRec(bar, Fade(COLOR_CYAN, 0.2f));
    DrawRectangleRec((Rectangle){ bar.x, bar.y, bar.width * RankProgress(hp), bar.height }, COLOR_CYAN);

    Rectangle reward = { 670, 446, 290, 74 };
    DrawRoundOutlined(reward, 16, 3, COLOR_CREAM, COLOR_INK);
    DrawText("NEXT BOOSTER", (int)reward.x + 18, (int)reward.y + 10, 16, COLOR_PURPLE);
    char line[64];
    if (app.progress.milestone < MILESTONE_COUNT) {
        int next = milestoneHp[app.progress.milestone];
        snprintf(line, sizeof line, "at %d HP  (%d to go)", next, next - hp > 0 ? next - hp : 0);
        DrawText(line, (int)reward.x + 18, (int)reward.y + 34, 18, COLOR_TXT);
        float from = app.progress.milestone > 0 ? (float)milestoneHp[app.progress.milestone - 1] : 0;
        float f = Clampf((hp - from) / (next - from), 0, 1);
        Rectangle b2 = { reward.x + 18, reward.y + 58, reward.width - 36, 6 };
        DrawRectangleRec(b2, Fade(COLOR_PURPLE, 0.22f));
        DrawRectangleRec((Rectangle){ b2.x, b2.y, b2.width * f, b2.height }, COLOR_PURPLE);
    } else DrawText("All rewards unlocked!", (int)reward.x + 18, (int)reward.y + 36, 18, COLOR_TXT);

    char hello[96];
    if (activeProfile >= 0) snprintf(hello, sizeof hello, "Hi %s! Read the notes, then take on a map.", PlayerName());
    else if (HasProgress()) snprintf(hello, sizeof hello, "Nice progress! Save it under your name below.");
    else snprintf(hello, sizeof hello, "Study the notes first, then pick a map!");
    if (e > 0.8f) DrawBubble((Rectangle){ 350, 446, 300, 54 }, hello, 15, 500);
    DrawBot(500, 636, 0.72f, MOOD_HAPPY);

    DrawHeaderBar("CHOOSE YOUR PATH", 122, true);
    if (DrawCandyButton((Rectangle){ 12, 8, 100, 44 }, "BACK", COLOR_ORANGE, true, 20) || (!app.nameModal && IsKeyPressed(KEY_ESCAPE))) SetScreen(SCREEN_TITLE);
    if (DrawCandyButton((Rectangle){ 25, 572, 190, 54 }, "PLAYERS", COLOR_PURPLE, true, 22)) SetScreen(SCREEN_PROFILES);
    if (activeProfile < 0 && HasProgress()) {
        float pulse = 0.5f + 0.5f * sinf((float)GetTime() * 4);
        DrawRound((Rectangle){ 225, 568, 200, 62 }, 18, Fade(COLOR_GREEN, 0.10f + 0.12f * pulse));
        if (DrawCandyButton((Rectangle){ 230, 572, 190, 54 }, "SAVE PROGRESS", COLOR_GREEN, true, 18)) OpenNameModal(2);
    }
    DrawBoosterTray((Rectangle){ 560, 572, 410, 52 }, true);
    DrawNameModal();
}

// ================================================================ notes and flashcards
static int DrawNotesBody(int level, Rectangle view, float scroll, bool draw) {
    textMode = 1;
    int x = (int)view.x + 28, width = (int)view.width - 72;
    int y = (int)view.y + 12 - (int)scroll, start = y;
    bool first = true;
    for (int i = 0; i < noteCounts[level]; i++) {
        const char *line = noteLines[level][i];
        char tag = line[0];
        const char *text = line + 1;
        if (tag == '#') {
            if (!first) y += 16;
            if (draw) {
                TextDraw(text, x, y, 26, COLOR_CYAN);
                DrawRectangle(x, y + 33, 56, 3, COLOR_PURPLE);
            }
            y += 44;
        } else if (tag == '~') {
            y = WrapText(text, x, y, width, 20, COLOR_TXT, draw) + 8;
        } else if (tag == '-') {
            if (draw) DrawNgon(V2((float)x + 8, (float)y + 11), 6, 5, 0, COLOR_CYAN);
            y = WrapText(text, x + 26, y, width - 26, 20, COLOR_TXT, draw) + 6;
        } else if (tag == '>') {
            int lines = 0;
            while (i + lines < noteCounts[level] && noteLines[level][i + lines][0] == '>') lines++;
            int h = lines * 26 + 18;
            if (draw) {
                DrawRoundOutlined((Rectangle){ (float)x, (float)y, (float)width, (float)h }, 12, 2, COLOR_CODE_BG, (Color){ 52, 68, 128, 255 });
                for (int k = 0; k < lines; k++) {
                    textMode = 2;
                    TextDraw(noteLines[level][i + k] + 1, x + 16, y + 9 + k * 26, 19, COLOR_CODE_TXT);
                    textMode = 1;
                }
            }
            y += h + 10;
            i += lines - 1;
        }
        first = false;
    }
    textMode = 0;
    return y - start + 24;
}

static void DrawFlashcard(Rectangle r, int level, int index, float flip, float pop) {
    float cosv = cosf(flip * PI);
    float scaleX = fabsf(cosv) < 0.04f ? 0.04f : fabsf(cosv);
    bool back = flip > 0.5f;
    Rectangle card = { r.x + r.width * (1 - scaleX) / 2, r.y, r.width * scaleX, r.height };
    float grow = 1.0f + (pop - 1.0f) * 0.04f;
    card = (Rectangle){ card.x + card.width * (1 - grow) / 2, card.y + card.height * (1 - grow) / 2, card.width * grow, card.height * grow };
    Color accent = back ? COLOR_PURPLE : COLOR_CYAN;
    DrawRound((Rectangle){ card.x + 5, card.y + 12, card.width, card.height }, 28, Fade(BLACK, 0.3f));
    DrawRoundOutlined(card, 28, 4, back ? (Color){ 26, 20, 62, 255 } : (Color){ 12, 30, 52, 255 }, accent);
    if (scaleX < 0.35f) return;
    textMode = 1;
    const Flashcard *fc = &cardSets[level][index];
    int cx = (int)(card.x + card.width / 2);
    const char *label = back ? "DEFINITION" : "KEY TERM";
    DrawTextCentered(label, cx, (int)card.y + 22, 16, accent);
    if (!back) {
        int size = 44;
        while (size > 24 && TextW(fc->front, size) > (int)card.width - 60) size -= 2;
        TextDraw(fc->front, cx - TextW(fc->front, size) / 2, (int)(card.y + card.height / 2) - size / 2 - 6, size, WHITE);
        textMode = 0;
        DrawTextCentered("click the card to flip", cx, (int)(card.y + card.height) - 34, 14, COLOR_GRAY);
    } else {
        int h = WrapText(fc->back, 0, 0, (int)card.width - 80, 26, WHITE, false);
        WrapText(fc->back, (int)card.x + 40, (int)(card.y + (card.height - (h - 0)) / 2) + 6, (int)card.width - 80, 26, WHITE, true);
        textMode = 0;
    }
}

static void ScreenNotes(void) {
    DrawBackground(0.42f);
    float e = app.screenTime;
    if (app.notesLevel < 0) {   // ---- module list
        DrawHeaderBar("STUDY NOTES", 122, true);
        if (DrawCandyButton((Rectangle){ 12, 8, 100, 44 }, "BACK", COLOR_ORANGE, true, 20) || IsKeyPressed(KEY_ESCAPE)) { SetScreen(SCREEN_HUB); return; }
        for (int l = 0; l < LEVEL_COUNT; l++) {
            float k = EaseOutBack(Clampf((e - 0.1f - l * 0.08f) / 0.45f, 0, 1));
            if (k <= 0.01f) continue;
            Rectangle row = { 40 + (1.0f - k) * 700.0f, 84 + l * 90.0f, 920, 80 };
            DrawRoundOutlined(row, 18, 3, COLOR_CREAM, COLOR_INK);
            Color col = LevelColor(l);
            DrawNgon(V2(row.x + 46, row.y + 40), 6, 28, 0, COLOR_INK);
            DrawNgon(V2(row.x + 46, row.y + 40), 6, 25, 0, col);
            char n[4];
            snprintf(n, sizeof n, "%d", l + 1);
            DrawTextCO(n, (int)row.x + 46, (int)row.y + 26, 28, WHITE, COLOR_INK);
            DrawText(levelNames[l], (int)row.x + 92, (int)row.y + 12, 24, COLOR_TXT);
            char blurb[120];
            FitText(levelBlurbs[l], blurb, sizeof blurb, 440, 15);
            DrawText(blurb, (int)row.x + 92, (int)row.y + 46, 15, COLOR_DIM);
            if (DrawCandyButton((Rectangle){ row.x + 560, row.y + 12, 170, 56 }, "READ NOTES", col, true, 18)) { app.notesLevel = l; app.notesTab = 0; app.notesScroll = 0; }
            char cards[32];
            snprintf(cards, sizeof cards, "FLASHCARDS %d", cardCounts[l]);
            if (DrawCandyButton((Rectangle){ row.x + 744, row.y + 12, 160, 56 }, cards, COLOR_PURPLE, true, 16)) { app.notesLevel = l; app.notesTab = 1; app.cardIndex = 0; app.cardFlipped = false; app.cardFlip = 0; }
        }
        DrawBot(880, 640, 0.55f, MOOD_HAPPY);
        return;
    }

    // ---- reader
    int level = app.notesLevel;
    DrawHeaderBar(levelNames[level], 122, true);
    if (DrawCandyButton((Rectangle){ 12, 8, 100, 44 }, "BACK", COLOR_ORANGE, true, 20) || IsKeyPressed(KEY_ESCAPE)) { app.notesLevel = -1; app.screenTime = 0.5f; return; }
    for (int tab = 0; tab < 2; tab++) {
        Rectangle r = { 30 + tab * 200.0f, 80, 190, 46 };
        if (DrawCandyButton(r, tab == 0 ? "NOTES" : "FLASHCARDS", tab == app.notesTab ? LevelColor(level) : COLOR_PURPLE, true, 20) && tab != app.notesTab) {
            app.notesTab = tab; app.notesScroll = 0; app.cardIndex = 0; app.cardFlipped = false; app.cardFlip = 0;
        }
    }
    if (DrawCandyButton((Rectangle){ 600, 80, 110, 46 }, "< PREV", COLOR_CYAN, level > 0, 16)) { app.notesLevel = level - 1; app.notesScroll = 0; app.cardIndex = 0; app.cardFlipped = false; app.cardFlip = 0; return; }
    if (DrawCandyButton((Rectangle){ 720, 80, 110, 46 }, "NEXT >", COLOR_CYAN, level < LEVEL_COUNT - 1, 16)) { app.notesLevel = level + 1; app.notesScroll = 0; app.cardIndex = 0; app.cardFlipped = false; app.cardFlip = 0; return; }

    Rectangle panel = { 30, 138, 940, 424 };
    DrawRoundOutlined(panel, 22, 4, COLOR_CREAM, COLOR_INK);
    if (app.notesTab == 0) {
        Rectangle view = { panel.x + 6, panel.y + 6, panel.width - 12, panel.height - 12 };
        int total = DrawNotesBody(level, view, 0, false);
        float maxScroll = total - view.height > 0 ? total - view.height : 0;
        if (CheckCollisionPointRec(GetMousePosition(), view)) app.notesScroll -= GetMouseWheelMove() * 60;
        if (KeyRepeat(KEY_DOWN)) app.notesScroll += 40;
        if (KeyRepeat(KEY_UP)) app.notesScroll -= 40;
        if (KeyRepeat(KEY_PAGE_DOWN)) app.notesScroll += 300;
        if (KeyRepeat(KEY_PAGE_UP)) app.notesScroll -= 300;
        app.notesScroll = Clampf(app.notesScroll, 0, maxScroll);
        BeginScissorMode((int)view.x, (int)view.y, (int)view.width, (int)view.height);
        DrawNotesBody(level, view, app.notesScroll, true);
        EndScissorMode();
        if (maxScroll > 0) {
            float barH = view.height * view.height / total;
            DrawRound((Rectangle){ view.x + view.width - 12, view.y + (view.height - barH) * (app.notesScroll / maxScroll), 6, barH }, 3, COLOR_CYAN);
            DrawTextCentered("scroll: mouse wheel or arrow keys", 525, 590, 14, COLOR_GRAY);
        }
        if (DrawCandyButton((Rectangle){ 790, 572, 185, 54 }, "FLASHCARDS", COLOR_PURPLE, true, 20)) { app.notesTab = 1; app.cardIndex = 0; app.cardFlipped = false; app.cardFlip = 0; }
        if (DrawCandyButton((Rectangle){ 30, 572, 230, 54 }, "ALL MODULES", COLOR_ORANGE, true, 20)) { app.notesLevel = -1; app.screenTime = 0.5f; }
    } else {
        int count = cardCounts[level];
        float target = app.cardFlipped ? 1.0f : 0.0f;
        app.cardFlip += (target - app.cardFlip) * Clampf(GetFrameTime() * 9.0f, 0, 1);
        if (fabsf(target - app.cardFlip) < 0.003f) app.cardFlip = target;
        Rectangle card = { 150, 176, 700, 290 };
        bool hovered = HoverRect(card);
        if (TakeClick(hovered) || IsKeyPressed(KEY_SPACE) || IsKeyPressed(KEY_ENTER)) app.cardFlipped = !app.cardFlipped;
        DrawFlashcard(card, level, app.cardIndex, app.cardFlip, hovered ? 1.0f : 0.0f);
        char counter[32];
        snprintf(counter, sizeof counter, "%d / %d", app.cardIndex + 1, count);
        DrawTextCentered(counter, 500, 482, 20, COLOR_TXT);
        for (int i = 0; i < count; i++) DrawCircleV(V2(500 + (i - (count - 1) / 2.0f) * 22, 520), i == app.cardIndex ? 6.0f : 4.0f, i == app.cardIndex ? COLOR_CYAN : Fade(COLOR_GRAY, 0.6f));
        bool prev = DrawCandyButton((Rectangle){ 30, 572, 160, 54 }, "< PREV", COLOR_ORANGE, true, 20) || KeyRepeat(KEY_LEFT);
        bool next = DrawCandyButton((Rectangle){ 810, 572, 160, 54 }, "NEXT >", COLOR_GREEN, true, 22) || KeyRepeat(KEY_RIGHT);
        if (DrawCandyButton((Rectangle){ 330, 572, 160, 54 }, "FLIP", COLOR_PURPLE, true, 22)) app.cardFlipped = !app.cardFlipped;
        if (DrawCandyButton((Rectangle){ 510, 572, 160, 54 }, "NOTES", COLOR_CYAN, true, 22)) { app.notesTab = 0; app.notesScroll = 0; }
        if (prev) { app.cardIndex = (app.cardIndex + count - 1) % count; app.cardFlipped = false; app.cardFlip = 0; PlayFx(FX_BLIP); }
        if (next) { app.cardIndex = (app.cardIndex + 1) % count; app.cardFlipped = false; app.cardFlip = 0; PlayFx(FX_BLIP); }
    }
}

// ================================================================ the two level maps
// MCQ = "Neon Skyline" (night city and a glowing highway). TYPING = "Circuit City" (a circuit board with traces).
static int MapCount(void) { return mapNodeCount[app.mode]; }
static Vector2 NodeP(int level) { return nodePos[app.mode][level]; }

static int CurrentLevelForMap(void) {
    int last = 0;
    for (int l = 0; l < MapCount(); l++) {
        if (!IsLevelUnlocked(app.mode, l)) continue;
        last = l;
        if (app.progress.bestGems[app.mode][l] == 0) return l;
    }
    return last;
}

static bool MapUnlocked(int level) { return IsLevelUnlocked(app.mode, level) && app.hopTo != level; }

static Vector2 RoadPoint(int seg, float f) {
    Vector2 a = NodeP(seg), b = NodeP(seg + 1);
    if (app.mode == MODE_MCQ) return V2(a.x + (b.x - a.x) * f, a.y + (b.y - a.y) * Smooth(f));
    // circuit trace: horizontal, vertical, horizontal
    float dx = fabsf(b.x - a.x) * 0.5f, dy = fabsf(b.y - a.y), d = f * (dx * 2 + dy);
    float sx = b.x > a.x ? 1.0f : -1.0f, sy = b.y > a.y ? 1.0f : -1.0f;
    if (d < dx) return V2(a.x + sx * d, a.y);
    d -= dx;
    if (d < dy) return V2(a.x + sx * dx, a.y + sy * d);
    d -= dy;
    return V2(a.x + sx * (dx + d), b.y);
}

// ---- backdrops
static void DrawSkylineLayer(int seed, float baseY, int minH, int maxH, Color col, float parallax, float lit) {
    float shift = (GetMousePosition().x - 500) * parallax;
    float t = (float)GetTime();
    for (int i = -1; i < 30; i++) {
        unsigned int h = Hash2(i, seed);
        int w = 34 + (int)(h % 30), height = minH + (int)((h >> 6) % (unsigned)(maxH - minH));
        float x = i * 38.0f + shift;
        DrawRectangle((int)x, (int)(baseY - height), w, height + 240, col);
        DrawRectangle((int)x, (int)(baseY - height), w, 2, Fade(COLOR_CYAN, 0.10f + lit * 0.2f));
        if (lit > 0)
            for (int wy = 10; wy < height - 6; wy += 11)
                for (int wx = 6; wx < w - 6; wx += 9) {
                    unsigned int wh = Hash2(i * 97 + wy, wx + seed);
                    if (wh % 6 == 0) DrawRectangle((int)x + wx, (int)(baseY - height) + wy, 3, 4, Fade(wh % 2 ? COLOR_CYAN : COLOR_PURPLE, lit * (0.5f + 0.4f * sinf(t * 0.7f + (float)(wh % 7)))));
                }
    }
}

static void DrawSkyBackdrop(void) {
    float t = (float)GetTime();
    DrawRectangleGradientV(0, 0, WINDOW_WIDTH, 440, (Color){ 8, 14, 46, 255 }, (Color){ 44, 34, 98, 255 });
    DrawRectangleGradientV(0, 440, WINDOW_WIDTH, 200, (Color){ 44, 34, 98, 255 }, (Color){ 8, 10, 34, 255 });
    for (int i = 0; i < SPARKLE_COUNT; i++) {
        float tw = 0.5f + 0.5f * sinf(t * 1.8f + i * 1.7f);
        if (sparklePos[i].y < 330) DrawRectangle((int)sparklePos[i].x, (int)sparklePos[i].y, 2, 2, Fade(WHITE, 0.15f + 0.45f * tw));
    }
    // ringed planet
    Vector2 pc = V2(835, 150);
    DrawCircleGradient((int)pc.x, (int)pc.y, 120, Fade(COLOR_PURPLE, 0.20f), Fade(COLOR_PURPLE, 0));
    DrawCircleV(pc, 52, (Color){ 52, 46, 128, 255 });
    DrawCircleSector(pc, 52, 200, 330, 24, (Color){ 74, 66, 168, 255 });
    DrawCircleSector(pc, 40, 20, 150, 24, (Color){ 40, 36, 104, 255 });
    DrawEllipseLines((int)pc.x, (int)pc.y + 4, 92, 16, Fade(COLOR_CYAN, 0.55f));
    DrawEllipseLines((int)pc.x, (int)pc.y + 4, 100, 19, Fade(COLOR_CYAN, 0.25f));
    // small moon
    DrawCircleV(V2(170, 120), 14, Fade(COLOR_TXT, 0.8f));
    DrawCircleV(V2(165, 116), 12, (Color){ 20, 26, 66, 120 });
    DrawSkylineLayer(11, 430, 90, 190, (Color){ 22, 28, 78, 255 }, 0.012f, 0.0f);
    DrawSkylineLayer(23, 485, 70, 160, (Color){ 15, 20, 62, 255 }, 0.026f, 0.5f);
    DrawSkylineLayer(37, 560, 40, 110, (Color){ 10, 14, 48, 255 }, 0.05f, 0.8f);
    // drifting drones
    for (int i = 0; i < 3; i++) {
        float x = fmodf(t * (22 + i * 9) + i * 330, WINDOW_WIDTH + 120) - 60, y = 150 + i * 62 + sinf(t * 1.3f + i) * 6;
        DrawRectangle((int)x - 8, (int)y, 16, 5, (Color){ 120, 140, 200, 255 });
        DrawLineEx(V2(x - 12, y - 3), V2(x - 3, y - 3), 2, Fade(COLOR_TXT, 0.7f));
        DrawLineEx(V2(x + 3, y - 3), V2(x + 12, y - 3), 2, Fade(COLOR_TXT, 0.7f));
        DrawCircleV(V2(x, y + 7), 2.5f, ((int)(t * 2 + i) % 2) ? COLOR_CYAN : Fade(COLOR_CYAN, 0.2f));
    }
    DrawRectangleGradientV(0, 520, WINDOW_WIDTH, 120, Fade(COLOR_BG_BOT, 0), Fade(COLOR_INK, 0.75f));
}

static void DrawCircuitBackdrop(void) {
    float t = (float)GetTime();
    DrawRectangleGradientV(0, 0, WINDOW_WIDTH, WINDOW_HEIGHT, (Color){ 6, 10, 30, 255 }, (Color){ 16, 16, 54, 255 });
    for (int x = 0; x < WINDOW_WIDTH; x += 40) DrawRectangle(x, 0, 1, WINDOW_HEIGHT, Fade(COLOR_CYAN, 0.045f));
    for (int y = 0; y < WINDOW_HEIGHT; y += 40) DrawRectangle(0, y, WINDOW_WIDTH, 1, Fade(COLOR_CYAN, 0.045f));
    static const Rectangle chips[] = { { 36, 92, 130, 76 }, { 424, 92, 150, 80 }, { 764, 452, 150, 92 }, { 236, 500, 120, 58 }, { 604, 92, 86, 58 }, { 930, 330, 60, 100 }, { 20, 250, 60, 90 } };
    static const char *labels[] = { "CPU", "RAM", "I/O", "GPU", "ROM", "NET", "SYS" };
    for (int i = 0; i < 7; i++) {
        Rectangle c = chips[i];
        DrawRoundOutlined(c, 8, 2, (Color){ 12, 18, 48, 255 }, Fade(COLOR_PURPLE, 0.5f));
        DrawRectangleLinesEx((Rectangle){ c.x + 10, c.y + 10, c.width - 20, c.height - 20 }, 1, Fade(COLOR_CYAN, 0.25f));
        for (float p = c.x + 10; p < c.x + c.width - 6; p += 12) { DrawRectangle((int)p, (int)c.y - 5, 4, 5, Fade(COLOR_CYAN, 0.3f)); DrawRectangle((int)p, (int)(c.y + c.height), 4, 5, Fade(COLOR_CYAN, 0.3f)); }
        DrawText(labels[i], (int)(c.x + c.width / 2) - MeasureText(labels[i], 14) / 2, (int)(c.y + c.height / 2) - 7, 14, Fade(COLOR_CYAN, 0.45f));
    }
    // background traces with moving data bits
    static const Vector2 traces[4][4] = {
        { {0, 200}, {260, 200}, {300, 240}, {300, 330} }, { {1000, 560}, {800, 560}, {760, 600}, {600, 600} },
        { {0, 600}, {170, 600}, {210, 560}, {210, 520} }, { {1000, 140}, {900, 140}, {870, 170}, {700, 170} },
    };
    for (int k = 0; k < 4; k++) {
        for (int i = 0; i < 3; i++) DrawLineEx(traces[k][i], traces[k][i + 1], 2, Fade(COLOR_PURPLE, 0.22f));
        DrawCircleV(traces[k][3], 4, Fade(COLOR_CYAN, 0.35f));
        float f = fmodf(t * 0.25f + k * 0.27f, 1.0f) * 3.0f;
        int seg = (int)f;
        Vector2 a = traces[k][seg], b = traces[k][seg + 1];
        DrawCircleV(V2(a.x + (b.x - a.x) * (f - seg), a.y + (b.y - a.y) * (f - seg)), 3, Fade(COLOR_CYAN, 0.7f));
    }
}

// ---- road / trace between level nodes
static void DrawRoad(void) {
    float t = (float)GetTime();
    bool sky = app.mode == MODE_MCQ;
    for (int pass = 0; pass < 4; pass++)
        for (int seg = 0; seg < MapCount() - 1; seg++) {
            bool open = MapUnlocked(seg + 1);
            Vector2 prev = RoadPoint(seg, 0);
            for (int i = 1; i <= 40; i++) {
                Vector2 p = RoadPoint(seg, i / 40.0f);
                float edgeW = sky ? 22.0f : 13.0f, bedW = sky ? 17.0f : 9.0f;
                if (pass == 0) { DrawLineEx(prev, p, 30, Fade(open ? COLOR_CYAN : COLOR_PURPLE, open ? 0.06f : 0.03f)); }
                else if (pass == 1) { Color c = open ? Fade(COLOR_CYAN, 0.6f) : (Color){ 58, 64, 118, 255 }; DrawLineEx(prev, p, edgeW, c); DrawCircleV(p, edgeW / 2, c); }
                else if (pass == 2) { DrawLineEx(prev, p, bedW, (Color){ 8, 12, 36, 255 }); DrawCircleV(p, bedW / 2, (Color){ 8, 12, 36, 255 }); }
                else if (i % 3 == 1) DrawLineEx(prev, p, 2, open ? Fade(COLOR_TXT, 0.5f) : Fade(COLOR_GRAY, 0.3f));
                prev = p;
            }
        }
    for (int seg = 0; seg < MapCount() - 1; seg++) {
        if (!MapUnlocked(seg + 1)) continue;
        for (int k = 0; k < 2; k++) {
            Vector2 p = RoadPoint(seg, fmodf(t * 0.3f + k * 0.5f + seg * 0.17f, 1.0f));
            DrawCircleV(p, 11, Fade(COLOR_CYAN, 0.22f));
            DrawCircleV(p, 4.5f, WHITE);
        }
    }
}

// ---- level node: hex badge on a glowing platform (MCQ) or a chip on a pad (typing)
static void DrawLevelNode(Vector2 c, int level, bool locked, int gems, bool isCurrent, bool hovered) {
    float t = (float)GetTime();
    bool boss = app.mode == MODE_TYPED && level == LEVEL_COUNT - 1;
    float r = boss ? 46.0f : 37.0f;
    float pop = EaseOutBack(Clampf((app.screenTime - 0.25f - level * 0.1f) / 0.45f, 0, 1));
    if (pop < 0.02f) return;
    if (hovered) r *= 1.07f;
    r *= pop;
    if (level == app.unlockLevel && app.unlockTime < 0.7f) r *= 0.45f + 0.55f * EaseOutBack(app.unlockTime / 0.55f);
    float bounce = isCurrent ? -fabsf(sinf(t * 4.0f)) * 8.0f : 0;
    Color col = locked ? (Color){ 84, 92, 138, 255 } : LevelColor(level);
    // platform
    float py = c.y + r + 8;
    DrawEllipse((int)c.x, (int)py + 7, r * 1.45f, r * 0.40f, Fade(COLOR_INK, 0.55f));
    DrawEllipse((int)c.x, (int)py + 2, r * 1.32f, r * 0.36f, locked ? (Color){ 26, 32, 72, 255 } : Mix(col, 0.32f));
    DrawEllipse((int)c.x, (int)py, r * 1.18f, r * 0.30f, Fade(col, locked ? 0.25f : 0.5f));
    DrawEllipseLines((int)c.x, (int)py, r * 1.18f, r * 0.30f, Fade(col, locked ? 0.4f : 0.9f));
    if (!locked) {
        DrawRectangleGradientV((int)c.x - 16, (int)(c.y - r - 150), 32, 150, Fade(col, 0), Fade(col, isCurrent ? 0.20f : 0.08f));
        DrawNgon(V2(c.x, c.y + bounce), 6, r + 15, 0, Fade(col, 0.10f));
    }
    c.y += bounce;
    if (isCurrent) DrawNgonLines(c, 6, r + 12 + sinf(t * 6) * 3, 0, 2, Fade(COLOR_TXT, 0.8f));
    if (app.mode == MODE_MCQ || boss) {
        DrawNgon(c, 6, r + 5, 0, COLOR_INK);
        DrawNgon(c, 6, r + 2, 0, col);
        DrawNgon(c, 6, r - 4, 0, COLOR_INK);
        DrawNgon(c, 6, r - 6, 0, Mix(col, locked ? 0.2f : (gems > 0 ? 0.5f : 0.28f)));
        DrawRectangle((int)(c.x - r * 0.45f), (int)(c.y - r * 0.62f), (int)(r * 0.9f), 2, Fade(WHITE, 0.25f));
    } else {
        Rectangle chip = { c.x - r, c.y - r, r * 2, r * 2 };
        for (float p = -r + 10; p < r - 6; p += 11) {   // pins
            DrawRectangle((int)(c.x + p), (int)(chip.y - 6), 5, 6, Fade(col, 0.7f));
            DrawRectangle((int)(c.x + p), (int)(chip.y + chip.height), 5, 6, Fade(col, 0.7f));
            DrawRectangle((int)(chip.x - 6), (int)(c.y + p), 6, 5, Fade(col, 0.7f));
            DrawRectangle((int)(chip.x + chip.width), (int)(c.y + p), 6, 5, Fade(col, 0.7f));
        }
        DrawChamfer(Expand(chip, 3), 14, COLOR_INK);
        DrawChamfer(Expand(chip, 1), 13, col);
        DrawChamfer(Expand(chip, -3), 11, COLOR_INK);
        DrawChamfer(Expand(chip, -5), 10, Mix(col, locked ? 0.2f : (gems > 0 ? 0.5f : 0.28f)));
    }
    if (pop < 0.9f) return;
    if (boss) { DrawStarShape(V2(c.x, c.y - r - 14), 15, 0, COLOR_INK); DrawStarShape(V2(c.x, c.y - r - 14), 11, 0, locked ? COLOR_GRAY : COLOR_YELLOW); }
    if (locked) DrawPadlock(V2(c.x, c.y + 2), boss ? 1.45f : 1.2f, COLOR_GRAY);
    else {
        char number[4];
        snprintf(number, sizeof number, "%d", level + 1);
        DrawTextCO(number, (int)c.x, (int)c.y - (boss ? 22 : 19), boss ? 46 : 40, WHITE, COLOR_INK);
    }
    if (!locked) DrawStarRow(c.x, c.y - bounce + r + 44, 11, gems, 99);
    // name plate
    const char *name = stageNames[app.mode][level];
    int w = MeasureText(name, 16) + 32;
    Rectangle plate = { c.x - w / 2.0f, c.y - bounce - r - (boss ? 62 : 48), (float)w, 28 };
    DrawRoundOutlined(plate, 12, 2, (Color){ 12, 18, 46, 255 }, locked ? (Color){ 64, 72, 124, 255 } : Fade(col, 0.9f));
    DrawTextCentered(name, (int)(plate.x + plate.width / 2), (int)plate.y + 6, 16, locked ? COLOR_GRAY : COLOR_TXT);
}

static void DrawLevelPopup(void) {
    int level = app.selectedLevel;
    bool unlocked = IsLevelUnlocked(app.mode, level);
    DrawRectangle(0, 0, WINDOW_WIDTH, WINDOW_HEIGHT, Fade(COLOR_INK, 0.72f * Smooth(app.popupTime / 0.2f)));
    Rectangle p = { 290, 130, 420, 400 };
    p.y += (1.0f - EaseOutBack(app.popupTime / 0.4f)) * -480.0f;
    DrawRound((Rectangle){ p.x + 4, p.y + 12, p.width, p.height }, 26, Fade(BLACK, 0.3f));
    DrawRoundOutlined(p, 26, 4, COLOR_CREAM, COLOR_INK);
    Rectangle ribbon = { p.x - 26, p.y - 30, p.width + 52, 64 };
    DrawRoundOutlined(ribbon, 18, 3, Mix(LevelColor(level), 0.7f), LevelColor(level));
    char title[24];
    snprintf(title, sizeof title, "LEVEL %d", level + 1);
    DrawTextCO(title, (int)(p.x + p.width / 2), (int)ribbon.y + 14, 36, WHITE, COLOR_INK);
    DrawTextCentered(stageNames[app.mode][level], (int)(p.x + p.width / 2), (int)p.y + 52, 24, COLOR_CYAN);
    DrawTextCentered(levelNames[level], (int)(p.x + p.width / 2), (int)p.y + 86, 18, COLOR_TXT);
    WrapText(levelBlurbs[level], (int)p.x + 34, (int)p.y + 118, (int)p.width - 68, 17, COLOR_DIM, true);
    DrawStarRow(p.x + p.width / 2, p.y + 206, 26, app.progress.bestGems[app.mode][level], 99);
    int count = 0;
    for (int i = 0; i < QUESTION_BANK_SIZE; i++)
        if (QuestionAt(i)->levelIndex == level && (app.mode == MODE_MCQ) == (QuestionAt(i)->kind == KIND_MCQ)) count++;
    char info[80];
    snprintf(info, sizeof info, "%s  -  %d questions  -  3 stars = all right", app.mode == MODE_MCQ ? "MCQ" : "TYPING", count);
    DrawTextCentered(info, (int)(p.x + p.width / 2), (int)p.y + 254, 14, COLOR_DIM);
    const char *label = app.progress.bestGems[app.mode][level] > 0 ? "REPLAY" : "PLAY";
    if (DrawCandyButton((Rectangle){ p.x + 90, p.y + 282, 240, 64 }, label, COLOR_GREEN, unlocked, 32) || (unlocked && IsKeyPressed(KEY_ENTER))) StartLevel(level);
    if (DrawCandyButton((Rectangle){ p.x + 90, p.y + 340, 240, 44 }, "STUDY NOTES FIRST", COLOR_CYAN, true, 16)) {
        app.notesLevel = level; app.notesTab = 0; app.notesScroll = 0; app.selectedLevel = -1;
        SetScreen(SCREEN_NOTES);
        return;
    }
    Rectangle closeButton = { p.x + p.width - 44, p.y - 40, 52, 48 };
    bool closed = DrawCandyButton(closeButton, "", COLOR_RED, true, 24);
    DrawXMark(closeButton);
    if (closed || IsKeyPressed(KEY_ESCAPE)) app.selectedLevel = -1;
    DrawBot(180, 560, 1.1f, MOOD_HAPPY);
}

static void ScreenMap(void) {
    float t = (float)GetTime(), dt = GetFrameTime();
    // robot hops to the level that just unlocked, then (optionally) its popup opens
    if (app.pendingHop >= 0 && app.hopTo < 0) { app.hopFrom = app.pendingHop; app.hopTo = app.hopFrom + 1; app.hopTime = -0.7f; app.pendingHop = -1; }
    if (app.hopTo >= 0) {
        app.hopTime += dt;
        static int lastStep;
        int step = app.hopTime > 0 ? (int)(Clampf(app.hopTime / 1.5f, 0, 1) * 4) : 0;
        if (step != lastStep && step > 0 && step < 4) PlayFx(FX_HOP);
        lastStep = step;
        if (app.hopTime >= 1.5f) { PlayFx(FX_UNLOCK); SpawnBurst(NodeP(app.hopTo), 60); app.unlockLevel = app.hopTo; app.unlockTime = 0; app.hopTo = -1; }
    }
    if (app.unlockLevel >= 0) { app.unlockTime += dt; if (app.unlockTime > 2.4f) app.unlockLevel = -1; }
    if (app.selectedLevel >= 0) app.popupTime += dt;
    if (app.autoOpenLevel >= 0 && app.hopTo < 0 && app.pendingHop < 0 && app.screenTime > 0.6f && (app.unlockLevel < 0 || app.unlockTime > 1.2f)) {
        if (app.autoOpenLevel < MapCount() && IsLevelUnlocked(app.mode, app.autoOpenLevel)) { app.selectedLevel = app.autoOpenLevel; app.popupTime = 0; PlayFx(FX_POP); }
        app.autoOpenLevel = -1;
    }

    if (app.mode == MODE_MCQ) DrawSkyBackdrop(); else DrawCircuitBackdrop();
    if (app.mode == MODE_MCQ) {   // glowing gate at the end of the skyline road
        Vector2 g = V2(925, 372);
        DrawRing(g, 26, 31, 0, 360, 36, Fade(COLOR_CYAN, 0.55f + 0.2f * sinf(t * 2)));
        DrawRing(g, 14, 17, 0, 360, 28, Fade(COLOR_PURPLE, 0.7f));
        DrawCircleV(g, 9, Fade(COLOR_TXT, 0.35f + 0.25f * sinf(t * 3)));
        DrawTextCentered("TYPING MAP", (int)g.x, (int)g.y + 40, 13, Fade(COLOR_TXT, 0.7f));
    }

    uiLocked = app.selectedLevel >= 0 || app.hopTo >= 0 || app.nameModal;
    DrawRoad();
    int current = CurrentLevelForMap();
    int hoveredLevel = -1;
    for (int level = 0; level < MapCount(); level++) {
        bool unlocked = IsLevelUnlocked(app.mode, level);
        bool hovered = !uiLocked && CheckCollisionPointCircle(GetMousePosition(), NodeP(level), level == LEVEL_COUNT - 1 && app.mode == MODE_TYPED ? 50 : 42);
        if (hovered) hoveredLevel = level;
        DrawLevelNode(NodeP(level), level, !MapUnlocked(level), app.progress.bestGems[app.mode][level], level == current && MapUnlocked(level), hovered);
        if (TakeClick(hovered)) {
            if (!unlocked) { char msg[96]; snprintf(msg, sizeof msg, "Earn a star on %s first!", stageNames[app.mode][level - 1]); ShowToast(msg); }
            else { app.selectedLevel = level; app.popupTime = 0; PlayFx(FX_POP); }
        }
    }
    (void)hoveredLevel;
    if (app.unlockLevel >= 0 && app.unlockTime < 1.0f) {
        float radius = 40 + app.unlockTime * 170;
        DrawRing(NodeP(app.unlockLevel), radius, radius + 5, 0, 360, 48, Fade(LevelColor(app.unlockLevel), 1.0f - app.unlockTime));
    }
    Vector2 botFoot = V2(NodeP(current).x - 78, NodeP(current).y + 44);
    BotMood botMood = MOOD_HAPPY;
    if (app.hopTo >= 0) {
        float f = Clampf(app.hopTime / 1.5f, 0, 1);
        Vector2 p = RoadPoint(app.hopFrom, f);
        float lift = (f > 0 && f < 1) ? fabsf(sinf(f * PI * 4)) * 32 : 0;
        botFoot = V2(p.x - 78, p.y + 44 - lift);
        botMood = f > 0 ? MOOD_CHEER : MOOD_HAPPY;
    }
    DrawBot(botFoot.x, botFoot.y, 0.62f, botMood);

    DrawHeaderBar(app.mode == MODE_MCQ ? "NEON SKYLINE" : "CIRCUIT CITY", 122, true);
    if (DrawCandyButton((Rectangle){ 12, 8, 100, 44 }, "BACK", COLOR_ORANGE, true, 20)) SetScreen(SCREEN_HUB);
    DrawText(app.mode == MODE_MCQ ? "MCQ MAP" : "TYPING MAP", 470, 22, 18, COLOR_DIM);

    int weakIds[MAX_QUESTIONS_PER_LEVEL];
    int weakCount = CollectWeak(weakIds, MAX_QUESTIONS_PER_LEVEL);
    char weakLabel[40];
    snprintf(weakLabel, sizeof weakLabel, weakCount ? "WEAK SPOTS (%d)" : "WEAK SPOTS", weakCount);
    if (DrawCandyButton((Rectangle){ 25, 572, 250, 54 }, weakLabel, COLOR_CYAN, true, 20)) StartPractice();
    DrawBoosterTray((Rectangle){ 300, 576, 400, 48 }, true);
    if (DrawCandyButton((Rectangle){ 725, 572, 250, 54 }, "MY PROGRESS", COLOR_PURPLE, true, 22)) {
        app.reviewTab = 0;
        app.reviewCount = 0;
        SetScreen(SCREEN_REVIEW);
    }
    if (app.unlockLevel >= 0) {
        float a = Smooth(app.unlockTime / 0.3f) * (1.0f - Smooth((app.unlockTime - 2.0f) / 0.4f));
        Rectangle banner = { 290, 84 - 70 * (1 - a), 420, 50 };
        char text[64];
        snprintf(text, sizeof text, "LEVEL %d UNLOCKED!", app.unlockLevel + 1);
        DrawRoundOutlined(banner, 16, 3, COLOR_CREAM, COLOR_CYAN);
        DrawTextCO(text, 500, (int)banner.y + 12, 26, COLOR_CYAN, COLOR_INK);
    }
    if (IsKeyPressed(KEY_ESCAPE) && app.selectedLevel < 0 && app.hopTo < 0) SetScreen(SCREEN_HUB);

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
        default:         return (Color){70, 84, 140, 255};
    }
}

static void DrawQuestionPips(void) {
    float spacing = 40, startX = 545 - (app.questionCount - 1) * spacing / 2;
    for (int i = 0; i < app.questionCount; i++) {
        Vector2 c = V2(startX + i * spacing, 29);
        bool isCurrent = i == app.current;
        bool hovered = !uiLocked && CheckCollisionPointCircle(GetMousePosition(), c, 17);
        float r = (isCurrent ? 16.0f + sinf((float)GetTime() * 5) * 1.2f : 13.0f) + (hovered ? 2 : 0);
        if (isCurrent) DrawRing(c, r + 4, r + 7, 0, 360, 30, Fade(WHITE, 0.8f));
        DrawCircleV(c, r + 3, COLOR_INK);
        DrawCircleV(c, r, StatusColor(app.qStatus[i]));
        char number[4];
        snprintf(number, sizeof number, "%d", i + 1);
        DrawTextCentered(number, (int)c.x, (int)c.y - 8, 18, app.qStatus[i] == ST_UNSEEN ? COLOR_TXT : COLOR_INK);
        if (TakeClick(hovered) && i != app.current) GoToQuestion(i);
    }
}

static void DrawFeedbackSheet(void) {
    app.sheetTime += GetFrameTime();
    float h = 252, a = Smooth(app.sheetTime / 0.25f);
    float top = WINDOW_HEIGHT - h * a;
    bool correct = app.lastAnswerCorrect;
    Color base = correct ? (Color){8, 40, 46, 255} : (Color){42, 16, 52, 255};
    bool shielded = strncmp(app.feedbackHead, "Shield", 6) == 0;
    Color edge = correct ? COLOR_GREEN : (shielded ? COLOR_PURPLE : COLOR_RED);
    DrawRoundOutlined((Rectangle){ -12, top, WINDOW_WIDTH + 24, h + 40 }, 30, 5, base, edge);
    DrawRound((Rectangle){ 30, top + 9, 300, 6 }, 3, Fade(edge, 0.7f));
    DrawTextO(app.feedbackHead, 40, (int)top + 26, 44, edge, COLOR_INK);
    const char *text = (!correct && app.revealed) ? app.revealText : app.feedbackText;
    textMode = 1;
    WrapText(text, 40, (int)top + 84, 700, 20, COLOR_TXT, true);
    textMode = 0;

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
    DrawTextCentered("Leave this level?", 500, 216, 32, COLOR_TXT);
    WrapText("Your answers in this run will be lost, but your lifetime stats are kept.", 330, 266, 340, 18, COLOR_DIM, true);
    if (DrawCandyButton((Rectangle){ 322, 346, 170, 58 }, "STAY", COLOR_GREEN, true, 26) || IsKeyPressed(KEY_ESCAPE)) app.confirmQuit = false;
    if (DrawCandyButton((Rectangle){ 508, 346, 170, 58 }, "LEAVE", COLOR_RED, true, 26)) { app.confirmQuit = false; SetScreen(SCREEN_MAP); }
}

static void DrawLevelIntro(void) {
    const float duration = 2.1f;
    float e = duration - app.introTime;
    float k = Smooth(e / 0.35f) * (1.0f - Smooth((e - (duration - 0.4f)) / 0.4f));
    if (k <= 0.01f) return;
    float t = (float)GetTime();
    DrawRectangle(0, 0, WINDOW_WIDTH, WINDOW_HEIGHT, Fade(COLOR_INK, 0.62f * k));
    float h = 240 * k, y = 320 - h / 2;
    DrawRectangle(0, (int)y, WINDOW_WIDTH, (int)h, (Color){6, 6, 34, 248});
    DrawRectangle(0, (int)y - 3, WINDOW_WIDTH, 3, COLOR_CYAN);
    DrawRectangle(0, (int)(y + h), WINDOW_WIDTH, 3, COLOR_CYAN);
    DrawRectangle(0, (int)(y + h) + 3, WINDOW_WIDTH, 8, Fade(COLOR_CYAN, 0.18f));
    for (int x = -40 + (int)fmodf(t * 80, 40); x < WINDOW_WIDTH; x += 40) {
        DrawRectangle(x, (int)y + 6, 20, 5, Fade(COLOR_YELLOW, 0.85f));
        DrawRectangle(WINDOW_WIDTH - x - 20, (int)(y + h) - 11, 20, 5, Fade(COLOR_YELLOW, 0.85f));
    }
    if (k < 0.85f) return;
    float slideIn = (1.0f - Smooth((e - 0.15f) / 0.4f)) * -600.0f;
    float slideOut = Smooth((e - (duration - 0.45f)) / 0.45f) * 700.0f;
    int ox = (int)(slideIn + slideOut);
    char title[40], info[80];
    if (app.levelIndex >= 0) snprintf(title, sizeof title, "LEVEL %d", app.levelIndex + 1); else snprintf(title, sizeof title, "PRACTICE");
    DrawText(title, 60 + ox - 4, (int)y + 34, 84, Fade(COLOR_CYAN, 0.7f));
    DrawText(title, 60 + ox + 4, (int)y + 34, 84, Fade(COLOR_PURPLE, 0.7f));
    DrawTextO(title, 60 + ox, (int)y + 34, 84, WHITE, COLOR_INK);
    DrawText(app.levelIndex >= 0 ? stageNames[app.mode][app.levelIndex] : "Weak spot mission", 64 + ox * 2, (int)y + 124, 30, COLOR_CYAN);
    if (app.levelIndex >= 0) snprintf(info, sizeof info, "%s  //  %d QUESTIONS", levelNames[app.levelIndex], app.questionCount);
    else snprintf(info, sizeof info, "%d QUESTIONS  //  HALF RIGHT = 1 STAR", app.questionCount);
    DrawText(info, 64 + ox * 3, (int)y + 168, 18, COLOR_YELLOW);
    if ((int)(t * 3) % 2 == 0) DrawText("> READY?", 64 + ox * 3, (int)y + 198, 20, COLOR_GREEN);
    DrawBot(850 - ox, y + h - 8, 1.5f, MOOD_CHEER);
}

static void DrawOptionRow(Rectangle r, int option, const char *text, int state, bool hovered) {
    // state: 0 normal, 1 right, 2 wrong, 3 removed
    Color border = { 64, 76, 140, 255 }, fill = hovered ? (Color){ 26, 38, 82, 255 } : (Color){ 16, 24, 58, 255 }, badge = COLOR_CYAN;
    if (state == 1) { border = COLOR_GREEN; fill = (Color){ 12, 52, 54, 255 }; badge = COLOR_GREEN; }
    else if (state == 2) { border = COLOR_RED; fill = (Color){ 56, 20, 58, 255 }; badge = COLOR_RED; }
    else if (state == 3) { border = (Color){ 40, 46, 86, 255 }; fill = (Color){ 12, 16, 40, 255 }; badge = COLOR_GRAY; }
    else if (hovered) border = COLOR_CYAN;
    DrawRoundOutlined(r, 14, 2, fill, border);
    float cy = r.y + r.height / 2;
    DrawCircleV(V2(r.x + 30, cy), 16, badge);
    DrawCircleV(V2(r.x + 30, cy), 13, COLOR_CODE_BG);
    char letter[2] = { (char)('A' + option), 0 };
    DrawTextCentered(letter, (int)r.x + 30, (int)cy - 8, 18, state == 3 ? COLOR_GRAY : COLOR_TXT);
    textMode = 1;
    TextDraw(text, (int)r.x + 62, (int)cy - 12, 23, state == 3 ? COLOR_GRAY : COLOR_TXT);
    if (state == 3) DrawLineEx(V2(r.x + 62, cy + 1), V2(r.x + 62 + (float)TextW(text, 23), cy + 1), 2, COLOR_GRAY);
    textMode = 0;
}

static void ScreenQuestion(void) {
    const Question *question = CurrentQuestion();
    float frameTime = GetFrameTime();
    app.qTime += frameTime;
    if (app.introTime > 0 && app.screenTime > 0.3f) app.introTime -= frameTime;
    bool blocked = app.sheetOpen || app.confirmQuit || app.runCountdown > 0 || app.introTime > 0;
    uiLocked = blocked;
    DrawBackground(0.34f);

    // ---- prompt card and snippet (plain reading font)
    int x = 30 + (int)((1.0f - Smooth(app.qTime / 0.28f)) * 90.0f), width = 740, y = 98;
    textMode = 1;
    int textEnd = WrapText(question->prompt, x + 22, y + 14, width - 44, 23, COLOR_TXT, false);
    Rectangle card = { (float)x, (float)y, (float)width, (float)(textEnd - (y + 14) + 22) };
    DrawRound((Rectangle){ card.x + 3, card.y + 8, card.width, card.height }, 22, Fade(BLACK, 0.2f));
    DrawRoundOutlined(card, 22, 3, COLOR_CREAM, COLOR_INK);
    WrapText(question->prompt, x + 22, y + 14, width - 44, 23, COLOR_TXT, true);
    textMode = 0;
    int contentY = (int)(card.y + card.height) + 14;
    if (question->snippet[0]) {
        Rectangle code = { (float)x, (float)contentY, (float)width, (float)(CountLines(question->snippet) * 26 + 22) };
        DrawRoundOutlined(code, 16, 3, COLOR_CODE_BG, COLOR_INK);
        DrawCodeBlock(question->snippet, x + 22, contentY + 11, 20, COLOR_CODE_TXT);
        contentY += (int)code.height + 16;
    }

    Vector2 popAt = GetMousePosition();
    if (question->kind == KIND_MCQ) {
        int chosen = -1;
        for (int option = 0; option < 4; option++) {
            Rectangle r = { (float)x, (float)(contentY + option * 58), (float)width, 50 };
            r.x += (1.0f - Smooth((app.qTime - 0.12f - option * 0.07f) / 0.28f)) * 160.0f;   // answers slide in one by one
            int state = app.eliminated[option] ? 3 : 0;
            if (app.sheetOpen) {
                bool showRight = app.lastAnswerCorrect || app.revealed;
                if (option == app.lastChoice && !app.lastAnswerCorrect) state = 2;
                else if (showRight && option == question->correctOption) state = 1;
            }
            bool hovered = !blocked && state != 3 && HoverRect(r);
            DrawOptionRow(r, option, question->options[option], state, hovered);
            if (TakeClick(hovered) && !blocked) chosen = option;
            if (!blocked && IsKeyPressed(KEY_ONE + option) && state != 3) chosen = option;
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
        textMode = 1;
        TextDraw("Type your answer, then press ENTER", x, contentY, 22, COLOR_DIM);
        if (!blocked) HandleTextInput(app.typedAnswer, &app.typedLength, sizeof app.typedAnswer);
        Rectangle box = { (float)x, (float)contentY + 38, (float)width, 64 };
        DrawRoundOutlined(box, 18, 3, COLOR_CODE_BG, (Color){ 64, 76, 140, 255 });
        char shown[110];
        snprintf(shown, sizeof shown, "%s%s", app.typedAnswer, (int)(GetTime() * 2) % 2 == 0 && !blocked ? "_" : "");
        textMode = 2;
        TextDraw(shown, x + 22, contentY + 38 + 16, 30, COLOR_TXT);
        textMode = 0;
    } else {
        if (!blocked) HandleCodeInput();
        Rectangle editor = { (float)x, (float)contentY, (float)width, (float)(564 - contentY) };
        DrawRoundOutlined(editor, 18, 3, COLOR_CODE_BG, COLOR_INK);
        DrawRound((Rectangle){ editor.x, editor.y, editor.width, 30 }, 18, (Color){ 22, 32, 84, 255 });
        DrawRectangle((int)editor.x, (int)editor.y + 16, (int)editor.width, 14, (Color){ 22, 32, 84, 255 });
        DrawCircleV(V2(editor.x + 20, editor.y + 15), 5, COLOR_RED);
        DrawCircleV(V2(editor.x + 38, editor.y + 15), 5, COLOR_YELLOW);
        DrawCircleV(V2(editor.x + 56, editor.y + 15), 5, COLOR_GREEN);
        DrawText("answer.c", (int)editor.x + 80, (int)editor.y + 7, 16, COLOR_GRAY);
        int lineY = (int)editor.y + 40, caretLine = 0, caretCol = 0;
        for (int i = 0; i < app.codeCursor; i++) { if (app.codeAnswer[i] == '\n') { caretLine++; caretCol = 0; } else caretCol++; }
        BeginScissorMode((int)editor.x + 4, (int)editor.y + 32, (int)editor.width - 8, (int)editor.height - 40);
        int lines = CountLines(app.codeAnswer);
        textMode = 2;
        for (int i = 0; i < lines; i++) {
            char number[8];
            snprintf(number, sizeof number, "%d", i + 1);
            TextDraw(number, (int)editor.x + 14, lineY + i * 26, 20, (Color){ 90, 104, 164, 255 });
        }
        textMode = 0;
        DrawCodeBlock(app.codeAnswer, (int)editor.x + 52, lineY, 20, COLOR_CODE_TXT);
        const char *lineStart = app.codeAnswer;
        for (int i = 0; i < caretLine; i++) { lineStart = strchr(lineStart, '\n'); lineStart = lineStart ? lineStart + 1 : app.codeAnswer + app.codeLength; }
        char prefix[160];
        int prefixLength = caretCol < 159 ? caretCol : 159;
        memcpy(prefix, lineStart, (size_t)prefixLength);
        prefix[prefixLength] = '\0';
        textMode = 2;
        int caretX = (int)editor.x + 52 + TextW(prefix, 20);
        textMode = 0;
        if ((int)(GetTime() * 2) % 2 == 0 && !blocked) DrawRectangle(caretX, lineY + caretLine * 26, 3, 22, COLOR_YELLOW);
        EndScissorMode();
    }

    // ---- side column: Byte, speech bubble and info chips
    char bubbleText[200];
    if (question->kind == KIND_SHORT && app.used5050[app.current]) {
        char first[100];
        int n = 0;
        while (question->acceptedAnswers[n] && question->acceptedAnswers[n] != '|' && n < 99) { first[n] = question->acceptedAnswers[n]; n++; }
        first[n] = '\0';
        snprintf(bubbleText, sizeof bubbleText, "Starts with \"%c\" and has %d characters.", first[0], n);
    }
    else if (app.qHint[app.current]) snprintf(bubbleText, sizeof bubbleText, "Hint: %s", question->hint);
    else if (question->kind == KIND_MCQ) snprintf(bubbleText, sizeof bubbleText, "Tap an answer! Stuck? Ask me for a HINT.");
    else if (question->kind == KIND_SHORT) snprintf(bubbleText, sizeof bubbleText, "Type it in and press ENTER!");
    else snprintf(bubbleText, sizeof bubbleText, "Write the function. CTRL+ENTER runs the tests!");
    textMode = 1;
    int bubbleBottom = WrapText(bubbleText, 0, 0, 161, 16, COLOR_TXT, false);
    DrawBubble((Rectangle){ 790, 98, 185, (float)(bubbleBottom + 14) }, bubbleText, 16, 880);
    textMode = 0;
    if (!app.sheetOpen) DrawBot(882, 424, 1.15f, MOOD_THINK);
    if (!app.sheetOpen) {
        char info[80];
        Rectangle chip = { 790, 440, 185, 34 };
        DrawRoundOutlined(chip, 17, 3, COLOR_CREAM, COLOR_INK);
        char fitted[48];
        FitText(app.levelIndex >= 0 ? stageNames[app.mode][app.levelIndex] : "Weak spot practice", fitted, sizeof fitted, 165, 16);
        DrawTextCentered(fitted, 882, 449, 16, COLOR_TXT);
        snprintf(info, sizeof info, "Question %d of %d", app.current + 1, app.questionCount);
        DrawTextCentered(info, 882, 484, 18, COLOR_TXT);
        float chipX = 790;
        if (app.shieldOn) { DrawRoundOutlined((Rectangle){ chipX, 510, 90, 24 }, 12, 2, (Color){ 30, 22, 70, 255 }, COLOR_PURPLE); DrawTextCentered("SHIELD ON", (int)chipX + 45, 516, 13, COLOR_PURPLE); chipX += 95; }
        if (app.doubleOn) { DrawRoundOutlined((Rectangle){ chipX, 510, 90, 24 }, 12, 2, (Color){ 20, 26, 80, 255 }, COLOR_ORANGE); DrawTextCentered("2x HP ON", (int)chipX + 45, 516, 13, COLOR_ORANGE); }
        if (app.streak > 1) {
            snprintf(info, sizeof info, "STREAK x%d", app.streak);
            DrawRoundOutlined((Rectangle){ 800, 538, 165, 30 }, 15, 2, (Color){ 22, 40, 84, 255 }, COLOR_YELLOW);
            DrawTextCentered(info, 882, 545, 16, COLOR_YELLOW);
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
    if ((hintClicked || (!blocked && IsKeyPressed(KEY_F1))) && !app.qHint[app.current]) { app.qHint[app.current] = true; SpawnBurst(V2(880, 140), 14); PlayFx(FX_BLIP); }
    bool others = NextUnresolved(app.current) >= 0;
    const char *skipLabel = !others ? "FINISH" : (IsResolved(app.qStatus[app.current]) ? "NEXT" : "SKIP");
    if (DrawCandyButton((Rectangle){ 310, 572, 140, 54 }, skipLabel, others ? COLOR_CYAN : COLOR_GREEN, true, 22)) SkipOrNext();

    // boosters earned at HP milestones
    {
        const char *tip = NULL;
        Color boostCol[BOOST_COUNT] = { COLOR_CYAN, COLOR_PURPLE, COLOR_ORANGE };
        for (int b = 0; b < BOOST_COUNT; b++) {
            Rectangle r = { 462 + b * 106.0f, 572, 100, 54 };
            char label[24];
            bool on = (b == BOOST_SHIELD && app.shieldOn) || (b == BOOST_DOUBLE && app.doubleOn);
            bool usable = app.progress.boost[b] > 0 && !on && !(b == BOOST_FIFTY && app.used5050[app.current]) && !app.sheetOpen;
            if (on) snprintf(label, sizeof label, "%s ON", boostShort[b]);
            else snprintf(label, sizeof label, "%s x%d", boostShort[b], app.progress.boost[b]);
            if (HoverRect(r)) tip = boostLong[b];
            if (DrawCandyButton(r, label, on ? COLOR_GREEN : boostCol[b], usable, 15) && !blocked) UseBooster(b);
        }
        if (tip && !app.sheetOpen) {
            Rectangle t = { 330, 534, 450, 28 };
            DrawRoundOutlined(t, 10, 2, COLOR_CREAM, COLOR_NEON);
            char fitted[160];
            FitText(tip, fitted, sizeof fitted, (int)t.width - 20, 13);
            DrawText(fitted, (int)t.x + 10, (int)t.y + 8, 13, COLOR_TXT);
        }
    }

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
    if (app.introTime > 0) {
        uiLocked = false;
        if (app.introTime > 0.45f && (IsMouseButtonPressed(MOUSE_BUTTON_LEFT) || IsKeyPressed(KEY_ENTER) || IsKeyPressed(KEY_SPACE))) app.introTime = 0.45f;
        DrawLevelIntro();
        return;
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
    if (pass && t < 2.0f && (int)(t * 60) % 4 == 0) SpawnConfetti(2);
    static float prevT;
    if (t < prevT) prevT = 0;
    for (int i = 0; i < app.levelGems && !practice; i++) {
        float when = 0.3f + i * 0.35f + 0.17f;
        if (prevT < when && t >= when) { SetSoundPitch(fxSounds[FX_STAR], 1.0f + i * 0.26f); PlayFx(FX_STAR); }
    }
    prevT = t;

    Rectangle card = { 250, 96, 500, 530 };
    DrawRound((Rectangle){ card.x + 4, card.y + 12, card.width, card.height }, 30, Fade(BLACK, 0.28f));
    DrawRoundOutlined(card, 30, 5, COLOR_CREAM, COLOR_INK);
    Rectangle ribbon = { card.x - 30, card.y - 34, card.width + 60, 70 };
    DrawRoundOutlined(ribbon, 20, 4, pass ? COLOR_GREEN : COLOR_RED, COLOR_INK);
    DrawRound((Rectangle){ ribbon.x + 24, ribbon.y + 7, ribbon.width - 48, 15 }, 7, Fade(WHITE, 0.3f));
    DrawTextCO(practice ? "PRACTICE DONE!" : (pass ? "LEVEL CLEAR!" : "OH NO! TRY AGAIN"), 500, (int)ribbon.y + 16, 40, WHITE, COLOR_INK);

    char line[96];
    if (practice) snprintf(line, sizeof line, "Weak spot practice");
    else snprintf(line, sizeof line, "Level %d - %s", app.levelIndex + 1, stageNames[app.mode][app.levelIndex]);
    DrawTextCentered(line, 500, (int)card.y + 50, 22, COLOR_DIM);
    if (!practice) DrawStarRow(500, card.y + 128, 36, app.levelGems, t - 0.3f);
    snprintf(line, sizeof line, "SCORE %d / %d      +%d HP", app.correctCount, app.questionCount, (int)(app.levelXp * Smooth((t - 0.3f) / 1.2f)));
    DrawTextCentered(line, 500, (int)card.y + (practice ? 110 : 190), 28, COLOR_TXT);
    if (app.bonusHp > 0 && t > 1.0f) {
        snprintf(line, sizeof line, "includes +%d HP level bonus", app.bonusHp);
        DrawTextCentered(line, 500, (int)card.y + 226, 16, COLOR_CYAN);
    }

    float spacing = 46, startX = 500 - (app.questionCount - 1) * spacing / 2;
    int mistakes = 0;
    for (int i = 0; i < app.questionCount; i++) {
        Vector2 c = V2(startX + i * spacing, card.y + 270);
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
    WrapText(message, (int)card.x + 40, (int)card.y + 306, 420, 18, COLOR_DIM, true);
    if (app.fixedCount > 0) {
        snprintf(line, sizeof line, "You fixed %d mistake%s on a retry!", app.fixedCount, app.fixedCount == 1 ? "" : "s");
        DrawTextCentered(line, 500, (int)card.y + 352, 18, COLOR_CYAN);
    }
    if (app.rewardText[0]) {
        float pulse = 0.5f + 0.5f * sinf(t * 5);
        Rectangle badge = { card.x + 30, card.y + 378, card.width - 60, 30 };
        DrawRoundOutlined(badge, 15, 2, (Color){ 30, 22, 70, 255 }, Fade(COLOR_PURPLE, 0.6f + 0.4f * pulse));
        char fitted[96];
        FitText(app.rewardText, fitted, sizeof fitted, (int)badge.width - 20, 16);
        DrawTextCentered(fitted, 500, (int)badge.y + 7, 16, COLOR_TXT);
    }

    if (DrawCandyButton((Rectangle){ card.x + 40, card.y + 416, 420, 48 }, mistakes ? "REVIEW MY MISTAKES" : "OPEN MY PROGRESS", COLOR_CYAN, true, 24)) {
        app.reviewTab = mistakes ? 2 : 0;
        app.reviewCount = 0;
        if (mistakes) { app.reviewCount = app.lastRunCount; for (int i = 0; i < app.lastRunCount; i++) { app.reviewIds[i] = app.lastRunIds[i]; app.reviewStatus[i] = app.lastRunStatus[i]; } app.reviewExpanded = -1; app.reviewScroll = 0; }
        SetScreen(SCREEN_REVIEW);
        return;
    }
    int nextLevel = app.levelIndex + 1;
    bool nextReady = !practice && nextLevel < LEVEL_COUNT && IsLevelUnlocked(app.mode, nextLevel);
    float by = card.y + 470;
    if (DrawCandyButton((Rectangle){ card.x + 25, by, 140, 54 }, "MAP", COLOR_PURPLE, true, 22)) { SetScreen(SCREEN_MAP); return; }
    if (DrawCandyButton((Rectangle){ card.x + 180, by, 140, 54 }, practice ? "AGAIN" : "REPLAY", COLOR_ORANGE, true, 22)) {
        if (practice) StartPractice(); else StartLevel(app.levelIndex);
        return;
    }
    if (DrawCandyButton((Rectangle){ card.x + 335, by, 140, 54 }, "NEXT", COLOR_GREEN, nextReady, 22)) { app.autoOpenLevel = nextLevel; SetScreen(SCREEN_MAP); return; }

    DrawBot(125, 572, 1.7f, pass ? MOOD_CHEER : MOOD_SAD);
    DrawLollipop(V2(880, 420), 34, COLOR_CYAN, COLOR_CREAM, -0.2f);
    DrawCupcake(V2(880, 612), 0.9f);
}

// ---------------------------------------------------------------- progress report
static int DrawQuestionDetail(const Question *q, int x, int y, int w, bool draw) {
    if (q->snippet[0]) {
        int h = CountLines(q->snippet) * 24 + 16;
        if (draw) {
            DrawRound((Rectangle){ (float)x, (float)y, (float)w, (float)h }, 10, COLOR_CODE_BG);
            DrawCodeBlock(q->snippet, x + 12, y + 8, 18, COLOR_CODE_TXT);
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
    textMode = 1;
    if (answer[0]) y = WrapText(answer, x, y, w, 20, COLOR_GREEN, draw);
    y = WrapText(q->explanation, x, y, w, 19, COLOR_TXT, draw);
    textMode = 0;
    return y;
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
    DrawCircleV(V2(x, y), 12, value > 0 ? color : (Color){50, 48, 96, 255});
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
    DrawRing(ring, 56, 80, 0, 360, 60, (Color){34, 32, 84, 255});
    if (totalAttempts) DrawRing(ring, 56, 80, 270, 270 + 360.0f * percent / 100.0f * Smooth((app.screenTime - 0.2f) / 0.8f), 60, percent >= 80 ? COLOR_GREEN : (percent >= 50 ? COLOR_YELLOW : COLOR_RED));
    char text[64];
    snprintf(text, sizeof text, totalAttempts ? "%d%%" : "--", percent);
    DrawTextCentered(text, (int)ring.x, (int)ring.y - 20, 38, COLOR_TXT);
    DrawTextCentered("correct", (int)ring.x, (int)ring.y + 18, 16, COLOR_DIM);
    int stars = TotalStars();
    snprintf(text, sizeof text, "Answers given: %d", totalAttempts);        DrawText(text, (int)panel.x + 30, (int)panel.y + 214, 20, COLOR_TXT);
    snprintf(text, sizeof text, "Mistakes: %d", totalAttempts - totalCorrect); DrawText(text, (int)panel.x + 30, (int)panel.y + 238, 20, COLOR_TXT);
    snprintf(text, sizeof text, "Skipped: %d", totalSkipped);                DrawText(text, (int)panel.x + 30, (int)panel.y + 262, 20, COLOR_TXT);
    snprintf(text, sizeof text, "Stars: %d / 27", stars);                    DrawText(text, (int)panel.x + 30, (int)panel.y + 286, 20, COLOR_TXT);

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
    DrawRoundOutlined(note, 14, 3, (Color){22, 32, 70, 255}, COLOR_INK);
    WrapText(advice, (int)note.x + 12, (int)note.y + 8, (int)note.width - 24, 16, COLOR_TXT, true);

    // per level rows
    DrawTextCentered("YOUR LEVELS", (int)(panel.x + 640), (int)panel.y + 10, 22, COLOR_TXT);
    for (int l = 0; l < LEVEL_COUNT; l++) {
        Rectangle row = { panel.x + 350, panel.y + 42 + l * 72.0f, 570, 62 };
        DrawRoundOutlined(row, 18, 3, (Color){18, 16, 52, 255}, (Color){62, 56, 128, 255});
        int attempts, correct, weak;
        LevelReport(l, &attempts, &correct, &weak);
        int pct = attempts ? correct * 100 / attempts : 0;
        DrawCircleV(V2(row.x + 32, row.y + 31), 22, COLOR_INK);
        DrawCircleV(V2(row.x + 32, row.y + 31), 19, LevelColor(l));
        char number[4];
        snprintf(number, sizeof number, "%d", l + 1);
        DrawTextCO(number, (int)row.x + 32, (int)row.y + 18, 26, WHITE, COLOR_INK);
        DrawText(levelNames[l], (int)row.x + 66, (int)row.y + 7, 20, COLOR_TXT);
        Rectangle track = { row.x + 66, row.y + 36, 220, 14 };
        DrawRound(track, 7, (Color){34, 32, 84, 255});
        if (attempts) DrawRound((Rectangle){ track.x, track.y, track.width * pct / 100.0f * Smooth((app.screenTime - 0.25f - l * 0.1f) / 0.5f) < 14 ? 14 : track.width * pct / 100.0f * Smooth((app.screenTime - 0.25f - l * 0.1f) / 0.5f), track.height }, 7, pct >= 80 ? COLOR_GREEN : (pct >= 50 ? COLOR_YELLOW : COLOR_RED));
        snprintf(number, sizeof number, "%d%%", pct);
        if (attempts) DrawText(number, (int)track.x + 232, (int)track.y - 3, 20, COLOR_TXT);
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
            DrawStarShape(sc, 11, 0, s < best ? COLOR_YELLOW : (Color){60, 64, 110, 255});
        }
        bool open = IsLevelUnlocked(app.mode, l);
        if (DrawCandyButton((Rectangle){ row.x + 474, row.y + 9, 82, 50 }, open ? "PLAY" : "LOCK", COLOR_GREEN, open, 18)) StartLevel(l);
    }
}

static void ScreenReview(void) {
    DrawBackground(0.18f);
    DrawHeaderBar("MY PROGRESS", 122, true);
    if (DrawCandyButton((Rectangle){ 12, 8, 100, 44 }, "BACK", COLOR_ORANGE, true, 20) || IsKeyPressed(KEY_ESCAPE)) { SetScreen(SCREEN_MAP); return; }

    const char *tabNames[3] = { "OVERVIEW", "WEAK SPOTS", "LAST RUN" };
    for (int tab = 0; tab < 3; tab++) {
        Rectangle r = { 30 + tab * 200.0f, 86, 190, 52 };
        if (DrawCandyButton(r, tabNames[tab], tab == app.reviewTab ? COLOR_CYAN : COLOR_PURPLE, true, 22) && tab != app.reviewTab) { app.reviewTab = tab; BuildReviewList(tab); }
    }
    Rectangle panel = { 30, 148, 940, 406 };
    DrawRoundOutlined(panel, 26, 5, COLOR_CREAM, COLOR_INK);

    if (app.reviewTab == 0) {
        DrawReviewOverview(panel);
    } else {
        if (app.reviewCount == 0) {
            DrawTextCentered(app.reviewTab == 1 ? "No weak spots yet!" : "Nothing to review yet!", 500, 300, 32, COLOR_TXT);
            DrawTextCentered(app.reviewTab == 1 ? "Keep playing - I will flag anything tricky here." : "Finish a level and its answers show up here.", 500, 346, 20, COLOR_DIM);
        } else {
            DrawText(app.reviewTab == 1 ? "Tricky questions first. Tap one to see the answer and why." : "Tap a question to see the answer and why.", (int)panel.x + 28, (int)panel.y + 14, 18, COLOR_DIM);
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
                DrawRoundOutlined(whole, 18, 3, (Color){18, 16, 52, 255}, expanded ? LevelColor(q->levelIndex) : (Color){62, 56, 128, 255});
                QStatus status = app.reviewStatus[i];
                DrawCircleV(V2(head.x + 34, head.y + 31), 20, COLOR_INK);
                DrawCircleV(V2(head.x + 34, head.y + 31), 17, app.reviewTab == 2 ? StatusColor(status) : LevelColor(q->levelIndex));
                char tag[8];
                snprintf(tag, sizeof tag, "L%d", q->levelIndex + 1);
                if (app.reviewTab == 2) snprintf(tag, sizeof tag, "%d", i + 1);
                DrawTextCentered(tag, (int)head.x + 34, (int)head.y + 22, 18, COLOR_INK);
                char fitted[200];
                textMode = 1;
                FitText(q->prompt, fitted, sizeof fitted, app.reviewTab == 1 ? 480 : 600, 20);
                TextDraw(fitted, (int)head.x + 66, (int)head.y + 8, 20, COLOR_TXT);
                if (q->snippet[0] && !expanded) {
                    textMode = 2;
                    FitText(q->snippet, fitted, sizeof fitted, 480, 16);
                    TextDraw(fitted, (int)head.x + 66, (int)head.y + 36, 16, COLOR_CODE_TXT);
                    textMode = 0;
                } else if (!q->snippet[0] && !expanded) {
                    textMode = 0;
                    DrawText(levelNames[q->levelIndex], (int)head.x + 66, (int)head.y + 36, 16, COLOR_DIM);
                }
                textMode = 0;
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
                    DrawLineEx(V2(head.x + 20, head.y + 64), V2(head.x + head.width - 20, head.y + 64), 2, (Color){62, 56, 128, 255});
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
// Small speaker button (top right of every screen). Press M too, except while answering questions.
static void DrawSoundToggle(void) {
    Rectangle r = { 940, 71, 44, 26 };
    bool hovered = HoverRect(r);
    if ((IsKeyPressed(KEY_M) && app.screen != SCREEN_QUESTION && !app.nameModal) || TakeClick(hovered)) { soundOn = !soundOn; if (soundOn) PlayFx(FX_BLIP); }
    DrawRoundOutlined(r, 8, 2, COLOR_CREAM, hovered ? COLOR_CYAN : COLOR_INK);
    Color c = soundOn ? COLOR_CYAN : COLOR_GRAY;
    DrawRectangle((int)r.x + 10, (int)r.y + 9, 6, 8, c);
    DrawTriSafe(V2(r.x + 16, r.y + 9), V2(r.x + 24, r.y + 4), V2(r.x + 24, r.y + 22), c);
    DrawTriSafe(V2(r.x + 16, r.y + 9), V2(r.x + 24, r.y + 22), V2(r.x + 16, r.y + 17), c);
    if (soundOn) {
        DrawRing(V2(r.x + 25, r.y + 13), 5, 7, 305, 415, 10, c);
        DrawRing(V2(r.x + 25, r.y + 13), 10, 12, 305, 415, 12, c);
    } else {
        DrawLineEx(V2(r.x + 28, r.y + 8), V2(r.x + 38, r.y + 18), 3, COLOR_RED);
        DrawLineEx(V2(r.x + 28, r.y + 18), V2(r.x + 38, r.y + 8), 3, COLOR_RED);
    }
}

static void InitApp(void) {
    SetExitKey(KEY_NULL);   // ESC is used for "back"
    for (int i = 0; i < SPARKLE_COUNT; i++) sparklePos[i] = V2((float)GetRandomValue(0, WINDOW_WIDTH), (float)GetRandomValue(0, WINDOW_HEIGHT));
    memset(&app, 0, sizeof app);
    app.selectedLevel = -1;
    app.pendingHop = app.hopFrom = app.hopTo = app.unlockLevel = app.autoOpenLevel = -1;
    LoadReadingFonts();
    LoadProfiles();
    LoadGuestProgress();
    app.notesLevel = -1;
    InitAudio();
}

static void FrameUpdate(void) {
    float dt = GetFrameTime();
    app.screenTime += dt;
    UpdateEffects(dt);
    UpdateAudio(dt, app.screen == SCREEN_QUESTION);
    clickConsumed = false;
    botLayer = 0;
    textMode = 0;
    wantHand = false;
    uiLocked = false;
    BeginDrawing();
    ClearBackground(COLOR_BG_TOP);   // also clears the depth buffer used by the 3D robot
    switch (app.screen) {
        case SCREEN_TITLE:    ScreenTitle();    break;
        case SCREEN_PROFILES: ScreenProfiles(); break;
        case SCREEN_HUB:      ScreenHub();      break;
        case SCREEN_NOTES:    ScreenNotes();    break;
        case SCREEN_MAP:      ScreenMap();      break;
        case SCREEN_QUESTION: ScreenQuestion(); break;
        case SCREEN_RESULTS:  ScreenResults();  break;
        case SCREEN_REVIEW:   ScreenReview();   break;
    }
    DrawEffects();
    DrawSoundToggle();
    for (int y = 0; y < WINDOW_HEIGHT; y += 3) DrawRectangle(0, y, WINDOW_WIDTH, 1, Fade(BLACK, 0.07f));   // faint CRT scanlines
    if (app.screenTime < 0.55f) {   // "blinds" wipe between screens
        const int slats = 14;
        float width = WINDOW_WIDTH / (float)slats;
        for (int i = 0; i < slats; i++) {
            float p = Smooth((app.screenTime - i * 0.025f) / 0.3f);
            float h = WINDOW_HEIGHT * (1.0f - p);
            if (h < 1) continue;
            DrawRectangle((int)(i * width), 0, (int)width + 1, (int)h, (Color){6, 10, 30, 255});
            DrawRectangle((int)(i * width), (int)h - 2, (int)width + 1, 2, Fade(COLOR_CYAN, 0.8f));
        }
    }
    EndDrawing();
    SetMouseCursor(wantHand ? MOUSE_CURSOR_POINTING_HAND : MOUSE_CURSOR_DEFAULT);
}

int main(void) {
    InitWindow(WINDOW_WIDTH, WINDOW_HEIGHT, "C Quest");
    SetTargetFPS(60);
    InitApp();
    while (!WindowShouldClose()) FrameUpdate();
    ShutdownAudio();
    CloseWindow();
    return 0;
}
