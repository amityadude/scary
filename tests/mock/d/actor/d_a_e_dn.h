#pragma once
#include "d/dolzel_rel.h"
struct MockAcch {
    bool wall = false; bool ground = true;
    int ChkWallHit() { return wall; }
    int ChkGroundHit() { return ground; }
};
struct e_dn_class {
    fopEn_enemy_c actor;
    s16 action = 0, mode = 0; s16 timer[4] = {0, 0, 0, 0};
    int learn = 0; f32 field_0x74c = 0, field_0x708 = 0, field_0x704 = 0, field_0x7e8 = 0; s16 field_0x70e = 0; u8 field_0x710 = 0;
    u8 status = 0; f32 pl_range = 100000.0f; s16 unk_timer_1 = 0, invulnerability_timer = 0; u8 guard_flag = 0;
    mDoExt_McaMorfSO* anm_p = nullptr; int anm_no = -1; MockAcch objacch;
};
