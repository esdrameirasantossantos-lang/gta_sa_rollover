#include <mod/amlmod.h>
#include <cmath>
#include <cstdint>

MYMOD(net.esdra.realisticrollover, Realistic Rollover, 0.7, Esdra)

BEGIN_DEPLIST()
    ADD_DEPENDENCY_VER(net.rusjj.aml, 1.3.0)
END_DEPLIST()

struct Vec3 {
    float x, y, z;
};

static inline float Dot(Vec3 a, Vec3 b) {
    return a.x*b.x + a.y*b.y + a.z*b.z;
}

static inline float Clamp01(float x) {
    if (x < 0.0f) return 0.0f;
    if (x > 1.0f) return 1.0f;
    return x;
}

struct MatrixSA {
    Vec3 right;   uint32_t flagsRight;
    Vec3 forward; uint32_t flagsForward;
    Vec3 up;      uint32_t flagsUp;
    Vec3 pos;     uint32_t flagsPos;
};

using FindPlayerVehicleFn = void* (*)(int, bool);
using ApplyTurnForceFn = void (*)(void*, Vec3, Vec3);

static FindPlayerVehicleFn FindPlayerVehicleNative = nullptr;
static ApplyTurnForceFn ApplyTurnForceNative = nullptr;

static constexpr uintptr_t MATRIX_PTR_OFFSET = 0x14;
static constexpr uintptr_t MOVE_SPEED_OFFSET = 0x48;

// Blowover
static constexpr float MIN_FORWARD_SPEED = 0.78f;
static constexpr float MIN_LATERAL_SPEED = 0.24f;
static constexpr float SPEED_RAMP = 0.95f;
static constexpr float SLIP_RAMP = 0.55f;
static constexpr float MAX_IMPULSE = 150.0f;
static constexpr float REAR_OFFSET = 1.45f;
static constexpr float REAR_UP_OFFSET = 0.10f;
static constexpr float MIN_UP_AXIS_Z = 0.70f;

// Atrito artificial da carroceria
static constexpr float BODY_FRICTION_MULTIPLIER = 2.2f;
static constexpr float BODY_CONTACT_MIN_HEIGHT = 0.20f;
static constexpr float BODY_TIPPED_LIMIT = 0.65f;

DECL_HOOK(void, PhysicalApplyFriction,
          void* self, float friction, void* colPoint)
{
    float adjustedFriction = friction;

    if (self && colPoint && FindPlayerVehicleNative) {
        void* vehicle = FindPlayerVehicleNative(0, false);

        if (vehicle == self) {
            auto* bytes = reinterpret_cast<uint8_t*>(self);
            MatrixSA* matrix =
                *reinterpret_cast<MatrixSA**>(
                    bytes + MATRIX_PTR_OFFSET
                );

            if (matrix) {
                // Supõe que os três primeiros floats de CColPoint
                // representam a posição mundial do contato.
                const Vec3 contact =
                    *reinterpret_cast<Vec3*>(colPoint);

                const Vec3 relative = {
                    contact.x - matrix->pos.x,
                    contact.y - matrix->pos.y,
                    contact.z - matrix->pos.z
                };

                const float localHeight =
                    Dot(relative, matrix->up);

                // Só aumenta o atrito quando o carro está inclinado
                // e o contato parece estar na região da carroceria.
                if (matrix->up.z < BODY_TIPPED_LIMIT &&
                    localHeight > BODY_CONTACT_MIN_HEIGHT) {
                    adjustedFriction *= BODY_FRICTION_MULTIPLIER;
                }
            }
        }
    }

    // Mantém a rotina original do jogo.
    PhysicalApplyFriction(self, adjustedFriction, colPoint);
}

DECL_HOOK(void, AutomobileProcessControl, void* self)
{
    AutomobileProcessControl(self);

    if (!self || !FindPlayerVehicleNative ||
        !ApplyTurnForceNative)
        return;

    void* vehicle = FindPlayerVehicleNative(0, false);

    if (!vehicle || vehicle != self)
        return;

    auto* bytes = reinterpret_cast<uint8_t*>(self);

    MatrixSA* matrix =
        *reinterpret_cast<MatrixSA**>(
            bytes + MATRIX_PTR_OFFSET
        );

    Vec3* velocity =
        reinterpret_cast<Vec3*>(
            bytes + MOVE_SPEED_OFFSET
        );

    if (!matrix || !velocity)
        return;

    if (matrix->up.z < MIN_UP_AXIS_Z)
        return;

    const float forwardSpeed =
        std::fabs(Dot(*velocity, matrix->forward));

    const float lateralSpeed =
        std::fabs(Dot(*velocity, matrix->right));

    if (forwardSpeed < MIN_FORWARD_SPEED ||
        lateralSpeed < MIN_LATERAL_SPEED)
        return;

    const float speedFactor = Clamp01(
        (forwardSpeed - MIN_FORWARD_SPEED) / SPEED_RAMP
    );

    const float slipFactor = Clamp01(
        (lateralSpeed - MIN_LATERAL_SPEED) / SLIP_RAMP
    );

    const float impulse =
        MAX_IMPULSE * speedFactor * slipFactor;

    if (impulse < 1.0f)
        return;

    const Vec3 upwardForce = {
        0.0f, 0.0f, impulse
    };

    const Vec3 rearOffset = {
        0.0f, -REAR_OFFSET, REAR_UP_OFFSET
    };

    ApplyTurnForceNative(self, upwardForce, rearOffset);
}

ON_MOD_LOAD()
{
    void* gtasaHandle = aml->GetLibHandle("libGTASA.so");

    if (!gtasaHandle)
        return;

    FindPlayerVehicleNative =
        reinterpret_cast<FindPlayerVehicleFn>(
            aml->GetSym(
                gtasaHandle,
                "_Z17FindPlayerVehicleib"
            )
        );

    ApplyTurnForceNative =
        reinterpret_cast<ApplyTurnForceFn>(
            aml->GetSym(
                gtasaHandle,
                "_ZN9CPhysical14ApplyTurnForceE7CVectorS0_"
            )
        );

    if (!FindPlayerVehicleNative ||
        !ApplyTurnForceNative)
        return;

    HOOKSYM(
        PhysicalApplyFriction,
        gtasaHandle,
        "_ZN9CPhysical13ApplyFrictionEfR9CColPoint"
    );

    HOOKSYM(
        AutomobileProcessControl,
        gtasaHandle,
        "_ZN11CAutomobile14ProcessControlEv"
    );
}
