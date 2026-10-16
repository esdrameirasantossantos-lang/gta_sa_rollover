#include <mod/amlmod.h>
#include <aml-psdk/game_sa/entity/Automobile.h>
#include <cmath>

MYMOD(net.esdra.realisticrollover, Realistic Rollover, 1.8, Esdra)

BEGIN_DEPLIST()
    ADD_DEPENDENCY_VER(net.rusjj.aml, 1.3.0)
END_DEPLIST()

using FindPlayerVehicleFn = CVehicle* (*)(int, bool);
using ApplyForceFn = void (*)(void*, CVector, CVector, bool);

static FindPlayerVehicleFn FindPlayerVehicleNative = nullptr;
static ApplyForceFn ApplyForceNative = nullptr;
static int g_cooldown = 0;

// No friction hook and no traction changes. This version focuses only on
// lifting the rear during a fast corner; it does not inject ApplyTurnForce.
static constexpr float MIN_FORWARD_SPEED = 0.32f;
static constexpr float MIN_LATERAL_SPEED = 0.045f;
static constexpr float SPEED_RAMP = 0.75f;
static constexpr float SLIP_RAMP = 0.24f;
static constexpr float MAX_LIFT_FORCE = 1.25f;
static constexpr float REAR_OFFSET = 1.15f;
static constexpr int COOLDOWN_FRAMES = 7;

static float Clamp01(float x) {
    return x < 0.0f ? 0.0f : (x > 1.0f ? 1.0f : x);
}
static float Abs(float x) { return std::fabs(x); }

DECL_HOOK(void, AutomobileProcessControl, CAutomobile* car)
{
    AutomobileProcessControl(car);

    if (!car || !FindPlayerVehicleNative || !ApplyForceNative)
        return;
    if (FindPlayerVehicleNative(0, false) != car)
        return;

    if (g_cooldown > 0) {
        --g_cooldown;
        return;
    }

    CMatrix* matrix = car->m_matrix;
    if (!matrix)
        return;

    // Don't keep adding lift once the car is already substantially rolled over.
    if (matrix->up.z < 0.25f)
        return;

    const CVector velocity = car->m_vecMoveSpeed;
    const float forwardSpeed = Abs(
        velocity.x * matrix->at.x +
        velocity.y * matrix->at.y +
        velocity.z * matrix->at.z);
    const float lateralSpeed = Abs(
        velocity.x * matrix->right.x +
        velocity.y * matrix->right.y +
        velocity.z * matrix->right.z);

    if (forwardSpeed < MIN_FORWARD_SPEED || lateralSpeed < MIN_LATERAL_SPEED)
        return;

    const float speedFactor = Clamp01(
        (forwardSpeed - MIN_FORWARD_SPEED) / SPEED_RAMP);
    const float slipFactor = Clamp01(
        (lateralSpeed - MIN_LATERAL_SPEED) / SLIP_RAMP);
    const float intensity = speedFactor * slipFactor;
    if (intensity < 0.03f)
        return;

    // Apply a small upward force at a world-space point behind the car's
    // centre. This directly adds lift and a pitch moment at the rear.
    const CVector rearWorldPoint(
        matrix->pos.x - matrix->at.x * REAR_OFFSET,
        matrix->pos.y - matrix->at.y * REAR_OFFSET,
        matrix->pos.z - matrix->at.z * REAR_OFFSET);
    const CVector upwardForce(0.0f, 0.0f, MAX_LIFT_FORCE * intensity);

    ApplyForceNative(car, upwardForce, rearWorldPoint, true);
    g_cooldown = COOLDOWN_FRAMES;
}

ON_MOD_LOAD()
{
    void* handle = aml->GetLibHandle("libGTASA.so");
    if (!handle)
        return;

    FindPlayerVehicleNative = reinterpret_cast<FindPlayerVehicleFn>(
        aml->GetSym(handle, "_Z17FindPlayerVehicleib"));
    ApplyForceNative = reinterpret_cast<ApplyForceFn>(
        aml->GetSym(handle, "_ZN9CPhysical10ApplyForceE7CVectorS0_b"));

    if (!FindPlayerVehicleNative || !ApplyForceNative)
        return;

    HOOKSYM(AutomobileProcessControl, handle,
            "_ZN11CAutomobile14ProcessControlEv");
}
