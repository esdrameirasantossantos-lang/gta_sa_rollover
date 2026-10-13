#include <mod/amlmod.h>
#include <cmath>
#include <cstdint>

MYMOD(net.esdra.realisticrollover, Realistic Rollover, 0.8, Esdra)

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
    Vec3 right;
    uint32_t flagsRight;

    Vec3 forward;
    uint32_t flagsForward;

    Vec3 up;
    uint32_t flagsUp;

    Vec3 pos;
    uint32_t flagsPos;
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

// Atrito da carroceria
static constexpr float BODY_FRICTION_MULTIPLIER = 2.2f;
static constexpr float BODY_CONTACT_MIN_HEIGHT = 0.20f;
static constexpr float BODY_TIPPED_LIMIT = 0.65f;

// Evita multiplicar o atrito duas vezes se os hooks
// forem chamados um dentro do outro.
static thread_local bool g_insideFrictionHook = false;

static MatrixSA* GetMatrix(void* entity) {
    if (!entity)
        return nullptr;

    auto* bytes = reinterpret_cast<uint8_t*>(entity);

    return *reinterpret_cast<MatrixSA**>(
        bytes + MATRIX_PTR_OFFSET
    );
}

// Esta verificação depende de CColPoint começar com
// a posição mundial do contato. Isso precisa ser
// confirmado para a versão exata do jogo.
static bool IsBodyContact(void* vehicle, void* colPoint) {
    if (!vehicle || !colPoint)
        return false;

    MatrixSA* matrix = GetMatrix(vehicle);

    if (!matrix)
        return false;

    // Não aplicar o efeito enquanto o veículo está
    // aproximadamente na posição normal.
    if (matrix->up.z >= BODY_TIPPED_LIMIT)
        return false;

    const Vec3 contact =
        *reinterpret_cast<Vec3*>(colPoint);

    const Vec3 relative = {
        contact.x - matrix->pos.x,
        contact.y - matrix->pos.y,
        contact.z - matrix->pos.z
    };

    // Distância do contato ao longo do eixo vertical
    // local do veículo.
    const float localHeight = Dot(relative, matrix->up);

    return localHeight > BODY_CONTACT_MIN_HEIGHT;
}

static float AdjustBodyFriction(
    void* vehicle,
    float friction,
    void* colPoint
) {
    if (g_insideFrictionHook)
        return friction;

    if (IsBodyContact(vehicle, colPoint))
        return friction * BODY_FRICTION_MULTIPLIER;

    return friction;
}

// Sobrecarga: ApplyFriction(float, CColPoint&)
DECL_HOOK(
    void,
    PhysicalApplyFrictionSingle,
    void* self,
    float friction,
    void* colPoint
) {
    float adjusted = friction;

    void* playerVehicle = nullptr;

    if (FindPlayerVehicleNative)
        playerVehicle = FindPlayerVehicleNative(0, false);

    if (self && self == playerVehicle) {
        adjusted = AdjustBodyFriction(
            self, friction, colPoint
        );
    }

    const bool previous = g_insideFrictionHook;
    g_insideFrictionHook = true;

    PhysicalApplyFrictionSingle(
        self, adjusted, colPoint
    );

    g_insideFrictionHook = previous;
}

// Sobrecarga: ApplyFriction(CPhysical*, float, CColPoint&)
DECL_HOOK(
    void,
    PhysicalApplyFrictionPair,
    void* self,
    void* other,
    float friction,
    void* colPoint
) {
    float adjusted = friction;

    void* playerVehicle = nullptr;

    if (FindPlayerVehicleNative)
        playerVehicle = FindPlayerVehicleNative(0, false);

    if (playerVehicle) {
        if (self == playerVehicle || other == playerVehicle) {
            adjusted = AdjustBodyFriction(
                playerVehicle, friction, colPoint
            );
        }
    }

    const bool previous = g_insideFrictionHook;
    g_insideFrictionHook = true;

    PhysicalApplyFrictionPair(
        self, other, adjusted, colPoint
    );

    g_insideFrictionHook = previous;
}

// Blowover
DECL_HOOK(void, AutomobileProcessControl, void* self)
{
    AutomobileProcessControl(self);

    if (!self || !FindPlayerVehicleNative ||
        !ApplyTurnForceNative)
        return;

    void* vehicle = FindPlayerVehicleNative(0, false);

    if (!vehicle || vehicle != self)
        return;

    MatrixSA* matrix = GetMatrix(self);

    auto* bytes = reinterpret_cast<uint8_t*>(self);

    Vec3* velocity = reinterpret_cast<Vec3*>(
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

    ApplyTurnForceNative(
        self, upwardForce, rearOffset
    );
}

ON_MOD_LOAD()
{
    void* gtasaHandle =
        aml->GetLibHandle("libGTASA.so");

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
        PhysicalApplyFrictionSingle,
        gtasaHandle,
        "_ZN9CPhysical13ApplyFrictionEfR9CColPoint"
    );

    HOOKSYM(
        PhysicalApplyFrictionPair,
        gtasaHandle,
        "_ZN9CPhysical13ApplyFrictionEPS_fR9CColPoint"
    );

    HOOKSYM(
        AutomobileProcessControl,
        gtasaHandle,
        "_ZN11CAutomobile14ProcessControlEv"
    );
}
