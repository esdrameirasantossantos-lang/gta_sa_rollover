// ============================================================================
// Realistic Rollover Physics — AML PSDK plugin para GTA San Andreas Android
// ============================================================================
#include <mod/amlmod.h>
#include <mod/logger.h>

#include <aml-psdk/game_sa/Events.h>
#include <aml-psdk/game_sa/entity/PlayerPed.h>
#include <aml-psdk/game_sa/entity/Vehicle.h>
#include <aml-psdk/game_sa/entity/Automobile.h>
#include <aml-psdk/game_sa/enum/SurfaceType.h>
#include <aml-psdk/game_sa/base/Timer.h>
#include <aml-psdk/gta_base/Vector.h>
#include <aml-psdk/game_sa/base/Matrix.h>
#include <cmath>

MYMOD(net.rusjj.psdkexample.realisticrollover, Realistic Rollover Physics, 1.0, Claude)

static const float ROLLOVER_UP_THRESHOLD = 0.55f;
static const float MIN_SPEED_FOR_EFFECT = 0.08f;
static const float SPIN_GAIN = 1.0f;
static const float MAX_EXTRA_SPIN = 1.5f;
static const float SPIN_BLEND_RATE = 1.5f;
static const bool ENABLE_SLIDE_EFFECT = false;    

static float GetSurfaceGrip(unsigned char surface)
{
    switch (surface)
    {
        case SURFACE_TARMAC:
        case SURFACE_TARMAC_FUCKED:
        case SURFACE_TARMAC_REALLYFUCKED:
        case SURFACE_PAVEMENT:
        case SURFACE_PAVEMENT_FUCKED:
        case SURFACE_FUCKED_CONCRETE:
        case SURFACE_PAINTED_GROUND:
            return 0.80f;

        case SURFACE_GRAVEL:
            return 0.55f;

        case SURFACE_GRASS_SHORT_LUSH:
        case SURFACE_GRASS_MEDIUM_LUSH:
        case SURFACE_GRASS_LONG_LUSH:
        case SURFACE_GRASS_SHORT_DRY:
        case SURFACE_GRASS_MEDIUM_DRY:
        case SURFACE_GRASS_LONG_DRY:
        case SURFACE_GOLFGRASS_ROUGH:
        case SURFACE_GOLFGRASS_SMOOTH:
            return 0.35f;

        case SURFACE_STEEP_SLIDYGRASS:
        case SURFACE_STEEP_CLIFF:
            return 0.20f;

        default:
            return 0.55f;
    }
}

static void ProcessRealisticRollover(CVehicle* veh, float dt)
{
    if (!veh) return;

    CPhysical* phys = (CPhysical*)veh;

    if (!phys->bOnSolidSurface) return;

    CMatrix* mat = veh->GetMatrix();
    if (!mat) return;

    float upZ = mat->GetUp().z;

    if (upZ > ROLLOVER_UP_THRESHOLD) return;

    CVector& moveSpeed = phys->m_vecMoveSpeed;
    float linSpeed = moveSpeed.Magnitude();

    if (linSpeed < MIN_SPEED_FOR_EFFECT) return;

    float grip = GetSurfaceGrip(phys->m_nContactSurface);

    CVector& turnSpeed = phys->m_vecTurnSpeed;
    float curSpin = turnSpeed.Magnitude();

    float targetSpin = linSpeed * SPIN_GAIN;
    if (targetSpin > MAX_EXTRA_SPIN) targetSpin = MAX_EXTRA_SPIN;

    CVector axis;
    if (curSpin > 0.05f)
    {
        float invCur = 1.0f / curSpin;
        axis = CVector(turnSpeed.x * invCur, turnSpeed.y * invCur, turnSpeed.z * invCur);
    }
    else
    {
        CVector worldUp(0.0f, 0.0f, 1.0f);
        CVector moveDir(moveSpeed.x / linSpeed, moveSpeed.y / linSpeed, moveSpeed.z / linSpeed);

        CVector rawAxis(
            moveDir.y * worldUp.z - moveDir.z * worldUp.y,
            moveDir.z * worldUp.x - moveDir.x * worldUp.z,
            moveDir.x * worldUp.y - moveDir.y * worldUp.x
        );
        float axisLen = rawAxis.Magnitude();
        if (axisLen < 0.001f) return;
        float inv = 1.0f / axisLen;
        axis = CVector(rawAxis.x * inv, rawAxis.y * inv, rawAxis.z * inv);
    }

    float blend = 1.0f - expf(-SPIN_BLEND_RATE * dt);
    float newSpinMag = curSpin + (targetSpin - curSpin) * blend;
    if (newSpinMag < 0.0f) newSpinMag = 0.0f;
    if (newSpinMag > MAX_EXTRA_SPIN) newSpinMag = MAX_EXTRA_SPIN;

    turnSpeed.x = axis.x * newSpinMag;
    turnSpeed.y = axis.y * newSpinMag;
    turnSpeed.z = axis.z * newSpinMag;

    if (ENABLE_SLIDE_EFFECT)
{
    float frictionDrop = (1.0f - grip) * dt * 1.6f;
    float keepFactor = 1.0f - (grip * dt * 0.9f) + frictionDrop;

    if (keepFactor > 1.0f) keepFactor = 1.0f;
    if (keepFactor < 0.85f) keepFactor = 0.85f;

    moveSpeed.x *= keepFactor;
    moveSpeed.y *= keepFactor;
    moveSpeed.z *= keepFactor;
}
}

ON_MOD_LOAD()
{
    Events::gameProcessEvent += []()
    {
        CPlayerPed* player = FindPlayerPed(-1);
        if (!player) return;

        CVehicle* veh = player->m_pVehicle;
        if (!veh) return;

        float dt = CTimer::GetTimeStepInSeconds();
        if (dt <= 0.0f || dt > 0.2f) dt = 1.0f / 30.0f;

        ProcessRealisticRollover(veh, dt);
    };
}
