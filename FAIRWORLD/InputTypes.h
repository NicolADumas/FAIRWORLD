#pragma once

namespace fw {
    // L'alfabeto universale per ogni periferica fisica del Sistema Operativo
    enum class InputID {
        NONE, // Usato anche per indicare "Nessun Modificatore Richiesto"
        
        // --- MOUSE ---
        MOUSE_LEFT,
        MOUSE_RIGHT,
        MOUSE_MIDDLE,
        
        // --- TASTIERA ---
        KEY_W, KEY_A, KEY_S, KEY_D, 
        KEY_SPACE, KEY_ESC, KEY_ENTER,
        KEY_SHIFT, KEY_CTRL, KEY_ALT, // Modificatori

        // --- FRECCE ---
        KEY_UP, KEY_DOWN, KEY_LEFT, KEY_RIGHT,

        // --- GAMEPAD (XInput layout) ---
        PAD_FACE_DOWN,   // A / Croce
        PAD_FACE_RIGHT,  // B / Cerchio
        PAD_FACE_LEFT,   // X / Quadrato
        PAD_FACE_UP,     // Y / Triangolo
        PAD_TRIGGER_L,   // L2
        PAD_TRIGGER_R,   // R2
        PAD_BUMPER_L,    // L1
        PAD_BUMPER_R,    // R1
        PAD_DPAD_UP,
        PAD_DPAD_DOWN,
        PAD_DPAD_LEFT,
        PAD_DPAD_RIGHT,
        PAD_THUMB_L,
        PAD_THUMB_R,
        PAD_START,
        PAD_SELECT
    };
}
