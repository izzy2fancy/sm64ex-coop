#include <string.h>
#include <time.h>
#include "djui.h"

#include "src/pc/controller/controller_sdl.h"
#include "src/pc/controller/controller_mouse.h"
#include "src/pc/controller/controller_keyboard.h"

#include "audio_defines.h"
#include "audio/external.h"

enum PadHoldDirection { PAD_HOLD_DIR_NONE, PAD_HOLD_DIR_UP, PAD_HOLD_DIR_DOWN, PAD_HOLD_DIR_LEFT, PAD_HOLD_DIR_RIGHT };
static enum PadHoldDirection sKeyboardHoldDirection = PAD_HOLD_DIR_NONE;
static u16 sKeyboardButtons = 0;

static bool sIgnoreInteractableUntilCursorReleased = false;

static struct DjuiBase* sInteractableFocus   = NULL;
static struct DjuiBase* sInteractableBinding = NULL;
static struct DjuiBase* sHovered      = NULL;
static struct DjuiBase* sMouseDown    = NULL;
bool gInteractableOverridePad         = false;
OSContPad gInteractablePad            = { 0 };
static OSContPad sLastInteractablePad = { 0 };
static int sLastMouseButtons          = 0;

static void djui_interactable_on_click(struct DjuiBase* base) {
    if (base == NULL) { return; }
    if (base->interactable == NULL) { return; }
    if (base->interactable->on_click == NULL) { return; }
    base->interactable->on_click(base);
}

static void djui_interactable_on_hover(struct DjuiBase* base) {
    if (base                               == NULL) { return; }
    if (base->interactable                 == NULL) { return; }
    if (base->interactable->on_hover == NULL) { return; }
    base->interactable->on_hover(base);
}

static void djui_interactable_on_hover_end(struct DjuiBase* base) {
    if (base                         == NULL) { return; }
    if (base->interactable           == NULL) { return; }
    if (base->interactable->on_hover == NULL) { return; }
    base->interactable->on_hover_end(base);
}

static void djui_interactable_on_cursor_down_begin(struct DjuiBase* base, bool inputCursor) {
    if (base                                     == NULL) { return; }
    if (base->interactable                       == NULL) { return; }
    if (base->interactable->on_cursor_down_begin == NULL) { return; }

    if (sHovered != NULL) {
        djui_interactable_on_hover_end(sHovered);
        sHovered = NULL;
    }

    base->interactable->on_cursor_down_begin(base, inputCursor);
}

static void djui_interactable_on_cursor_down(struct DjuiBase* base) {
    if (base                               == NULL) { return; }
    if (base->interactable                 == NULL) { return; }
    if (base->interactable->on_cursor_down == NULL) { return; }

    base->interactable->on_cursor_down(base);
}

static void djui_interactable_on_cursor_down_end(struct DjuiBase* base) {
    if (base                                   == NULL) { return; }
    if (base->interactable                     == NULL) { return; }
    if (base->interactable->on_cursor_down_end == NULL) { return; }
    base->interactable->on_cursor_down_end(base);

    if (djui_cursor_inside_base(base)) {
        djui_interactable_on_click(base);
    }
}

static void djui_interactable_on_focus_begin(struct DjuiBase* base) {
    if (base                               == NULL) { return; }
    if (base->interactable                 == NULL) { return; }
    if (base->interactable->on_focus_begin == NULL) { return; }
    base->interactable->on_focus_begin(base);
}

static void djui_interactable_on_focus(struct DjuiBase* base) {
    if (base                         == NULL) { return; }
    if (base->interactable           == NULL) { return; }
    if (base->interactable->on_focus == NULL) { return; }
    base->interactable->on_focus(base, &gInteractablePad);
}

static void djui_interactable_on_focus_end(struct DjuiBase* base) {
    if (base                             == NULL) { return; }
    if (base->interactable               == NULL) { return; }
    if (base->interactable->on_focus_end == NULL) { return; }
    base->interactable->on_focus_end(base);
}

static void djui_interactable_on_value_change(struct DjuiBase* base) {
    if (base                                == NULL) { return; }
    if (base->interactable                  == NULL) { return; }
    if (base->interactable->on_value_change == NULL) { return; }
    base->interactable->on_value_change(base);
}

static void djui_interactable_on_bind(struct DjuiBase* base) {
    if (base                        == NULL) { return; }
    if (base->interactable          == NULL) { return; }
    if (base->interactable->on_bind == NULL) { return; }
    base->interactable->on_bind(base);
}

