#include <mod/amlmod.h>
#include <cmath>
#include <cstdint>

MYMOD(net.esdra.realisticrollover, Realistic Rollover, 1.0, Esdra)

BEGIN_DEPLIST()
    ADD_DEPENDENCY_VER(net.rusjj.aml, 1.3.0)
END_DEPLIST()

// GTA SA Android 2.00 / ARMv7. Experimental source; compile and test in-game.
// The friction hook only changes the friction coefficient. It NEVER applies
// extra forces from inside the collision/friction callback.

struct Vec3 { float x, y, z; };
static inline Vec3 Sub(Vec3 a, Vec3 b) { return {a.x-b.x, a.y-b.y, a.z-b.z}; }
static inline Vec3 Mul(Vec3 a, float s) { return {a.x*s, a.y*s, a.z*s}; }
static inline float Dot(Vec3 a, Vec3 b) { return a.x*b.x + a.y*b.y + a.z*b.z; }
static inline float Clamp01(float x) { return x < 0.0f ? 0.0f : (x > 1.0f ? 1.0f : x); }

// CMatrix basis layout: right, up, at/forward, position.
struct MatrixSA {
    Vec3 right; uint32_t flagsRight;
    Vec3 up;    uint32_t flagsUp;
    Vec3 at;    uint32_t flagsAt;
    Vec3 pos;   uint32_t flagsPos;
};

struct ColPointSA {
    Vec3 point;
    float depth;
    Vec3 normal;
};

using FindPlayerVehicleFn = void* (*)(int playerId, bool includeRemote);
// CPhysical::ApplyTurnForce(force, point-offset).
using ApplyTurnForceFn = void (*)(void* physical, Vec3 force, Vec3 pointOffset);
static FindPlayerVehicleFn FindPlayerVehicleNative = nullptr;
static ApplyTurnForceFn ApplyTurnForceNative = nullptr;

static constexpr uintptr_t MATRIX_PTR_OFFSET = 0x14;
static constexpr uintptr_t MOVE_SPEED_OFFSET = 0x48;

// Keep body friction restrained to avoid turning the collision into a sticky stop.
static constexpr float BODY_FRICTION_MULTIPLIER = 1.15f;
static constexpr float GROUND_NORMAL_Z_MIN = 0.65f;
static constexpr float ROOF_LOCAL_Z_MIN = 0.25f;
static constexpr float SIDE_LOCAL_X_MIN = 0.70f;
static constexpr float SIDE_LOCAL_Z_MIN = -0.12f;

// Blowover requires both forward motion and lateral slip; tune after a safe test.
static constexpr float MIN_FORWARD_SPEED = 0.78f;
static constexpr float MIN_LATERAL_SPEED = 0.24f;
static constexpr float SPEED_RAMP = 0.95f;
static constexpr float SLIP_RAMP = 0.55f;
static constexpr float MAX_BLOWOVER_FORCE = 55.0f;
static constexpr float REAR_OFFSET = 1.45f;
static constexpr float REAR_UP_OFFSET = 0.10f;
static constexpr float MIN_UP_AXIS_Z = 0.70f;

static bool IsBodyGroundContact(void* self, const ColPointSA* cp) {
    if (!self || !cp || std::fabs(cp->normal.z) < GROUND_NORMAL_Z_MIN)
        return false;

    auto* bytes = reinterpret_cast<uint8_t*>(self);
    MatrixSA* matrix = *reinterpret_cast<MatrixSA**>(bytes + MATRIX_PTR_OFFSET);
    if (!matrix) return false;

    const Vec3 relative = Sub(cp->point, matrix->pos);
    const float localX = Dot(relative, matrix->right);
    const float localZ = Dot(relative, matrix->up);
    const bool roofContact = localZ > ROOF_LOCAL_Z_MIN;
    const bool sideContact = std::fabs(localX) > SIDE_LOCAL_X_MIN &&
                             localZ > SIDE_LOCAL_Z_MIN;
    return roofContact || sideContact;
}

DECL_HOOK(bool, PhysicalApplyFriction, void* self, float friction, ColPointSA* cp) {
    if (self && cp && FindPlayerVehicleNative &&
        FindPlayerVehicleNative(0, false) == self && IsBodyGroundContact(self, cp)) {
        friction *= BODY_FRICTION_MULTIPLIER;
    }
    // No ApplyTurnForce call here: this hook can run repeatedly during collision solving.
    return PhysicalApplyFriction(self, friction, cp);
}

DECL_HOOK(void, AutomobileProcessControl, void* self) {
    AutomobileProcessControl(self);

    if (!self || !FindPlayerVehicleNative || !ApplyTurnForceNative ||
        FindPlayerVehicleNative(0, false) != self)
        return;

    auto* bytes = reinterpret_cast<uint8_t*>(self);
    MatrixSA* matrix = *reinterpret_cast<MatrixSA**>(bytes + MATRIX_PTR_OFFSET);
    Vec3* velocity = reinterpret_cast<Vec3*>(bytes + MOVE_SPEED_OFFSET);
    if (!matrix || !velocity || matrix->up.z < MIN_UP_AXIS_Z)
        return;

    const float forwardSpeed = std::fabs(Dot(*velocity, matrix->at));
    const float lateralSpeed = std::fabs(Dot(*velocity, matrix->right));
    if (forwardSpeed < MIN_FORWARD_SPEED || lateralSpeed < MIN_LATERAL_SPEED)
        return;

    const float speedFactor = Clamp01((forwardSpeed - MIN_FORWARD_SPEED) / SPEED_RAMP);
    const float slipFactor = Clamp01((lateralSpeed - MIN_LATERAL_SPEED) / SLIP_RAMP);
    const float forceAmount = MAX_BLOWOVER_FORCE * speedFactor * slipFactor;
    if (forceAmount < 1.0f)
        return;

    // Apply a single bounded upward force at the rear offset once per control frame.
    // This is separate from friction to avoid repeated impulses from collision contacts.
    const Vec3 force = {0.0f, 0.0f, forceAmount};
    const Vec3 rearOffset = {0.0f, -REAR_OFFSET, REAR_UP_OFFSET};
    ApplyTurnForceNative(self, force, rearOffset);
}

ON_MOD_LOAD() {
    void* gtasaHandle = aml->GetLibHandle("libGTASA.so");
    if (!gtasaHandle) return;

    FindPlayerVehicleNative = reinterpret_cast<FindPlayerVehicleFn>(
        aml->GetSym(gtasaHandle, "_Z17FindPlayerVehicleib"));
    ApplyTurnForceNative = reinterpret_cast<ApplyTurnForceFn>(
        aml->GetSym(gtasaHandle, "_ZN9CPhysical14ApplyTurnForceE7CVectorS0_"));
    if (!FindPlayerVehicleNative || !ApplyTurnForceNative) return;

    HOOKSYM(PhysicalApplyFriction, gtasaHandle,
            "_ZN9CPhysical13ApplyFrictionEfR9CColPoint");
    HOOKSYM(AutomobileProcessControl, gtasaHandle,
            "_ZN11CAutomobile14ProcessControlEv");
}
