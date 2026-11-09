#pragma once
#include "d/actor/d_a_player.h"
struct daAlink_c : daPy_py_c {
    BOOL checkBowWaitAnime() const { return m_bowWait; }
    BOOL checkBowChargeWaitAnime() const { return 0; }
    BOOL checkBowReloadAnime() const { return 0; }
    BOOL checkHookshotReadyAnime() const { return 0; }
    BOOL checkBoomerangReadyAnime() const { return 0; }
    BOOL checkBowShootAnime() const { return m_bowShoot; }
    BOOL checkHookshotShootAnime() const { return 0; }
};
