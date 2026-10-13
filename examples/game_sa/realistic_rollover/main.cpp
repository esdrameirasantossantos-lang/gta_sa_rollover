#include <mod/amlmod.h>
#include <cmath>
#include <cstdint>

MYMOD(net.esdra.realisticrollover, Realistic Rollover, 0.4, Esdra)

BEGIN_DEPLIST()
    ADD_DEPENDENCY_VER(net.rusjj.aml, 1.3.0)
END_DEPLIST()

// GTA SA Android 2.00 / armeabi-v7a prototype.
// This pass targets the missing rear-lift torque. It deliberately does NOT
// alter native friction yet; body-contact friction must be added separately
// after the collision point classification is verified.

struct Vec3 {
    float x, y, z;
};

static inline Vec3 Mul(Vec3 v, float s) { return {v.x*s, v.y*s, v.z*s}; }
static inline float Dot(Vec3 a, Vec3 b) {
    return a.x*b.x + a.y*b.y + a.z*b.z;
}
static inline float Clamp01(float x) {
    if (x < 0.0f) return 0.0f;
    if (x > 1.0f) return 1.0f;
    return x;
}

// RenderWare matrix layout: right, forward (RwMatrix up), world-up (RwMatrix at), position.
struct MatrixSA {
    Vec3 right;   uint32_t flagsRight;
    Vec3 forward; uint32_t flagsForward;
    Vec3 up;      uint32_t flagsUp;
    Vec3 pos;     uint32_t flagsPos;
};

using FindPlayerVehicleFn = void* (*)(int playerId, bool includeRemote);
using ApplyTurnForceFn = void (*)(void* physical, Vec3 localPoint, Vec3 worldImpulse);

static FindPlayerVehicleFn FindPlayerVehicleNative = nullptr;
static ApplyTurnForceFn ApplyTurnForceNative = nullptr;

// Verified from this uploaded v2.00 binary's ApplyMoveForce/ApplyTurnForce:
// CPlaceable matrix pointer is at +0x14; CPhysical move speed is at +0x48.
static constexpr uintptr_t MATRIX_PTR_OFFSET = 0x14;
static constexpr uintptr_t MOVE_SPEED_OFFSET = 0x48;

// Blowover tuning. GTA velocity is in game units/frame, not km/h.
static constexpr float MIN_FORWARD_SPEED = 0.48f;
static constexpr float MIN_LATERAL_SPEED = 0.12f;
static constexpr float SPEED_RAMP = 1.20f;
static constexpr float SLIP_RAMP = 0.38f;
static constexpr float BASE_IMPULSE = 28.0f;
static constexpr float MAX_IMPULSE = 220.0f;
static constexpr float REAR_LOCAL_Y = -1.45f;
static constexpr float REAR_LOCAL_Z = 0.10f;
static constexpr float MIN_UP_AXIS_Z = 0.45f;

DECL_HOOK(void, AutomobileProcessControl, void* self)
{
    // Always run the game's original vehicle control first.
    AutomobileProcessControl(self);

    if (!self || !FindPlayerVehicleNative || !ApplyTurnForceNative)
        return;

    // Only affect the player's vehicle. Traffic vehicles remain unchanged.
    void* playerVehicle = FindPlayerVehicleNative(0, false);
    if (!playerVehicle || playerVehicle != self)
        return;

    auto* bytes = reinterpret_cast<uint8_t*>(self);
    MatrixSA* matrix = *reinterpret_cast<MatrixSA**>(bytes + MATRIX_PTR_OFFSET);
    Vec3* velocity = reinterpret_cast<Vec3*>(bytes + MOVE_SPEED_OFFSET);
    if (!matrix || !velocity)
        return;

    // Stop adding rollover torque after the car has tipped significantly.
    // This prevents the repeated impulse from making the vehicle spin forever.
    if (matrix->up.z < MIN_UP_AXIS_Z)
        return;

    const float forwardSpeed = std::fabs(Dot(*velocity, matrix->forward));
    const float lateralSpeed = std::fabs(Dot(*velocity, matrix->right));

    if (forwardSpeed < MIN_FORWARD_SPEED || lateralSpeed < MIN_LATERAL_SPEED)
        return;

    const float speedFactor = Clamp01((forwardSpeed - MIN_FORWARD_SPEED) / SPEED_RAMP);
    const float slipFactor = Clamp01((lateralSpeed - MIN_LATERAL_SPEED) / SLIP_RAMP);

    // A vertical impulse at a point behind the centre of mass produces pitch
    // torque that lifts the rear. ApplyTurnForce expects a vehicle-local point
    // as its first vector and a world-space impulse vector as its second.
    float impulse = BASE_IMPULSE + (MAX_IMPULSE - BASE_IMPULSE) * speedFactor * slipFactor;
    if (impulse > MAX_IMPULSE) impulse = MAX_IMPULSE;

    const Vec3 rearPoint = {0.0f, REAR_LOCAL_Y, REAR_LOCAL_Z};
    const Vec3 upwardImpulse = {0.0f, 0.0f, impulse};
    ApplyTurnForceNative(self, rearPoint, upwardImpulse);
}

ON_MOD_LOAD()
{
    void* gtasaHandle = aml->GetLibHandle("libGTASA.so");
    if (!gtasaHandle)
        return;

    FindPlayerVehicleNative = reinterpret_cast<FindPlayerVehicleFn>(
        aml->GetSym(gtasaHandle, "_Z17FindPlayerVehicleib"));
    ApplyTurnForceNative = reinterpret_cast<ApplyTurnForceFn>(
        aml->GetSym(gtasaHandle, "_ZN9CPhysical14ApplyTurnForceE7CVectorS0_"));

    if (!FindPlayerVehicleNative || !ApplyTurnForceNative)
        return;

    HOOKSYM(AutomobileProcessControl, gtasaHandle,
            "_ZN11CAutomobile14ProcessControlEv");
}
