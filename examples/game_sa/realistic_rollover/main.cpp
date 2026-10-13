#include <mod/amlmod.h>

#include <cmath>
#include <cstdint>
#include <cstring>

MYMOD(net.esdra.realisticrollover, Realistic Rollover, 0.3, Esdra)

BEGIN_DEPLIST()
    ADD_DEPENDENCY_VER(net.rusjj.aml, 1.3.0)
END_DEPLIST()

// Reverse-engineered from the user's GTA SA Android v2.00 armeabi-v7a libGTASA.so.
// Experimental first pass: native friction + angular force at actual body-ground contacts.
// This source has NOT been compiled or tested inside the game yet.
// It is a reverse-engineering prototype, not a verified release build.

struct Vec3 {
    float x, y, z;
};

static inline Vec3 Add(Vec3 a, Vec3 b) { return {a.x+b.x, a.y+b.y, a.z+b.z}; }
static inline Vec3 Sub(Vec3 a, Vec3 b) { return {a.x-b.x, a.y-b.y, a.z-b.z}; }
static inline Vec3 Mul(Vec3 a, float s) { return {a.x*s, a.y*s, a.z*s}; }
static inline float Dot(Vec3 a, Vec3 b) { return a.x*b.x + a.y*b.y + a.z*b.z; }
static inline float Len2(Vec3 a) { return Dot(a, a); }

struct MatrixSA {
    Vec3 right;   uint32_t flagsRight;
    Vec3 forward; uint32_t flagsForward; // RenderWare matrix's second axis
    Vec3 up;      uint32_t flagsUp;      // RenderWare matrix's third axis
    Vec3 pos;     uint32_t flagsPos;
};

struct ColPointSA {
    Vec3 point;       // offset 0x00
    float depth;      // offset 0x0C
    Vec3 normal;      // offset 0x10
    // Remaining material/piece fields are not needed here.
};

using FindPlayerVehicleFn = void* (*)(int playerId, bool includeRemote);
using ApplyTurnForceFn = void (*)(void* physical, Vec3 worldPoint, Vec3 force);

static FindPlayerVehicleFn FindPlayerVehicleNative = nullptr;
static ApplyTurnForceFn ApplyTurnForceNative = nullptr;
static void* gtasaHandle = nullptr;

// Confirmed against ARMv7 instructions in the supplied libGTASA.so:
// ApplyTurnForce reads the CPlaceable matrix pointer at self+0x14.
// ApplyForce adds linear velocity at self+0x48; ApplyTurnForce adds angular
// velocity at self+0x54. These are build-specific and must not be reused on
// a different libGTASA.so version.
static constexpr uintptr_t MATRIX_PTR_OFFSET = 0x14;
static constexpr uintptr_t MOVE_SPEED_OFFSET = 0x48;

// Tuning: this multiplier affects native friction at roof/side body contacts.
// The extra turn force changes angular speed only; it does not directly cancel
// the vehicle's linear sliding speed.
static constexpr float BODY_FRICTION_MULTIPLIER = 1.30f;
static constexpr float ROLL_GAIN = 2.50f;
static constexpr float MAX_ROLL_FORCE = 0.80f;
static constexpr float MIN_FORCE_FOR_ROLL = 0.055f;

static bool IsBodyGroundContact(void* self, const ColPointSA* cp,
                                MatrixSA*& matrixOut, Vec3*& moveSpeedOut,
                                float& localXOut, float& localZOut)
{
    if (!self || !cp)
        return false;

    // Ground-like contact only. abs() allows either collision-normal orientation.
    if (std::fabs(cp->normal.z) < 0.65f)
        return false;

    auto* bytes = reinterpret_cast<uint8_t*>(self);
    matrixOut = *reinterpret_cast<MatrixSA**>(bytes + MATRIX_PTR_OFFSET);
    moveSpeedOut = reinterpret_cast<Vec3*>(bytes + MOVE_SPEED_OFFSET);

    if (!matrixOut)
        return false;

    const Vec3 relative = Sub(cp->point, matrixOut->pos);
    localXOut = Dot(relative, matrixOut->right);
    localZOut = Dot(relative, matrixOut->up);

    // Roof contact: contact point is on the vehicle's local upper side.
    const bool roofContact = localZOut > 0.25f;

    // Side/body contact while tipped over. The lower wheel area is excluded.
    const bool sideBodyContact =
        std::fabs(localXOut) > 0.70f && localZOut > -0.12f;

    return roofContact || sideBodyContact;
}

