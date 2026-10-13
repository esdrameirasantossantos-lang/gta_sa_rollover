
#include <mod/amlmod.h>
#include <cmath>
#include <cstdint>

MYMOD(net.esdra.realisticrollover, Realistic Rollover, 1.2, Esdra)

BEGIN_DEPLIST()
    ADD_DEPENDENCY_VER(net.rusjj.aml, 1.3.0)
END_DEPLIST()

struct Vec3 {
    float x, y, z;
};

static inline Vec3 Add(Vec3 a, Vec3 b) {
    return {a.x + b.x, a.y + b.y, a.z + b.z};
}

static inline Vec3 Mul(Vec3 a, float s) {
    return {a.x * s, a.y * s, a.z * s};
}

static inline float Dot(Vec3 a, Vec3 b) {
    return a.x*b.x + a.y*b.y + a.z*b.z;
}

static inline float Abs(float x) {
    return std::fabs(x);
}

static inline float Clamp01(float x) {
    return x < 0.0f ? 0.0f : (x > 1.0f ? 1.0f : x);
}

// CMatrix: right, up, at, position.
struct MatrixSA {
    Vec3 right; uint32_t flagsRight;
    Vec3 up;    uint32_t flagsUp;
    Vec3 at;    uint32_t flagsAt;
    Vec3 pos;   uint32_t flagsPos;
};

// CColPoint: point, normal, depth, surface/piece/light bytes.
struct ColPointSA {
    Vec3 point;
    Vec3 normal;
    float depth;
    uint8_t surfaceA, pieceA, lightA, camA;
    uint8_t surfaceB, pieceB, lightB, camB;
};

using FindPlayerVehicleFn = void* (*)(int, bool);
using ApplyForceFn = void (*)(void*, Vec3, Vec3, bool);

static FindPlayerVehicleFn FindPlayerVehicleNative = nullptr;
static ApplyForceFn ApplyForceNative = nullptr;

static bool sawGroundContact = false;
static int forceCooldown = 0;

// These offsets MUST match the GTA SA Android 2.00 build.
static constexpr uintptr_t MATRIX_PTR_OFFSET = 0x14;
static constexpr uintptr_t MOVE_SPEED_OFFSET = 0x48;

// Body friction: a small increase, not global tire grip.
static constexpr float BODY_FRICTION_MULTIPLIER = 1.12f;
static constexpr float GROUND_NORMAL_Z_MIN = 0.55f;

// Rollover tuning.
static constexpr float MIN_FORWARD_SPEED = 0.45f;
static constexpr float MIN_LATERAL_SPEED = 0.12f;
static constexpr float SPEED_RAMP = 1.10f;
static constexpr float SLIP_RAMP = 0.65f;
static constexpr float MAX_BLOWOVER_FORCE = 12.0f;

static constexpr float SIDE_OFFSET = 0.95f;
static constexpr float REAR_OFFSET = 0.35f;
static constexpr float MIN_UP_AXIS_Z = 0.20f;
static constexpr float MAX_UPRIGHT_AXIS_Z = 0.94f;

DECL_HOOK(bool, PhysicalApplyFriction,
          void* self, float friction, ColPointSA* cp) {

    if (self && cp && FindPlayerVehicleNative &&
        FindPlayerVehicleNative(0, false) == self) {

        // Detect ground contact.
        if (cp->normal.z > GROUND_NORMAL_Z_MIN)
            sawGroundContact = true;

        auto* bytes = reinterpret_cast<uint8_t*>(self);
        MatrixSA* matrix =
            *reinterpret_cast<MatrixSA**>(bytes + MATRIX_PTR_OFFSET);

        if (matrix) {
            Vec3 rel = {
                cp->point.x - matrix->pos.x,
                cp->point.y - matrix->pos.y,
                cp->point.z - matrix->pos.z
            };

            const float localX = Dot(rel, matrix->right);
            const float localZ = Dot(rel, matrix->up);

            // Increase friction only at likely body/roof/side contacts.
            // Normal tire grip is not deliberately increased.
            const float normalUp = Abs(Dot(cp->normal, matrix->up));
            const float normalSide = Abs(Dot(cp->normal, matrix->right));

            const bool bodyContact =
                (normalUp > 0.55f &&
                 (localZ > 0.25f ||
                  (Abs(localX) > 0.70f && localZ > -0.12f))) ||
                (normalSide > 0.65f &&
                 Abs(localX) > 0.55f);

            if (bodyContact)
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

    const bool grounded = sawGroundContact;
    sawGroundContact = false;

    if (forceCooldown > 0)
        --forceCooldown;

    if (!grounded || forceCooldown > 0)
        return;

    auto* bytes = reinterpret_cast<uint8_t*>(self);

    MatrixSA* matrix =
        *reinterpret_cast<MatrixSA**>(bytes + MATRIX_PTR_OFFSET);

    Vec3* velocity =
        reinterpret_cast<Vec3*>(bytes + MOVE_SPEED_OFFSET);

    if (!matrix || !velocity)
        return;

    // Avoid triggering when the car is almost upright or upside down.
    if (matrix->up.z < MIN_UP_AXIS_Z ||
        matrix->up.z > MAX_UPRIGHT_AXIS_Z)
        return;

    const float forwardSpeed = Abs(Dot(*velocity, matrix->at));
    const float lateralVelocity = Dot(*velocity, matrix->right);
    const float lateralSpeed = Abs(lateralVelocity);

    if (forwardSpeed < MIN_FORWARD_SPEED ||
        lateralSpeed < MIN_LATERAL_SPEED)
        return;

    const float speedFactor = Clamp01(
        (forwardSpeed - MIN_FORWARD_SPEED) / SPEED_RAMP);

    const float slipFactor = Clamp01(
        (lateralSpeed - MIN_LATERAL_SPEED) / SLIP_RAMP);

    const float forceAmount =
        MAX_BLOWOVER_FORCE * speedFactor * slipFactor;

    if (forceAmount < 0.5f)
        return;

    const float side =
        (lateralVelocity >= 0.0f) ? -1.0f : 1.0f;

    // Convert the vehicle-local application point to WORLD coordinates.
    // ApplyForce expects the point of application, not a local offset.
    Vec3 worldPoint = Add(
        matrix->pos,
        Add(
            Mul(matrix->right, side * SIDE_OFFSET),
            Mul(matrix->at, -REAR_OFFSET)
        )
    );

    // Upward force plus off-centre application creates roll torque.
    Vec3 force = {0.0f, 0.0f, forceAmount};

    ApplyForceNative(self, force, worldPoint, true);

    // Prevent the force from being applied every single frame.
    forceCooldown = 8;
}

ON_MOD_LOAD() {
    void* gtasaHandle = aml->GetLibHandle("libGTASA.so");
    if (!gtasaHandle)
        return;

    FindPlayerVehicleNative =
        reinterpret_cast<FindPlayerVehicleFn>(
            aml->GetSym(gtasaHandle, "_Z17FindPlayerVehicleib"));

    ApplyForceNative =
        reinterpret_cast<ApplyForceFn>(
            aml->GetSym(
                gtasaHandle,
                "_ZN9CPhysical10ApplyForceE7CVectorS0_b"));

    if (!FindPlayerVehicleNative || !ApplyForceNative)
        return;

    HOOKSYM(
        PhysicalApplyFriction,
        gtasaHandle,
        "_ZN9CPhysical13ApplyFrictionEfR9CColPoint");

    HOOKSYM(
        AutomobileProcessControl,
        gtasaHandle,
        "_ZN11CAutomobile14ProcessControlEv");
}
