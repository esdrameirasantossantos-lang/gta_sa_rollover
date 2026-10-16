#include <mod/amlmod.h>
#include <aml-psdk/game_sa/entity/Automobile.h>
#include <cmath>

MYMOD(net.esdra.realisticrollover, Realistic Rollover, 1.6, Esdra)

BEGIN_DEPLIST()
    ADD_DEPENDENCY_VER(net.rusjj.aml, 1.3.0)
END_DEPLIST()

using FindPlayerVehicleFn = CVehicle* (*)(int, bool);
using ApplyTurnForceFn = void (*)(void*, CVector, CVector);

static FindPlayerVehicleFn FindPlayerVehicleNative = nullptr;
static ApplyTurnForceFn ApplyTurnForceNative = nullptr;
static int g_cooldown = 0;

// No friction hook or traction multiplier in this version.
// These are deliberately conservative first-test values.
static constexpr float MIN_FORWARD_SPEED = 0.28f;
static constexpr float MIN_LATERAL_SPEED = 0.075f;
static constexpr float SPEED_RAMP = 0.85f;
static constexpr float SLIP_RAMP = 0.32f;
static constexpr float MAX_ROLL_FORCE = 1.35f;
static constexpr float MAX_REAR_LIFT_FORCE = 1.10f;
static constexpr float SIDE_LEVER = 0.90f;
static constexpr float REAR_LEVER = 1.25f;
static constexpr int COOLDOWN_FRAMES = 5;

static float Clamp01(float x)
{
    return x < 0.0f ? 0.0f : (x > 1.0f ? 1.0f : x);
}

static float Abs(float x) { return std::fabs(x); }

static CVector AddScaled(const CVector& p, const CVector& axis, float scale)
{
    return CVector(p.x + axis.x * scale,
                   p.y + axis.y * scale,
                   p.z + axis.z * scale);
}

DECL_HOOK(void, AutomobileProcessControl, CAutomobile* car)
{
    // Preserve original GTA vehicle processing.
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

    // Do not keep injecting torque after the car is already upside down.
    // An upright car has matrix->up.z close to 1.0.
    if (matrix->up.z < 0.30f)
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

    const float speedFactor = Clamp01(
        (forwardSpeed - MIN_FORWARD_SPEED) / SPEED_RAMP);
    const float slipFactor = Clamp01(
        (lateralSpeed - MIN_LATERAL_SPEED) / SLIP_RAMP);
    const float intensity = speedFactor * slipFactor;
    if (intensity < 0.02f)
        return;

    // Lateral motion chooses which side receives the upward force.
    // ApplyTurnForce argument order is FORCE first, then WORLD-SPACE POINT.
    // An upward force at the side creates roll torque without adding a direct
    // linear launch impulse, and does not alter tire friction/traction.
    const float side = (lateralVelocity >= 0.0f) ? -1.0f : 1.0f;
    const CVector rollPoint = AddScaled(
        AddScaled(matrix->pos, matrix->right, side * SIDE_LEVER),
        matrix->at, -0.10f);
    const CVector rollForce(0.0f, 0.0f, MAX_ROLL_FORCE * intensity);
    ApplyTurnForceNative(car, rollForce, rollPoint);

    // At stronger speed + lateral slip, add a separate, smaller upward force
    // at the rear point. Its lever arm encourages the rear to rise (pitch-over)
    // instead of launching the vehicle's centre of mass upward.
    if (intensity > 0.48f) {
        const CVector rearPoint = AddScaled(matrix->pos, matrix->at, -REAR_LEVER);
        const float liftIntensity = Clamp01((intensity - 0.48f) / 0.52f);
        const CVector rearForce(0.0f, 0.0f, MAX_REAR_LIFT_FORCE * liftIntensity);
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
