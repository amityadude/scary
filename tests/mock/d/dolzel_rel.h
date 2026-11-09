#pragma once
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <functional>
typedef int16_t s16; typedef uint8_t u8; typedef uint16_t u16; typedef uint32_t u32; typedef int32_t s32; typedef int8_t s8; typedef float f32;
typedef int BOOL;
#define TRUE 1
#define FALSE 0
struct cXyz { float x = 0, y = 0, z = 0; };
struct csXyz { s16 x = 0, y = 0, z = 0; };
struct J3DAnmTransform { int id; };
struct J3DFrameCtrl { enum { EMode_NONE = 0, EMode_LOOP = 2 }; };
inline void* dComIfG_getObjectRes(const char*, int id) { static J3DAnmTransform t; t.id = id; return &t; }
struct mDoExt_McaMorfSO {
    int curId = -1; int attr = 0; float morf = 0, rate = 1, start = 0, frame = 0; int setAnmCalls = 0;
    void setAnm(J3DAnmTransform* a, int at, f32 m, f32 r, f32 s, f32) { curId = a->id; attr = at; morf = m; rate = r; start = s; frame = s; ++setAnmCalls; }
    void setPlaySpeed(f32 s) { rate = s; }
    f32 getPlaySpeed() { return rate; }
    f32 getFrame() { return frame; }
};
struct fopAc_ac_c {
    struct { cXyz pos; csXyz angle; } current;
    csXyz shape_angle; cXyz speed; f32 speedF = 0; f32 gravity = -5; s16 health = 100;
};
struct fopEn_enemy_c : fopAc_ac_c {};
