#include <mod/amlmod.h>
#include <aml-psdk/game_sa/entity/Automobile.h>
#include <cmath>

MYMOD(net.esdra.realisticrollover, Realistic Rollover, 1.7, Esdra)

BEGIN_DEPLIST()
    ADD_DEPENDENCY_VER(net.rusjj.aml, 1.3.0)
END_DEPLIST()

using FindPlayerVehicleFn = CVehicle* (*)(int, bool);
using ApplyTurnForceFn = void (*)(void*, CVector, CVector);

static FindPlayerVehicleFn FindPlayerVehicleNative = nullptr;
static ApplyTurnForceFn ApplyTurnForceNative = nullptr;
static int g_cooldown = 0;

// No friction hook and no traction multiplier.
// ApplyTurnForce takes (force, local point offset). Do NOT pass a world position.
static constexpr float MIN_FORWARD_SPEED = 0.30f;
static constexpr float MIN_LATERAL_SPEED = 0.055f;
static constexpr float SPEED_RAMP = 0.75f;
static constexpr float SLIP_RAMP = 0.28f;
static constexpr float MAX_ROLL_FORCE = 0.22f;
static constexpr float MAX_REAR_LIFT_FORCE = 0.16f;
static constexpr float SIDE_LEVER = 0.72f;
static constexpr float REAR_LEVER = 1.10f;
static constexpr int COOLDOWN_FRAMES = 4;

static float Clamp01(float x) {
    return x < 0.0f ? 0.0f : (x > 1.0f ? 1.0f : x);
}
static float Abs(float x) { return std::fabs(x); }

DECL_HOOK(void, AutomobileProcessControl, CAutomobile* car)
{
    AutomobileProcessControl(car);

    if (!car || !FindPlayerVehicleNative || !ApplyTurnForceNative)
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

    // Apply the extra rollover torque only while the car is reasonably upright.
    // This avoids feeding more torque into a car that is already falling over.
    if (matrix->up.z < 0.60f)
        return;

    const CVector velocity = car->m_vecMoveSpeed;
    const float forwardSpeed = Abs(
        velocity.x * matrix->at.x +
        velocity.y * matrix->at.y +
        velocity.z * matrix->at.z);
    const float lateralVelocity =
        velocity.x * matrix->right.x +
        velocity.y * matrix->right.y +
        velocity.z * matrix->right.z;
    const float lateralSpeed = Abs(lateralVelocity);

    if (forwardSpeed < MIN_FORWARD_SPEED || lateralSpeed < MIN_LATERAL_SPEED)
        return;

    const float speedFactor = Clamp01((forwardSpeed - MIN_FORWARD_SPEED) / SPEED_RAMP);
    const float slipFactor = Clamp01((lateralSpeed - MIN_LATERAL_SPEED) / SLIP_RAMP);
    const float intensity = speedFactor * slipFactor;
    if (intensity < 0.04f)
        return;

    // Local-space offset: X is the car's side axis; Y is its forward/rear axis.
    // A small upward force at the side produces roll torque. The sign follows
    // lateral movement so the effect acts toward the side the car is loading.
    const float side = (lateralVelocity >= 0.0f) ? -1.0f : 1.0f;
    const CVector rollForce(0.0f, 0.0f, MAX_ROLL_FORCE * intensity);
    const CVector rollPoint(side * SIDE_LEVER, -0.10f, 0.0f);
    ApplyTurnForceNative(car, rollForce, rollPoint);

    // A second, weaker upward force at a REAR local offset adds pitch torque,
    // encouraging the rear to rise at high speed/slip without a centre-mass launch.
    if (intensity > 0.58f) {
        const float liftIntensity = Clamp01((intensity - 0.58f) / 0.42f);
        const CVector rearForce(0.0f, 0.0f, MAX_REAR_LIFT_FORCE * liftIntensity);
        const CVector rearPoint(0.0f, -REAR_LEVER, 0.0f);
        ApplyTurnForceNative(car, rearForce, rearPoint);
    }

    g_cooldown = COOLDOWN_FRAMES;
}

ON_MOD_LOAD()
{
    void* handle = aml->GetLibHandle("libGTASA.so");
    if (!handle)
        return;

    FindPlayerVehicleNative = reinterpret_cast<FindPlayerVehicleFn>(
        aml->GetSym(handle, "_Z17FindPlayerVehicleib"));
    ApplyTurnForceNative = reinterpret_cast<ApplyTurnForceFn>(
        aml->GetSym(handle, "_ZN9CPhysical14ApplyTurnForceE7CVectorS0_"));

    if (!FindPlayerVehicleNative || !ApplyTurnForceNative)
        return;

    HOOKSYM(AutomobileProcessControl, handle,
            "_ZN11CAutomobile14ProcessControlEv");
}
