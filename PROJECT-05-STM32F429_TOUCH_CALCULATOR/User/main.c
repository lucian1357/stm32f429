/**
 *  Touch-screen pocket calculator demo for the STM32F429 Discovery
 *
 *  Uses the ILI9341 display and STMPE811 touch controller to draw a
 *  calculator keypad and evaluate basic arithmetic expressions.
 */

#include "stm32f4xx.h"
#include "defines.h"
#include "tm_stm32f4_ili9341.h"
#include "tm_stm32f4_stmpe811.h"
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define DISPLAY_HEIGHT                 80
#define BUTTON_COLUMNS                 4
#define BUTTON_ROWS                    4
#define BUTTON_SPACING                 4
#define BUTTON_START_X                 6
#define BUTTON_START_Y                 (DISPLAY_HEIGHT + 10)
#define BUTTON_WIDTH                   ((ILI9341_WIDTH - (BUTTON_START_X * 2) - (BUTTON_SPACING * (BUTTON_COLUMNS - 1))) / BUTTON_COLUMNS)
#define BUTTON_HEIGHT                  55
#define INPUT_BUFFER_SIZE              17

typedef enum {
    CALC_BUTTON_DIGIT,
    CALC_BUTTON_OPERATOR,
    CALC_BUTTON_CLEAR,
    CALC_BUTTON_EQUALS
} CalculatorButtonType;

typedef struct {
    const char *label;
    uint16_t x;
    uint16_t y;
    uint16_t w;
    uint16_t h;
    CalculatorButtonType type;
} CalculatorButton;

typedef struct {
    double accumulator;
    char currentOperator;
    bool hasAccumulator;
    bool error;
    char input[INPUT_BUFFER_SIZE];
} CalculatorState;

typedef struct {
    uint16_t x;
    uint16_t y;
    bool pressed;
} TouchStatus;