static void djui_interactable_cursor_update_active(struct DjuiBase* base) {
    if (!base->visible) { return; }
    if (!base->enabled) { return; }

    static struct DjuiBase* insideParent = NULL;

    if (!djui_cursor_inside_base(base)) { return; }

    if (base->interactable != NULL) {
        sHovered = base;
        insideParent = base;
    } else if (insideParent == NULL) {
        sHovered = NULL;
    }

    // check all children
    struct DjuiBaseChild* child = base->child;
    while (child != NULL) {
        djui_interactable_cursor_update_active(child->base);
        child = child->next;
    }

    if (insideParent == base) {
        insideParent = NULL;
    }
}

bool djui_interactable_is_binding(void) {
    return sInteractableBinding != NULL;
}

void djui_interactable_set_binding(struct DjuiBase* base) {
    sInteractableBinding = base;
    djui_cursor_set_visible(base == NULL);
    if (base == NULL) {
        sIgnoreInteractableUntilCursorReleased = true;
    }
}

void djui_interactable_set_input_focus(struct DjuiBase* base) {
    djui_interactable_on_focus_end(base);
    sInteractableFocus = base;
    djui_interactable_on_focus_begin(base);
    djui_cursor_set_visible(base == NULL);
}

bool djui_interactable_is_input_focus(struct DjuiBase* base) {
    return sInteractableFocus == base;
}

bool djui_interactable_on_key_down(int scancode) {

    bool keyFocused = (sInteractableFocus != NULL)
                   && (sInteractableFocus->interactable != NULL)
                   && (sInteractableFocus->interactable->on_key_down != NULL);

    if (keyFocused) {
        bool consume = sInteractableFocus->interactable->on_key_down(sInteractableFocus, scancode);
        if (consume) {
            sKeyboardHoldDirection = PAD_HOLD_DIR_NONE;
            sKeyboardButtons = 0;
            return true;
        }
    }

    if (scancode == SCANCODE_ESCAPE) {
        // pressed escape button on keyboard
        djui_panel_back();
    }

    if (gDjuiChatBox != NULL && !gDjuiChatBoxFocus) {
        bool pressChat = false;
        for (int i = 0; i < MAX_BINDS; i++) {
            if (scancode == (int)configKeyChat[i]) { pressChat = true; }
        }

        if (pressChat) {
            djui_chat_box_toggle();
            return true;
        }
    }

    switch (scancode) {
        case SCANCODE_UP:    sKeyboardHoldDirection = PAD_HOLD_DIR_UP;    return true;
        case SCANCODE_DOWN:  sKeyboardHoldDirection = PAD_HOLD_DIR_DOWN;  return true;
        case SCANCODE_LEFT:  sKeyboardHoldDirection = PAD_HOLD_DIR_LEFT;  return true;
        case SCANCODE_RIGHT: sKeyboardHoldDirection = PAD_HOLD_DIR_RIGHT; return true;
        case SCANCODE_ENTER: sKeyboardButtons |= PAD_BUTTON_A;            return true;
    }

    return false;
}

void djui_interactable_on_key_up(int scancode) {

    bool keyFocused = (sInteractableFocus != NULL)
                   && (sInteractableFocus->interactable != NULL)
                   && (sInteractableFocus->interactable->on_key_up != NULL);

    if (keyFocused) {
        sInteractableFocus->interactable->on_key_up(sInteractableFocus, scancode);
        sKeyboardHoldDirection = PAD_HOLD_DIR_NONE;
        sKeyboardButtons = 0;
        return;
    }

    OSContPad* pad = &gInteractablePad;
    switch (scancode) {
        case SCANCODE_UP:    if (sKeyboardHoldDirection == PAD_HOLD_DIR_UP)    { sKeyboardHoldDirection = PAD_HOLD_DIR_NONE; pad->stick_y = 0; } break;
        case SCANCODE_DOWN:  if (sKeyboardHoldDirection == PAD_HOLD_DIR_DOWN)  { sKeyboardHoldDirection = PAD_HOLD_DIR_NONE; pad->stick_y = 0; } break;
        case SCANCODE_LEFT:  if (sKeyboardHoldDirection == PAD_HOLD_DIR_LEFT)  { sKeyboardHoldDirection = PAD_HOLD_DIR_NONE; pad->stick_x = 0; } break;
        case SCANCODE_RIGHT: if (sKeyboardHoldDirection == PAD_HOLD_DIR_RIGHT) { sKeyboardHoldDirection = PAD_HOLD_DIR_NONE; pad->stick_x = 0; } break;
        case SCANCODE_ENTER: sKeyboardButtons &= ~PAD_BUTTON_A; break;
    }
}

