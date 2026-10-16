#include <mod/amlmod.h>
#include <aml-psdk/game_sa/entity/Automobile.h>
#include <aml-psdk/game_sa/base/ColModel.h>
#include <cmath>

MYMOD(net.esdra.realisticrollover, Realistic Rollover, 1.5, Esdra)

BEGIN_DEPLIST()
    ADD_DEPENDENCY_VER(net.rusjj.aml, 1.3.0)
END_DEPLIST()

using FindPlayerVehicleFn = CVehicle* (*)(int, bool);
using ApplyTurnForceFn = void (*)(void*, CVector, CVector);

static FindPlayerVehicleFn FindPlayerVehicleNative = nullptr;
static ApplyTurnForceFn ApplyTurnForceNative = nullptr;

// Only set when a collision point is classified as BODY contact, not wheel/ground contact.
static bool g_bodyContact = false;
static CVector g_bodyContactPoint(0.0f, 0.0f, 0.0f);
static int g_cooldown = 0;

static constexpr float BODY_FRICTION = 1.04f;
static constexpr float MIN_FORWARD = 0.35f;
static constexpr float MIN_LATERAL = 0.10f;
static constexpr float SLIP_RAMP = 0.65f;
static constexpr float ROLL_GAIN = 0.32f;
static constexpr float MAX_ROLL_FORCE = 0.30f;
static constexpr float MIN_TILT_UP_Z = 0.96f; // require some real body tilt before torque is added

static float Clamp01(float x) {
    return x < 0.0f ? 0.0f : (x > 1.0f ? 1.0f : x);
}
static float Abs(float x) { return std::fabs(x); }

DECL_HOOK(bool, PhysicalApplyFriction, void* self, float friction, CColPoint& cp)
{
    if (FindPlayerVehicleNative) {
        CVehicle* playerVehicle = FindPlayerVehicleNative(0, false);
        if (playerVehicle && self == playerVehicle) {
            CAutomobile* car = static_cast<CAutomobile*>(playerVehicle);
            CMatrix* matrix = car->m_matrix;
            if (matrix) {
                CVector rel(
                    cp.m_vecPoint.x - matrix->pos.x,
                    cp.m_vecPoint.y - matrix->pos.y,
                    cp.m_vecPoint.z - matrix->pos.z);

                const float localX = rel.x * matrix->right.x + rel.y * matrix->right.y + rel.z * matrix->right.z;
                const float localZ = rel.x * matrix->up.x + rel.y * matrix->up.y + rel.z * matrix->up.z;
                const float normalUp = Abs(cp.m_vecNormal.x * matrix->up.x + cp.m_vecNormal.y * matrix->up.y + cp.m_vecNormal.z * matrix->up.z);
                const float normalSide = Abs(cp.m_vecNormal.x * matrix->right.x + cp.m_vecNormal.y * matrix->right.y + cp.m_vecNormal.z * matrix->right.z);

                const bool roofContact = normalUp > 0.55f && localZ > 0.25f;
                const bool sideContact = normalSide > 0.55f && Abs(localX) > 0.55f && localZ > -0.12f;
                const bool bodyContact = roofContact || sideContact;

                if (bodyContact) {
                    friction *= BODY_FRICTION;
                    g_bodyContact = true;
                    g_bodyContactPoint = cp.m_vecPoint; // world-space collision point
                }
            }
        }
    }
    return PhysicalApplyFriction(self, friction, cp);
}

DECL_HOOK(void, AutomobileProcessControl, CAutomobile* car)
{
    AutomobileProcessControl(car);

    if (!car || !FindPlayerVehicleNative || !ApplyTurnForceNative ||
        FindPlayerVehicleNative(0, false) != car)
        return;

    const bool bodyContact = g_bodyContact;
    const CVector contactPoint = g_bodyContactPoint;
    g_bodyContact = false;

    if (!bodyContact)
        return;

    if (g_cooldown > 0) {
        --g_cooldown;
        return;
    }

    CMatrix* matrix = car->m_matrix;
    if (!matrix)
        return;

    // Avoid applying extra torque while the vehicle is still fully upright.
    if (matrix->up.z > MIN_TILT_UP_Z || matrix->up.z < -0.25f)
        return;

    const CVector velocity = car->m_vecMoveSpeed;
    const float forwardSpeed = Abs(velocity.x * matrix->at.x + velocity.y * matrix->at.y + velocity.z * matrix->at.z);
    const float lateralVelocity = velocity.x * matrix->right.x + velocity.y * matrix->right.y + velocity.z * matrix->right.z;
    const float lateralSpeed = Abs(lateralVelocity);

    if (forwardSpeed < MIN_FORWARD || lateralSpeed < MIN_LATERAL)
        return;

    const float slipFactor = Clamp01((lateralSpeed - MIN_LATERAL) / SLIP_RAMP);
    float torqueForce = ROLL_GAIN * slipFactor;
    if (torqueForce > MAX_ROLL_FORCE)
        torqueForce = MAX_ROLL_FORCE;
    if (torqueForce < 0.025f)
        return;

    // ApplyTurnForce changes angular motion only; unlike ApplyForce, it does not
    // add the same direct upward linear impulse that launched the car in v1.4.
    // Arguments are world contact point, then world-space force vector.
    const CVector torqueVector(0.0f, 0.0f, torqueForce);
    ApplyTurnForceNative(car, contactPoint, torqueVector);
    g_cooldown = 6;
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

    HOOKSYM(PhysicalApplyFriction, handle,
            "_ZN9CPhysical13ApplyFrictionEfR9CColPoint");
    HOOKSYM(AutomobileProcessControl, handle,
            "_ZN11CAutomobile14ProcessControlEv");
}
