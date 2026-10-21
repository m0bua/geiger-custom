#include "button_input.h"

#define CHECK(condition) do { if (!(condition)) return __LINE__; } while (0)
#define UPDATE(raw, time) button_input_update(&button, raw, time)

int test_entry(void)
{
    button_input_t button;
    button_input_t other;
    uint32_t time;

    /* 1. Normal short click and a second click. */
    button_input_init(&button, false, 0u);
    CHECK(UPDATE(true, 10u) == 0u);
    CHECK(UPDATE(true, 30u) == BUTTON_INPUT_DOWN);
    CHECK(UPDATE(false, 100u) == 0u);
    CHECK(UPDATE(false, 120u) == (BUTTON_INPUT_UP | BUTTON_INPUT_CLICK));
    CHECK(UPDATE(true, 160u) == 0u);
    CHECK(UPDATE(true, 180u) == BUTTON_INPUT_DOWN);
    CHECK(UPDATE(false, 210u) == 0u);
    CHECK(UPDATE(false, 230u) == (BUTTON_INPUT_UP | BUTTON_INPUT_CLICK));

    /* 2. Hundreds of calls at the same timestamp cannot debounce a press. */
    button_input_init(&button, false, 0u);
    for (time = 0u; time < 1000u; ++time) CHECK(UPDATE(true, 0u) == 0u);
    CHECK(UPDATE(true, 20u) == BUTTON_INPUT_DOWN);
    for (time = 0u; time < 1000u; ++time) CHECK(UPDATE(true, 20u) == 0u);
    CHECK(UPDATE(true, 799u) == 0u);
    CHECK(UPDATE(true, 800u) == BUTTON_INPUT_LONG);

    /* 3. A long click has one first event, timed repeats and no short release. */
    CHECK(UPDATE(true, 899u) == 0u);
    CHECK(UPDATE(true, 900u) == BUTTON_INPUT_LONG);
    CHECK(UPDATE(false, 950u) == 0u);
    CHECK(UPDATE(false, 970u) == BUTTON_INPUT_UP);

    /* 4. Release debounce must not stretch a 790 ms click into a long click. */
    button_input_init(&button, false, 0u);
    CHECK(UPDATE(true, 0u) == 0u);
    CHECK(UPDATE(true, 20u) == BUTTON_INPUT_DOWN);
    CHECK(UPDATE(false, 790u) == 0u);
    CHECK(UPDATE(false, 810u) == (BUTTON_INPUT_UP | BUTTON_INPUT_CLICK));

    /* 5. Wake while held: consume the whole hold, then accept the next click. */
    button_input_suppress(&button, true, 0u);
    for (time = 0u; time <= 2000u; time += 10u) CHECK(UPDATE(true, time) == 0u);
    CHECK(UPDATE(false, 2010u) == 0u);
    CHECK(UPDATE(false, 2030u) == 0u);
    CHECK(!button.suppressed);
    CHECK(UPDATE(true, 2040u) == 0u);
    CHECK(UPDATE(true, 2060u) == BUTTON_INPUT_DOWN);
    CHECK(UPDATE(false, 2100u) == 0u);
    CHECK(UPDATE(false, 2120u) == (BUTTON_INPUT_UP | BUTTON_INPUT_CLICK));

    /* 6. Wake after an already released tap, without any debounced UP event. */
    button_input_suppress(&button, false, 0u);
    CHECK(UPDATE(false, 0u) == 0u);
    CHECK(UPDATE(false, 10u) == 0u);
    CHECK(UPDATE(false, 20u) == 0u);
    CHECK(!button.suppressed);
    CHECK(UPDATE(true, 30u) == 0u);
    CHECK(UPDATE(true, 50u) == BUTTON_INPUT_DOWN);
    CHECK(UPDATE(false, 90u) == 0u);
    CHECK(UPDATE(false, 110u) == (BUTTON_INPUT_UP | BUTTON_INPUT_CLICK));

    /* 7. Bounce during wake release restarts its release timer. */
    button_input_suppress(&button, true, 0u);
    CHECK(UPDATE(false, 10u) == 0u);
    CHECK(UPDATE(true, 20u) == 0u);
    CHECK(UPDATE(false, 30u) == 0u);
    CHECK(UPDATE(false, 49u) == 0u);
    CHECK(button.suppressed);
    CHECK(UPDATE(false, 50u) == 0u);
    CHECK(!button.suppressed);

    /* 8. Normal contact bounce cannot emit a click or a long event. */
    button_input_init(&button, false, 0u);
    CHECK(UPDATE(true, 1u) == 0u);
    CHECK(UPDATE(false, 8u) == 0u);
    CHECK(UPDATE(true, 12u) == 0u);
    CHECK(UPDATE(false, 17u) == 0u);
    CHECK(UPDATE(false, 40u) == 0u);
    CHECK(UPDATE(true, 50u) == 0u);
    CHECK(UPDATE(true, 70u) == BUTTON_INPUT_DOWN);

    /* 9. Suppressing one key cannot block the other. */
    button_input_suppress(&button, true, 0u);
    button_input_init(&other, false, 0u);
    CHECK(button_input_update(&other, true, 10u) == 0u);
    CHECK(button_input_update(&other, true, 30u) == BUTTON_INPUT_DOWN);
    CHECK(UPDATE(true, 1000u) == 0u);
    CHECK(button_input_update(&other, false, 40u) == 0u);
    CHECK(button_input_update(&other, false, 60u) ==
        (BUTTON_INPUT_UP | BUTTON_INPUT_CLICK));

    /* 10. A key held at boot remains silent until released. */
    button_input_init(&button, true, 0u);
    CHECK(UPDATE(true, 3000u) == 0u);
    CHECK(UPDATE(false, 3010u) == 0u);
    CHECK(UPDATE(false, 3030u) == 0u);
    CHECK(!button.suppressed);
    CHECK(UPDATE(true, 3040u) == 0u);
    CHECK(UPDATE(true, 3060u) == BUTTON_INPUT_DOWN);

    /* 11. Millisecond rollover during a short click. */
    button_input_init(&button, false, UINT32_MAX - 100u);
    CHECK(UPDATE(true, UINT32_MAX - 50u) == 0u);
    CHECK(UPDATE(true, UINT32_MAX - 30u) == BUTTON_INPUT_DOWN);
    CHECK(UPDATE(false, 70u) == 0u);
    CHECK(UPDATE(false, 90u) == (BUTTON_INPUT_UP | BUTTON_INPUT_CLICK));

    /* 12. Millisecond rollover during release of a consumed wake tap. */
    button_input_suppress(&button, false, UINT32_MAX - 10u);
    CHECK(UPDATE(false, 5u) == 0u);
    CHECK(button.suppressed);
    CHECK(UPDATE(false, 10u) == 0u);
    CHECK(!button.suppressed);

    /* 13. Sparse sampling still recognizes a real long hold on release. */
    button_input_init(&button, false, 0u);
    CHECK(UPDATE(true, 0u) == 0u);
    CHECK(UPDATE(true, 20u) == BUTTON_INPUT_DOWN);
    CHECK(UPDATE(false, 900u) == 0u);
    CHECK(UPDATE(false, 920u) == (BUTTON_INPUT_UP | BUTTON_INPUT_LONG));

    /* 14. Long-loop backlog is not a series of elapsed sample periods. */
    button_input_init(&button, false, 10000u);
    CHECK(UPDATE(true, 10010u) == 0u);
    CHECK(UPDATE(true, 10030u) == BUTTON_INPUT_DOWN);
    for (time = 0u; time < 1000u; ++time) CHECK(UPDATE(true, 10040u) == 0u);
    CHECK(UPDATE(false, 10080u) == 0u);
    CHECK(UPDATE(false, 10100u) == (BUTTON_INPUT_UP | BUTTON_INPUT_CLICK));

    /* 15. Consuming an already debounced click clears its previous hold state. */
    button_input_init(&button, false, 0u);
    CHECK(UPDATE(true, 0u) == 0u);
    CHECK(UPDATE(true, 20u) == BUTTON_INPUT_DOWN);
    button_input_suppress(&button, false, 30u);
    CHECK(UPDATE(false, 50u) == 0u);
    CHECK(UPDATE(true, 60u) == 0u);
    CHECK(UPDATE(true, 80u) == BUTTON_INPUT_DOWN);
    CHECK(UPDATE(false, 120u) == 0u);
    CHECK(UPDATE(false, 140u) == (BUTTON_INPUT_UP | BUTTON_INPUT_CLICK));
    return 0;
}