void djui_interactable_on_text_input(char* text) {
    if (sInteractableFocus == NULL) { return; }
    if (sInteractableFocus->interactable == NULL) { return; }
    if (sInteractableFocus->interactable->on_text_input == NULL) { return; }
    sInteractableFocus->interactable->on_text_input(sInteractableFocus, text);
}

void djui_interactable_update_pad(void) {
    OSContPad* pad = &gInteractablePad;

    pad->button |= sKeyboardButtons;

    static enum PadHoldDirection lastPadHoldDirection = PAD_HOLD_DIR_NONE;
    static clock_t padHoldTimer = 0;

    enum PadHoldDirection padHoldDirection = sKeyboardHoldDirection;
    if (padHoldDirection != PAD_HOLD_DIR_NONE) {
        switch (padHoldDirection) {
            case PAD_HOLD_DIR_UP:    pad->stick_x =   0; pad->stick_y = -64; break;
            case PAD_HOLD_DIR_DOWN:  pad->stick_x =   0; pad->stick_y =  64; break;
            case PAD_HOLD_DIR_LEFT:  pad->stick_x = -64; pad->stick_y =   0; break;
            case PAD_HOLD_DIR_RIGHT: pad->stick_x =  64; pad->stick_y =   0; break;
            default: break;
        }
    } else if (pad->stick_x == 0 && pad->stick_y == 0) {
        padHoldDirection = PAD_HOLD_DIR_NONE;
    } else if (abs(pad->stick_x) > abs(pad->stick_y)) {
        padHoldDirection = (pad->stick_x < 0) ? PAD_HOLD_DIR_LEFT : PAD_HOLD_DIR_RIGHT;
    } else {
        padHoldDirection = (pad->stick_y > 0) ? PAD_HOLD_DIR_UP : PAD_HOLD_DIR_DOWN;
    }

    bool validPadHold = false;
    if (padHoldDirection == PAD_HOLD_DIR_NONE) {
        // nothing to do
    } else if (padHoldDirection != lastPadHoldDirection) {
        padHoldTimer = clock() + CLOCKS_PER_SEC * 0.25f;
        validPadHold = true;
    } else if (clock() > padHoldTimer) {
        padHoldTimer = clock() + CLOCKS_PER_SEC * 0.10f;
        validPadHold = true;
    }

    if (validPadHold && sInteractableFocus == NULL) {
        switch (padHoldDirection) {
            case PAD_HOLD_DIR_UP:    djui_cursor_move( 0, -1); break;
            case PAD_HOLD_DIR_DOWN:  djui_cursor_move( 0,  1); break;
            case PAD_HOLD_DIR_LEFT:  djui_cursor_move(-1,  0); break;
            case PAD_HOLD_DIR_RIGHT: djui_cursor_move( 1,  0); break;
            default: break;
        }
    }

    lastPadHoldDirection = padHoldDirection;
}

void djui_interactable_update(void) {
    // update pad
    djui_interactable_update_pad();

    // prevent pressing buttons when they should be ignored
    int mouseButtons = mouse_window_buttons;
    u16 padButtons = gInteractablePad.button;
    if (sIgnoreInteractableUntilCursorReleased) {
        if ((padButtons & PAD_BUTTON_A) || (mouseButtons & MOUSE_BUTTON_1)) {
            padButtons   &= ~PAD_BUTTON_A;
            mouseButtons &= ~MOUSE_BUTTON_1;
        } else {
            sIgnoreInteractableUntilCursorReleased = false;
        }
    }

    // update focused
    if (sInteractableFocus) {
        u16 mainButtons = PAD_BUTTON_A | PAD_BUTTON_B;
        if ((mouseButtons & MOUSE_BUTTON_1) && !(sLastMouseButtons && MOUSE_BUTTON_1) && !djui_cursor_inside_base(sInteractableFocus)) {
            // clicked outside of focused
            djui_interactable_set_input_focus(NULL);
        } else if ((padButtons & mainButtons) && !(sLastInteractablePad.button & mainButtons)) {
            // pressed main face button
            djui_interactable_set_input_focus(NULL);
        } else {
            djui_interactable_on_focus(sInteractableFocus);
        }
    } else if ((padButtons & PAD_BUTTON_B) && !(sLastInteractablePad.button & PAD_BUTTON_B)) {
        // pressed back button on controller
        djui_panel_back();
    } else if ((padButtons & PAD_BUTTON_START) && !(sLastInteractablePad.button & PAD_BUTTON_START)) {
        // pressed start button
        if (gDjuiPanelPauseCreated) { djui_panel_shutdown(); }
    }

    if (sInteractableBinding != NULL) {
        djui_interactable_on_bind(sInteractableBinding);
    } else if ((padButtons & PAD_BUTTON_A) || (mouseButtons & MOUSE_BUTTON_1)) {
        // cursor down events
        if (sHovered != NULL) {
            sMouseDown = sHovered;
            sHovered = NULL;
            djui_interactable_on_cursor_down_begin(sMouseDown, !mouseButtons);
        } else {
            djui_interactable_on_cursor_down(sMouseDown);
        }
    } else {
        // cursor up event
        if (sMouseDown != NULL) {
            djui_interactable_on_cursor_down_end(sMouseDown);
            sMouseDown = NULL;
        }
        struct DjuiBase* lastHovered = sHovered;
        sHovered = NULL;
        djui_interactable_cursor_update_active(&gDjuiRoot->base);
        if (lastHovered != sHovered) {
            djui_interactable_on_hover_end(lastHovered);
            play_sound(SOUND_MENU_MESSAGE_NEXT_PAGE, gDefaultSoundArgs);
        }
        djui_interactable_on_hover(sHovered);
    }

    sLastInteractablePad = gInteractablePad;
    sLastMouseButtons = mouseButtons;
}