static CalculatorButton buttons[] = {
    {"7", BUTTON_START_X + 0 * (BUTTON_WIDTH + BUTTON_SPACING), BUTTON_START_Y + 0 * (BUTTON_HEIGHT + BUTTON_SPACING), BUTTON_WIDTH, BUTTON_HEIGHT, CALC_BUTTON_DIGIT},
    {"8", BUTTON_START_X + 1 * (BUTTON_WIDTH + BUTTON_SPACING), BUTTON_START_Y + 0 * (BUTTON_HEIGHT + BUTTON_SPACING), BUTTON_WIDTH, BUTTON_HEIGHT, CALC_BUTTON_DIGIT},
    {"9", BUTTON_START_X + 2 * (BUTTON_WIDTH + BUTTON_SPACING), BUTTON_START_Y + 0 * (BUTTON_HEIGHT + BUTTON_SPACING), BUTTON_WIDTH, BUTTON_HEIGHT, CALC_BUTTON_DIGIT},
    {"/", BUTTON_START_X + 3 * (BUTTON_WIDTH + BUTTON_SPACING), BUTTON_START_Y + 0 * (BUTTON_HEIGHT + BUTTON_SPACING), BUTTON_WIDTH, BUTTON_HEIGHT, CALC_BUTTON_OPERATOR},
    {"4", BUTTON_START_X + 0 * (BUTTON_WIDTH + BUTTON_SPACING), BUTTON_START_Y + 1 * (BUTTON_HEIGHT + BUTTON_SPACING), BUTTON_WIDTH, BUTTON_HEIGHT, CALC_BUTTON_DIGIT},
    {"5", BUTTON_START_X + 1 * (BUTTON_WIDTH + BUTTON_SPACING), BUTTON_START_Y + 1 * (BUTTON_HEIGHT + BUTTON_SPACING), BUTTON_WIDTH, BUTTON_HEIGHT, CALC_BUTTON_DIGIT},
    {"6", BUTTON_START_X + 2 * (BUTTON_WIDTH + BUTTON_SPACING), BUTTON_START_Y + 1 * (BUTTON_HEIGHT + BUTTON_SPACING), BUTTON_WIDTH, BUTTON_HEIGHT, CALC_BUTTON_DIGIT},
    {"*", BUTTON_START_X + 3 * (BUTTON_WIDTH + BUTTON_SPACING), BUTTON_START_Y + 1 * (BUTTON_HEIGHT + BUTTON_SPACING), BUTTON_WIDTH, BUTTON_HEIGHT, CALC_BUTTON_OPERATOR},
    {"1", BUTTON_START_X + 0 * (BUTTON_WIDTH + BUTTON_SPACING), BUTTON_START_Y + 2 * (BUTTON_HEIGHT + BUTTON_SPACING), BUTTON_WIDTH, BUTTON_HEIGHT, CALC_BUTTON_DIGIT},
    {"2", BUTTON_START_X + 1 * (BUTTON_WIDTH + BUTTON_SPACING), BUTTON_START_Y + 2 * (BUTTON_HEIGHT + BUTTON_SPACING), BUTTON_WIDTH, BUTTON_HEIGHT, CALC_BUTTON_DIGIT},
    {"3", BUTTON_START_X + 2 * (BUTTON_WIDTH + BUTTON_SPACING), BUTTON_START_Y + 2 * (BUTTON_HEIGHT + BUTTON_SPACING), BUTTON_WIDTH, BUTTON_HEIGHT, CALC_BUTTON_DIGIT},
    {"-", BUTTON_START_X + 3 * (BUTTON_WIDTH + BUTTON_SPACING), BUTTON_START_Y + 2 * (BUTTON_HEIGHT + BUTTON_SPACING), BUTTON_WIDTH, BUTTON_HEIGHT, CALC_BUTTON_OPERATOR},
    {"0", BUTTON_START_X + 0 * (BUTTON_WIDTH + BUTTON_SPACING), BUTTON_START_Y + 3 * (BUTTON_HEIGHT + BUTTON_SPACING), BUTTON_WIDTH, BUTTON_HEIGHT, CALC_BUTTON_DIGIT},
    {".", BUTTON_START_X + 1 * (BUTTON_WIDTH + BUTTON_SPACING), BUTTON_START_Y + 3 * (BUTTON_HEIGHT + BUTTON_SPACING), BUTTON_WIDTH, BUTTON_HEIGHT, CALC_BUTTON_DIGIT},
    {"C", BUTTON_START_X + 2 * (BUTTON_WIDTH + BUTTON_SPACING), BUTTON_START_Y + 3 * (BUTTON_HEIGHT + BUTTON_SPACING), BUTTON_WIDTH, BUTTON_HEIGHT, CALC_BUTTON_CLEAR},
    {"+", BUTTON_START_X + 3 * (BUTTON_WIDTH + BUTTON_SPACING), BUTTON_START_Y + 3 * (BUTTON_HEIGHT + BUTTON_SPACING), BUTTON_WIDTH, BUTTON_HEIGHT, CALC_BUTTON_OPERATOR},
    {"=", BUTTON_START_X,                                      BUTTON_START_Y + 4 * (BUTTON_HEIGHT + BUTTON_SPACING), BUTTON_WIDTH * 4 + BUTTON_SPACING * 3, BUTTON_HEIGHT, CALC_BUTTON_EQUALS}
};

static void Calculator_Reset(CalculatorState *state) {
    state->accumulator = 0.0;
    state->currentOperator = 0;
    state->hasAccumulator = false;
    state->error = false;
    memset(state->input, 0, sizeof(state->input));
}

static bool Calculator_InputHasDecimal(const CalculatorState *state) {
    return strchr(state->input, '.') != NULL;
}

static void Calculator_ClearInput(CalculatorState *state) {
    memset(state->input, 0, sizeof(state->input));
}

static void Calculator_AppendInput(CalculatorState *state, char value) {
    size_t len = strlen(state->input);

    if (len + 1 >= INPUT_BUFFER_SIZE) {
        return;
    }

    if (value == '.' && Calculator_InputHasDecimal(state)) {
        return;
    }

    state->input[len] = value;
    state->input[len + 1] = '\0';
}

static double Calculator_ParseInput(const CalculatorState *state) {
    if (state->input[0] == '\0') {
        return 0.0;
    }

    return atof(state->input);
}

static double Calculator_ApplyOperation(double a, double b, char op, bool *error) {
    switch (op) {
        case '+':
            return a + b;
        case '-':
            return a - b;
        case '*':
            return a * b;
        case '/':
            if (b == 0.0) {
                *error = true;
                return 0.0;
            }
            return a / b;
        default:
            return b;
    }
}