DECL_HOOK(bool, ApplyFriction, void* self, float friction, ColPointSA* cp)
{
    MatrixSA* matrix = nullptr;
    Vec3* moveSpeed = nullptr;
    float localX = 0.0f;
    float localZ = 0.0f;

    bool bodyContact = false;
    void* playerVehicle = nullptr;

    if (FindPlayerVehicleNative && self && cp)
    {
        playerVehicle = FindPlayerVehicleNative(0, false);

        // Only affect the player's current vehicle; don't change pedestrians,
        // traffic vehicles, props, or ordinary wheel contacts.
        if (playerVehicle == self)
        {
            bodyContact = IsBodyGroundContact(
                self, cp, matrix, moveSpeed, localX, localZ);

            if (bodyContact)
                friction *= BODY_FRICTION_MULTIPLIER;
        }
    }

    // Let the game's own collision/friction solver run with the adjusted
    // coefficient first. Its return value is preserved.
    const bool result = ApplyFriction(self, friction, cp);

    if (bodyContact && result && matrix && moveSpeed && ApplyTurnForceNative)
    {
        const float lateralSpeed = Dot(*moveSpeed, matrix->right);
        const float forwardSpeed = Dot(*moveSpeed, matrix->forward);

        // A roof contact uses lateral motion to create roll torque.
        // A side contact uses forward motion to create roll torque.
        Vec3 force;
        if (localZ > 0.25f)
            force = Mul(matrix->right, -lateralSpeed * ROLL_GAIN);
        else
            force = Mul(matrix->forward, -forwardSpeed * ROLL_GAIN);

        // Project the added force onto the contact plane. This is more robust
        // than forcing world Z to zero on a sloped surface.
        const float normal2 = Len2(cp->normal);
        if (normal2 > 0.0001f)
            force = Sub(force, Mul(cp->normal, Dot(force, cp->normal) / normal2));

        const float force2 = Len2(force);
        if (force2 > MIN_FORCE_FOR_ROLL * MIN_FORCE_FOR_ROLL)
        {
            if (force2 > MAX_ROLL_FORCE * MAX_ROLL_FORCE)
                force = Mul(force, MAX_ROLL_FORCE / std::sqrt(force2));

            // The native function expects the WORLD contact point; it computes
            // the lever arm from the vehicle's center of mass internally.
            ApplyTurnForceNative(self, cp->point, force);
        }
    }

    return result;
}

ON_MOD_LOAD()
{
    const uintptr_t pGTASA = aml->GetLib("libGTASA.so");
    gtasaHandle = aml->GetLibHandle("libGTASA.so");

    if (!pGTASA || !gtasaHandle)
    {
        return;
    }

    FindPlayerVehicleNative = reinterpret_cast<FindPlayerVehicleFn>(
        aml->GetSym(gtasaHandle, "_Z17FindPlayerVehicleib"));

    ApplyTurnForceNative = reinterpret_cast<ApplyTurnForceFn>(
        aml->GetSym(gtasaHandle, "_ZN9CPhysical14ApplyTurnForceE7CVectorS0_"));

    if (!FindPlayerVehicleNative || !ApplyTurnForceNative)
    {
        return;
    }

    // ApplyFriction is exported by the supplied v2.00 ARMv7 libGTASA.so.
    HOOKSYM(ApplyFriction, gtasaHandle,
            "_ZN9CPhysical13ApplyFrictionEfR9CColPoint");

}
