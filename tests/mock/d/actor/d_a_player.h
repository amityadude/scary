#pragma once
#include "d/dolzel_rel.h"
struct daPy_py_c : fopAc_ac_c {
    enum {
        CUT_TYPE_NM_VERTICAL = 1, CUT_TYPE_NM_STAB = 2, CUT_TYPE_NM_RIGHT = 3, CUT_TYPE_NM_LEFT = 4, CUT_TYPE_HEAD_JUMP = 5,
        CUT_TYPE_FINISH_LEFT = 6, CUT_TYPE_FINISH_VERTICAL = 7, CUT_TYPE_TURN_RIGHT = 8, CUT_TYPE_JUMP = 0x0A,
        CUT_TYPE_DASH_LEFT_SIDESTEP = 0x10, CUT_TYPE_DASH_RIGHT_SIDESTEP = 0x11, CUT_TYPE_LARGE_JUMP_INIT = 0x12,
        CUT_TYPE_LARGE_JUMP = 0x13, CUT_TYPE_LARGE_JUMP_FINISH = 0x14, CUT_TYPE_FINISH_RIGHT = 0x15, CUT_TYPE_TURN_LEFT = 0x16,
        CUT_TYPE_LARGE_TURN_LEFT = 0x17, CUT_TYPE_LARGE_TURN_RIGHT = 0x18, CUT_TYPE_MORTAL_DRAW_A = 0x1A, CUT_TYPE_TWIRL = 0x1E,
        CUT_TYPE_MORTAL_DRAW_B = 0x1F, CUT_TYPE_FINISH_STAB = 0x20, CUT_TYPE_COMBO_STAB = 0x21, CUT_TYPE_DASH_LEFT = 0x25,
        CUT_TYPE_DASH_RIGHT = 0x26, CUT_TYPE_DOWN = 0x27, CUT_TYPE_UNK_28 = 0x28, CUT_TYPE_GUARD_ATTACK = 0x29,
        CUT_TYPE_WOLF_JUMP_S = 0x31, CUT_TYPE_WOLF_JUMP_S_FINISH = 0x32, CUT_TYPE_DASH_CHARGE = 0x38, CUT_TYPE_WOLF_JUMP = 0x39,
    };
    // scripted by the simulation
    u8 m_cut = 0; bool m_at = false, m_guard = false, m_wolf = false, m_arrowEnd = false;
    bool m_bowWait = false, m_bowShoot = false;
    u8 getCutType() const { return m_cut; }
    BOOL getCutAtFlg() const { return m_at; }
    BOOL checkPlayerGuard() const { return m_guard; }
    BOOL checkWolf() const { return m_wolf; }
    BOOL checkArrowChargeEnd() const { return m_arrowEnd; }
};
inline daPy_py_c*& mock_player() { static daPy_py_c* p = nullptr; return p; }
inline daPy_py_c* daPy_getPlayerActorClass() { return mock_player(); }