static void Calculator_FormatLine(const char *prefix, double value, char *output, size_t size) {
    char number[32];

    sprintf(number, "%0.6g", value);
    snprintf(output, size, "%s%s", prefix, number);
}

static void Calculator_DrawDisplay(const CalculatorState *state, const TouchStatus *touch) {
    char line1[64];
    char line2[64];
    char line3[64];

    TM_ILI9341_DrawFilledRectangle(0, 0, ILI9341_WIDTH, DISPLAY_HEIGHT, ILI9341_COLOR_BLACK);

    if (state->error) {
        snprintf(line1, sizeof(line1), "Error: invalid op");
    } else if (state->input[0] != '\0') {
        snprintf(line1, sizeof(line1), "Input: %s", state->input);
    } else if (state->hasAccumulator) {
        Calculator_FormatLine("Result: ", state->accumulator, line1, sizeof(line1));
    } else {
        snprintf(line1, sizeof(line1), "Ready");
    }

    if (state->currentOperator != 0) {
        snprintf(line2, sizeof(line2), "Op: %c", state->currentOperator);
    } else {
        snprintf(line2, sizeof(line2), "Op: none");
    }

    if (touch != NULL && touch->pressed) {
        snprintf(line3, sizeof(line3), "Touch: %3u,%3u", (unsigned int)touch->x, (unsigned int)touch->y);
    } else {
        snprintf(line3, sizeof(line3), "Touch: ---");
    }

    TM_ILI9341_Puts(5, 5, line1, &TM_Font_11x18, ILI9341_COLOR_GREEN, ILI9341_COLOR_BLACK);
    TM_ILI9341_Puts(5, 30, line2, &TM_Font_11x18, ILI9341_COLOR_LIGHTGREY, ILI9341_COLOR_BLACK);
    TM_ILI9341_Puts(5, 55, line3, &TM_Font_11x18, ILI9341_COLOR_YELLOW, ILI9341_COLOR_BLACK);
}

static void Calculator_DrawKeypadBackground(void) {
    uint16_t top = BUTTON_START_Y - BUTTON_SPACING;
    uint16_t height = ILI9341_HEIGHT - top;

    TM_ILI9341_DrawFilledRectangle(0, top, ILI9341_WIDTH, height, ILI9341_COLOR_DARKGRAY);
}

static uint16_t Calculator_ButtonColor(const CalculatorButton *button) {
    switch (button->type) {
        case CALC_BUTTON_DIGIT:
            return ILI9341_COLOR_LIGHTGREY;
        case CALC_BUTTON_OPERATOR:
            return 0xFDA0; /* Orange-like */
        case CALC_BUTTON_CLEAR:
            return ILI9341_COLOR_RED;
        case CALC_BUTTON_EQUALS:
            return ILI9341_COLOR_GREEN;
        default:
            return ILI9341_COLOR_WHITE;
    }
}

static void Calculator_DrawButton(const CalculatorButton *button, bool pressed) {
    uint16_t background = Calculator_ButtonColor(button);
    uint16_t textColor = ILI9341_COLOR_BLACK;
    uint16_t textWidth;
    uint16_t textHeight;
    uint16_t textX;
    uint16_t textY;

    if (pressed) {
        background = ILI9341_COLOR_YELLOW;
    }

    TM_ILI9341_DrawFilledRectangle(button->x, button->y, button->w, button->h, background);
    TM_ILI9341_DrawRectangle(button->x, button->y, button->w, button->h, ILI9341_COLOR_BLACK);

    /* Center label */
    textWidth = strlen(button->label) * 11;
    textHeight = 18;
    textX = button->x + (button->w - textWidth) / 2;
    textY = button->y + (button->h - textHeight) / 2;

    TM_ILI9341_Puts(textX, textY, (char *)button->label, &TM_Font_11x18, textColor, background);
}

static void Calculator_DrawButtons(void) {
    uint32_t i;

    Calculator_DrawKeypadBackground();

    for (i = 0; i < (sizeof(buttons) / sizeof(buttons[0])); i++) {
        Calculator_DrawButton(&buttons[i], false);
    }
}

