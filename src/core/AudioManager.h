#pragma once
#define MINIAUDIO_IMPLEMENTATION
#include "miniaudio.h"
#include <iostream>

class AudioManager {
public:
    bool Init() {
        m_result = ma_engine_init(nullptr, &m_engine);
        if (m_result != MA_SUCCESS) {
            std::cerr << "Impossibile inizializzare il motore audio!" << std::endl;
            return false;
        }
        return true;
    }

    void UpdateListener(ma_vec3f position, ma_vec3f forward, ma_vec3f up) {
        // Aggiorna la posizione delle orecchie virtuali del giocatore nel mondo VR
        ma_engine_listener_set_position(&m_engine, 0, position.x, position.y, position.z);
        ma_engine_listener_set_direction(&m_engine, 0, forward.x, forward.y, forward.z);
        ma_engine_listener_set_world_up(&m_engine, 0, up.x, up.y, up.z);
    }

    void PlaySound3D(const char* filepath, ma_vec3f soundPos) {
        // Riproduce un suono ancorato a un punto preciso del mondo reale
        ma_engine_play_sound_ex(&m_engine, filepath, nullptr, nullptr, soundPos.x, soundPos.y, soundPos.z);
    }

    void Shutdown() {
        ma_engine_uninit(&m_engine);
    }

private:
    ma_result m_result;
    ma_engine m_engine;
};
