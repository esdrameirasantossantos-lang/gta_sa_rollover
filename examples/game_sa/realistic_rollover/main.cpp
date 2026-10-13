#include <mod/amlmod.h>
#include <mod/logger.h>

#include <cmath>
#include <cstdint>

MYMOD(net.esdra.realisticrollover, Realistic Rollover, 0.9, Esdra)

BEGIN_DEPLIST()
    ADD_DEPENDENCY_VER(net.rusjj.aml, 1.3.0)
END_DEPLIST()

// GTA SA Android v2.00 / ARMv7.
// Experimental source: native body-contact friction plus a bounded turn force.
// Compile and test separately; this is not a tested .so.

struct Vec3 {
    float x, y, z;
};

static inline Vec3 Add(Vec3 a, Vec3 b) {
    return {a.x + b.x, a.y + b.y, a.z + b.z};
}
static inline Vec3 Sub(Vec3 a, Vec3 b) {
    return {a.x - b.x, a.y - b.y, a.z - b.z};
}
static inline Vec3 Mul(Vec3 a, float s) {
    return {a.x * s, a.y * s, a.z * s};
}
static inline float Dot(Vec3 a, Vec3 b) {
    return a.x * b.x + a.y * b.y + a.z * b.z;
}
static inline float Len2(Vec3 a) {
    return Dot(a, a);
}
static inline float Clamp(float v, float lo, float hi) {
    return v < lo ? lo : (v > hi ? hi : v);
}

// CMatrix / RwMatrix basis order: right, up, at, position.
struct MatrixSA {
    Vec3 right; uint32_t flagsRight;
    Vec3 up;    uint32_t flagsUp;
    Vec3 at;    uint32_t flagsAt;
    Vec3 pos;   uint32_t flagsPos;
};

// CColPoint offsets confirmed from the game's ApplyFriction instructions.
struct ColPointSA {
    Vec3 point;       // +0x00, world-space contact point
    float depth;      // +0x0C
    Vec3 normal;      // +0x10, world-space contact normal
};

using FindPlayerVehicleFn = void* (*)(int playerId, bool includeRemote);
// Binary disassembly confirms argument order: world contact point, then force.
using ApplyTurnForceFn = void (*)(void* physical, Vec3 worldPoint, Vec3 force);

static FindPlayerVehicleFn FindPlayerVehicleNative = nullptr;
static ApplyTurnForceFn ApplyTurnForceNative = nullptr;

static constexpr uintptr_t MATRIX_PTR_OFFSET = 0x14;
static constexpr uintptr_t MOVE_SPEED_OFFSET = 0x48;

// Start conservatively. This affects only classified roof/side body contacts.
static constexpr float BODY_FRICTION_MULTIPLIER = 1.30f;
static constexpr float ROLL_GAIN = 1.50f;
static constexpr float MAX_ROLL_FORCE = 0.45f;
static constexpr float MIN_FORCE_FOR_ROLL = 0.055f;
static constexpr float GROUND_NORMAL_Z_MIN = 0.65f;
static constexpr float ROOF_LOCAL_Z_MIN = 0.25f;
static constexpr float SIDE_LOCAL_X_MIN = 0.70f;
static constexpr float SIDE_LOCAL_Z_MIN = -0.12f;

static bool IsBodyGroundContact(void* self, const ColPointSA* cp,
                                MatrixSA*& matrixOut, Vec3*& moveSpeedOut,
                                float& localXOut, float& localZOut,
                                bool& roofOut) {
    if (!self || !cp)
        return false;

    // Ignore walls/near-vertical surfaces: this effect is for body-to-ground contact.
    if (std::fabs(cp->normal.z) < GROUND_NORMAL_Z_MIN)
        return false;

    auto* bytes = reinterpret_cast<uint8_t*>(self);
    matrixOut = *reinterpret_cast<MatrixSA**>(bytes + MATRIX_PTR_OFFSET);
    moveSpeedOut = reinterpret_cast<Vec3*>(bytes + MOVE_SPEED_OFFSET);
    if (!matrixOut || !moveSpeedOut)
        return false;

    const Vec3 relative = Sub(cp->point, matrixOut->pos);
    localXOut = Dot(relative, matrixOut->right);
    localZOut = Dot(relative, matrixOut->up);

    roofOut = localZOut > ROOF_LOCAL_Z_MIN;
    const bool sideBodyContact =
        std::fabs(localXOut) > SIDE_LOCAL_X_MIN &&
        localZOut > SIDE_LOCAL_Z_MIN;

    return roofOut || sideBodyContact;
}

// This overload returns bool in the supplied game binary (it ends with r0=0/1).
DECL_HOOK(bool, PhysicalApplyFriction, void* self, float friction, ColPointSA* cp) {
    MatrixSA* matrix = nullptr;
    Vec3* moveSpeed = nullptr;
    float localX = 0.0f;
    float localZ = 0.0f;
    bool roofContact = false;
    bool bodyContact = false;

    if (self && cp && FindPlayerVehicleNative &&
        FindPlayerVehicleNative(0, false) == self) {
        bodyContact = IsBodyGroundContact(self, cp, matrix, moveSpeed,
                                           localX, localZ, roofContact);
        if (bodyContact)
            friction *= BODY_FRICTION_MULTIPLIER;
    }

    // Run the original solver with adjusted friction and preserve its bool result.
    const bool result = PhysicalApplyFriction(self, friction, cp);

    // Add a small, bounded roll contribution only at roof contacts.
    // Side contacts get the native friction increase but no speculative extra torque.
    if (bodyContact && roofContact && result && matrix && moveSpeed &&
        ApplyTurnForceNative) {
        const float lateralSpeed = Dot(*moveSpeed, matrix->right);
        Vec3 force = Mul(matrix->right, -lateralSpeed * ROLL_GAIN);

        // Keep the added force tangent to the contact plane.
        const float normal2 = Len2(cp->normal);
        if (normal2 > 0.0001f)
            force = Sub(force, Mul(cp->normal,
                                  Dot(force, cp->normal) / normal2));

        const float force2 = Len2(force);
        if (force2 > MIN_FORCE_FOR_ROLL * MIN_FORCE_FOR_ROLL) {
            const float forceLength = std::sqrt(force2);
            if (forceLength > MAX_ROLL_FORCE)
                force = Mul(force, MAX_ROLL_FORCE / forceLength);
            ApplyTurnForceNative(self, cp->point, force);
        }
    }

    return result;
}

ON_MOD_LOAD() {
    logger->SetTag("Realistic Rollover");

    void* gtasaHandle = aml->GetLibHandle("libGTASA.so");
    if (!gtasaHandle) {
        logger->Error("libGTASA.so not found");
        return;
    }

    FindPlayerVehicleNative = reinterpret_cast<FindPlayerVehicleFn>(
        aml->GetSym(gtasaHandle, "_Z17FindPlayerVehicleib"));
    ApplyTurnForceNative = reinterpret_cast<ApplyTurnForceFn>(
        aml->GetSym(gtasaHandle, "_ZN9CPhysical14ApplyTurnForceE7CVectorS0_"));

    if (!FindPlayerVehicleNative || !ApplyTurnForceNative) {
        logger->Error("Required GTA SA v2.00 symbols not found");
        return;
    }

    HOOKSYM(PhysicalApplyFriction, gtasaHandle,
            "_ZN9CPhysical13ApplyFrictionEfR9CColPoint");
    logger->Info("Hook installed: roof/side contact friction prototype");
}