static void Calculator_HandleOperator(CalculatorState *state, char operatorChar) {
    double value = Calculator_ParseInput(state);

    if (!state->hasAccumulator) {
        state->accumulator = value;
        state->hasAccumulator = true;
    } else if (state->currentOperator != 0 || state->input[0] != '\0') {
        state->accumulator = Calculator_ApplyOperation(state->accumulator, value, state->currentOperator, &state->error);
    }

    Calculator_ClearInput(state);
    state->currentOperator = operatorChar;
}

static void Calculator_HandleEquals(CalculatorState *state) {
    if (!state->hasAccumulator && state->input[0] != '\0') {
        state->accumulator = Calculator_ParseInput(state);
        state->hasAccumulator = true;
        state->currentOperator = 0;
        Calculator_ClearInput(state);
        return;
    }

    if (state->hasAccumulator && state->currentOperator != 0) {
        double value = Calculator_ParseInput(state);
        state->accumulator = Calculator_ApplyOperation(state->accumulator, value, state->currentOperator, &state->error);
        state->currentOperator = 0;
        Calculator_ClearInput(state);
    }
}

static void Calculator_HandleButton(CalculatorState *state, const CalculatorButton *button) {
    if (state->error && button->type != CALC_BUTTON_CLEAR) {
        return;
    }

    switch (button->type) {
        case CALC_BUTTON_DIGIT:
            Calculator_AppendInput(state, button->label[0]);
            break;
        case CALC_BUTTON_OPERATOR:
            Calculator_HandleOperator(state, button->label[0]);
            break;
        case CALC_BUTTON_CLEAR:
            Calculator_Reset(state);
            break;
        case CALC_BUTTON_EQUALS:
            Calculator_HandleEquals(state);
            break;
        default:
            break;
    }
}

static int8_t Calculator_FindButton(uint16_t x, uint16_t y) {
    uint32_t i;

    for (i = 0; i < (sizeof(buttons) / sizeof(buttons[0])); i++) {
        const CalculatorButton *btn = &buttons[i];

        if (x >= btn->x && x <= (btn->x + btn->w) && y >= btn->y && y <= (btn->y + btn->h)) {
            return (int8_t)i;
        }
    }

    return -1;
}

int main(void) {
    TM_STMPE811_TouchData touchData;
    CalculatorState state;
    int8_t lastButton = -1;
    TouchStatus touchStatus;
    TouchStatus lastRenderedStatus;
    bool stateDirty;

    SystemInit();
    TM_ILI9341_Init();
    TM_ILI9341_Rotate(TM_ILI9341_Orientation_Portrait_2);
    TM_ILI9341_Fill(ILI9341_COLOR_GRAY);

    if (TM_STMPE811_Init() != TM_STMPE811_State_Ok) {
        TM_ILI9341_Puts(10, 10, "Touch init failed", &TM_Font_11x18, ILI9341_COLOR_RED, ILI9341_COLOR_GRAY);
        while (1) {}
    }

    touchData.orientation = TM_STMPE811_Orientation_Portrait_2;

    Calculator_Reset(&state);
    touchStatus.x = 0;
    touchStatus.y = 0;
    touchStatus.pressed = false;
    lastRenderedStatus = touchStatus;
    stateDirty = true;

    Calculator_DrawDisplay(&state, &touchStatus);
    Calculator_DrawButtons();

    while (1) {
        if (TM_STMPE811_ReadTouch(&touchData) == TM_STMPE811_State_Pressed) {
            touchStatus.x = touchData.x;
            touchStatus.y = touchData.y;
            touchStatus.pressed = true;
            int8_t pressed = Calculator_FindButton(touchData.x, touchData.y);

            if (pressed >= 0 && pressed != lastButton) {
                Calculator_HandleButton(&state, &buttons[pressed]);
                stateDirty = true;
                Calculator_DrawButton(&buttons[pressed], true);
                lastButton = pressed;
            }
        } else {
            touchStatus.x = 0;
            touchStatus.y = 0;
            touchStatus.pressed = false;
            if (lastButton >= 0) {
                Calculator_DrawButton(&buttons[lastButton], false);
                lastButton = -1;
            }
        }

        if (stateDirty ||
            touchStatus.pressed != lastRenderedStatus.pressed ||
            touchStatus.x != lastRenderedStatus.x ||
            touchStatus.y != lastRenderedStatus.y) {
            Calculator_DrawDisplay(&state, &touchStatus);
            lastRenderedStatus = touchStatus;
            stateDirty = false;
        }
    }
}
