#include <mod/amlmod.h>
#include <cmath>
#include <cstdint>

MYMOD(net.esdra.realisticrollover, Realistic Rollover, 1.1, Esdra)

BEGIN_DEPLIST()
    ADD_DEPENDENCY_VER(net.rusjj.aml, 1.3.0)
END_DEPLIST()

// Experimental GTA SA Android 2.00 / ARMv7 source.
// Uses ApplyForce for linear lift + torque, not ApplyTurnForce alone.

struct Vec3 { float x, y, z; };
static inline Vec3 Add(Vec3 a, Vec3 b) { return {a.x+b.x, a.y+b.y, a.z+b.z}; }
static inline Vec3 Mul(Vec3 a, float s) { return {a.x*s, a.y*s, a.z*s}; }
static inline float Dot(Vec3 a, Vec3 b) { return a.x*b.x + a.y*b.y + a.z*b.z; }
static inline float Abs(float x) { return std::fabs(x); }
static inline float Clamp01(float x) { return x < 0.0f ? 0.0f : (x > 1.0f ? 1.0f : x); }

// RenderWare CMatrix basis: right, up, at, position.
struct MatrixSA {
    Vec3 right; uint32_t flagsRight;
    Vec3 up;    uint32_t flagsUp;
    Vec3 at;    uint32_t flagsAt;
    Vec3 pos;   uint32_t flagsPos;
};
struct ColPointSA { Vec3 point; float depth; Vec3 normal; };

using FindPlayerVehicleFn = void* (*)(int playerId, bool includeRemote);
using ApplyForceFn = void (*)(void* physical, Vec3 force, Vec3 offset, bool validTorque);
static FindPlayerVehicleFn FindPlayerVehicleNative = nullptr;
static ApplyForceFn ApplyForceNative = nullptr;
static bool sawGroundContact = false;

static constexpr uintptr_t MATRIX_PTR_OFFSET = 0x14;
static constexpr uintptr_t MOVE_SPEED_OFFSET = 0x48;

static constexpr float BODY_FRICTION_MULTIPLIER = 1.05f;
static constexpr float GROUND_NORMAL_Z_MIN = 0.55f;
static constexpr float MIN_FORWARD_SPEED = 0.72f;
static constexpr float MIN_LATERAL_SPEED = 0.20f;
static constexpr float SPEED_RAMP = 0.85f;
static constexpr float SLIP_RAMP = 0.45f;
static constexpr float MAX_BLOWOVER_FORCE = 8.0f;
static constexpr float SIDE_OFFSET = 0.85f;
static constexpr float REAR_OFFSET = 0.65f;
static constexpr float MIN_UP_AXIS_Z = 0.20f;

DECL_HOOK(bool, PhysicalApplyFriction, void* self, float friction, ColPointSA* cp) {
    if (self && cp && FindPlayerVehicleNative && FindPlayerVehicleNative(0, false) == self) {
        if (cp->normal.z > GROUND_NORMAL_Z_MIN)
            sawGroundContact = true;

        // Small increase only for roof/side contacts; don't increase general tire grip.
        auto* bytes = reinterpret_cast<uint8_t*>(self);
        MatrixSA* matrix = *reinterpret_cast<MatrixSA**>(bytes + MATRIX_PTR_OFFSET);
        if (matrix && std::fabs(cp->normal.z) > GROUND_NORMAL_Z_MIN) {
            const Vec3 rel = {cp->point.x-matrix->pos.x,
                              cp->point.y-matrix->pos.y,
                              cp->point.z-matrix->pos.z};
            const float localX = Dot(rel, matrix->right);
            const float localZ = Dot(rel, matrix->up);
            if (localZ > 0.25f || (std::fabs(localX) > 0.70f && localZ > -0.12f))
                friction *= BODY_FRICTION_MULTIPLIER;
        }
    }
    return PhysicalApplyFriction(self, friction, cp);
}

DECL_HOOK(void, AutomobileProcessControl, void* self) {
    AutomobileProcessControl(self);
    if (!self || !FindPlayerVehicleNative || !ApplyForceNative ||
        FindPlayerVehicleNative(0, false) != self)
        return;

    // Consume contact observed by the collision/friction callback. This prevents
    // the rollover impulse from repeatedly firing while the car is freely airborne.
    const bool grounded = sawGroundContact;
    sawGroundContact = false;
    if (!grounded) return;

    auto* bytes = reinterpret_cast<uint8_t*>(self);
    MatrixSA* matrix = *reinterpret_cast<MatrixSA**>(bytes + MATRIX_PTR_OFFSET);
    Vec3* velocity = reinterpret_cast<Vec3*>(bytes + MOVE_SPEED_OFFSET);
    if (!matrix || !velocity || matrix->up.z < MIN_UP_AXIS_Z) return;

    const float forwardSpeed = Abs(Dot(*velocity, matrix->at));
    const float lateralVelocity = Dot(*velocity, matrix->right);
    const float lateralSpeed = Abs(lateralVelocity);
    if (forwardSpeed < MIN_FORWARD_SPEED || lateralSpeed < MIN_LATERAL_SPEED) return;

    const float speedFactor = Clamp01((forwardSpeed - MIN_FORWARD_SPEED) / SPEED_RAMP);
    const float slipFactor = Clamp01((lateralSpeed - MIN_LATERAL_SPEED) / SLIP_RAMP);
    const float forceAmount = MAX_BLOWOVER_FORCE * speedFactor * slipFactor;
    if (forceAmount < 0.5f) return;

    // Apply upward force on one side and toward the rear. The offset is transformed
    // from vehicle-local axes into world axes, so the force creates roll torque.
    const float side = (lateralVelocity >= 0.0f) ? -1.0f : 1.0f;
    Vec3 offset = Add(Mul(matrix->right, side * SIDE_OFFSET),
                      Mul(matrix->at, -REAR_OFFSET));
    const Vec3 force = {0.0f, 0.0f, forceAmount};
    ApplyForceNative(self, force, offset, true);
}

ON_MOD_LOAD() {
    void* gtasaHandle = aml->GetLibHandle("libGTASA.so");
    if (!gtasaHandle) return;

    FindPlayerVehicleNative = reinterpret_cast<FindPlayerVehicleFn>(
        aml->GetSym(gtasaHandle, "_Z17FindPlayerVehicleib"));
    ApplyForceNative = reinterpret_cast<ApplyForceFn>(
        aml->GetSym(gtasaHandle, "_ZN9CPhysical10ApplyForceE7CVectorS0_b"));
    if (!FindPlayerVehicleNative || !ApplyForceNative) return;

    HOOKSYM(PhysicalApplyFriction, gtasaHandle,
            "_ZN9CPhysical13ApplyFrictionEfR9CColPoint");
    HOOKSYM(AutomobileProcessControl, gtasaHandle,
            "_ZN11CAutomobile14ProcessControlEv");
}