void djui_interactable_hook_hover(struct DjuiBase* base,
                                  void (*on_hover)(struct DjuiBase*),
                                  void (*on_hover_end)(struct DjuiBase*)) {
    struct DjuiInteractable* interactable = base->interactable;
    interactable->on_hover     = on_hover;
    interactable->on_hover_end = on_hover_end;
}

void djui_interactable_hook_cursor_down(struct DjuiBase* base,
                                        void (*on_cursor_down_begin)(struct DjuiBase*, bool),
                                        void (*on_cursor_down)(struct DjuiBase*),
                                        void (*on_cursor_down_end)(struct DjuiBase*)) {
    struct DjuiInteractable* interactable = base->interactable;
    interactable->on_cursor_down_begin = on_cursor_down_begin;
    interactable->on_cursor_down       = on_cursor_down;
    interactable->on_cursor_down_end   = on_cursor_down_end;
}

void djui_interactable_hook_focus(struct DjuiBase* base,
                                        void (*on_focus_begin)(struct DjuiBase*),
                                        void (*on_focus)(struct DjuiBase*, OSContPad*),
                                        void (*on_focus_end)(struct DjuiBase*)) {
    struct DjuiInteractable* interactable = base->interactable;
    interactable->on_focus_begin = on_focus_begin;
    interactable->on_focus       = on_focus;
    interactable->on_focus_end   = on_focus_end;
}

void djui_interactable_hook_click(struct DjuiBase* base,
                                  void (*on_click)(struct DjuiBase*)) {
    struct DjuiInteractable* interactable = base->interactable;
    interactable->on_click = on_click;
}

void djui_interactable_hook_value_change(struct DjuiBase* base,
                                         void (*on_value_change)(struct DjuiBase*)) {
    struct DjuiInteractable* interactable = base->interactable;
    interactable->on_value_change = on_value_change;
}

void djui_interactable_hook_bind(struct DjuiBase* base,
                                 void (*on_bind)(struct DjuiBase*)) {
    struct DjuiInteractable* interactable = base->interactable;
    interactable->on_bind = on_bind;
}

void djui_interactable_hook_key(struct DjuiBase* base,
                                 bool (*on_key_down)(struct DjuiBase*, int),
                                 void (*on_key_up)(struct DjuiBase*, int)) {
    struct DjuiInteractable *interactable = base->interactable;
    interactable->on_key_down = on_key_down;
    interactable->on_key_up   = on_key_up;

}

void djui_interactable_hook_text_input(struct DjuiBase *base,
                                       void (*on_text_input)(struct DjuiBase*, char*)) {
    struct DjuiInteractable *interactable = base->interactable;
    interactable->on_text_input = on_text_input;
}

void djui_interactable_hook_enabled_change(struct DjuiBase *base,
                                           void (*on_enabled_change)(struct DjuiBase*)) {
    struct DjuiInteractable *interactable = base->interactable;
    interactable->on_enabled_change = on_enabled_change;
}

void djui_interactable_create(struct DjuiBase* base) {

    if (base->interactable != NULL) {
        free(base->interactable);
    }

    struct DjuiInteractable* interactable = malloc(sizeof(struct DjuiInteractable));
    memset(interactable, 0, sizeof(struct DjuiInteractable));
    base->interactable = interactable;
}
