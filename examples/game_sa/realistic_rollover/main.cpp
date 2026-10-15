#include <mod/amlmod.h>
#include <aml-psdk/game_sa/Physical.h>
#include <aml-psdk/game_sa/Automobile.h>
#include <aml-psdk/game_sa/ColModel.h>
#include <cmath>
#include <cstdint>

MYMOD(net.esdra.realisticrollover, Realistic Rollover, 1.3, Esdra)

BEGIN_DEPLIST()
    ADD_DEPENDENCY_VER(net.rusjj.aml, 1.3.0)
END_DEPLIST()

static inline float Dot3(
    float ax, float ay, float az,
    float bx, float by, float bz)
{
    return ax * bx + ay * by + az * bz;
}

static inline float Clamp01(float x)
{
    return x < 0.0f ? 0.0f : (x > 1.0f ? 1.0f : x);
}

static inline float AbsF(float x)
{
    return std::fabs(x);
}

// Configuração do capotamento.
static constexpr float MIN_FORWARD_SPEED = 0.30f;
static constexpr float MIN_LATERAL_SPEED = 0.10f;

static constexpr float SPEED_RAMP = 1.00f;
static constexpr float SLIP_RAMP = 0.55f;

// Aumente para um capotamento mais forte.
static constexpr float MAX_BLOWOVER_FORCE = 45.0f;

static constexpr float SIDE_OFFSET = 1.00f;
static constexpr float REAR_OFFSET = 0.25f;

static constexpr float MIN_UP_AXIS_Z = 0.20f;
static constexpr float MAX_UPRIGHT_AXIS_Z = 0.96f;

static constexpr float MIN_FORCE = 1.0f;
static constexpr int FORCE_COOLDOWN = 5;

using FindPlayerVehicleFn = void* (*)(int, bool);

static FindPlayerVehicleFn FindPlayerVehicleNative = nullptr;

static bool sawGroundContact = false;
static int forceCooldown = 0;

DECL_HOOK(bool, PhysicalApplyFriction,
          void* self, float adhesiveLimit, CColPoint* cp)
{
    if (self && cp && FindPlayerVehicleNative &&
        FindPlayerVehicleNative(0, false) == self)
    {
        // A normal real está em CColPoint::m_vecNormal.
        // O valor absoluto também permite detectar contato
        // com superfícies que tenham normal apontando para baixo.
        if (AbsF(cp->m_vecNormal.z) > 0.55f)
            sawGroundContact = true;
    }

    // Não aumenta a aderência: mantém o comportamento original.
    return PhysicalApplyFriction(self, adhesiveLimit, cp);
}

DECL_HOOK(void, AutomobileProcessControl, void* self)
{
    AutomobileProcessControl(self);

    if (!self || !FindPlayerVehicleNative ||
        FindPlayerVehicleNative(0, false) != self)
    {
        sawGroundContact = false;
        return;
    }

    const bool grounded = sawGroundContact;
    sawGroundContact = false;

    if (forceCooldown > 0)
        --forceCooldown;

    if (!grounded || forceCooldown > 0)
        return;

    auto* car = reinterpret_cast<CAutomobile*>(self);
    auto* physical = reinterpret_cast<CPhysical*>(self);

    CMatrix* matrix = car->GetMatrix();

    if (!matrix)
        return;

    const CVector& right = matrix->GetRight();
    const CVector& forward = matrix->GetForward();
    const CVector& up = matrix->GetUp();

    // Só inicia o efeito enquanto o carro ainda está
    // relativamente próximo da posição normal.
    if (up.z < MIN_UP_AXIS_Z ||
        up.z > MAX_UPRIGHT_AXIS_Z)
        return;

    const CVector& velocity = physical->m_vecMoveSpeed;

    const float forwardSpeed = AbsF(Dot3(
        velocity.x, velocity.y, velocity.z,
        forward.x, forward.y, forward.z));

    const float lateralVelocity = Dot3(
        velocity.x, velocity.y, velocity.z,
        right.x, right.y, right.z);

    const float lateralSpeed = AbsF(lateralVelocity);

    if (forwardSpeed < MIN_FORWARD_SPEED ||
        lateralSpeed < MIN_LATERAL_SPEED)
        return;

    const float speedFactor = Clamp01(
        (forwardSpeed - MIN_FORWARD_SPEED) / SPEED_RAMP);

    const float slipFactor = Clamp01(
        (lateralSpeed - MIN_LATERAL_SPEED) / SLIP_RAMP);

    const float forceAmount =
        MAX_BLOWOVER_FORCE * speedFactor * slipFactor;

    if (forceAmount < MIN_FORCE)
        return;

    // Escolhe o lado de aplicação conforme a derrapagem.
    const float side =
        lateralVelocity >= 0.0f ? -1.0f : 1.0f;

    // O offset é um vetor relativo ao centro de massa,
    // convertido para coordenadas do mundo usando a matriz.
    const CVector offset(
        right.x * side * SIDE_OFFSET -
            forward.x * REAR_OFFSET,

        right.y * side * SIDE_OFFSET -
            forward.y * REAR_OFFSET,

        right.z * side * SIDE_OFFSET -
            forward.z * REAR_OFFSET
    );

    const CVector force(0.0f, 0.0f, forceAmount);

    // Usa o método declarado no PSDK, evitando uma assinatura
    // manual de função nativa para argumentos CVector.
    physical->ApplyForce(force, offset, true);

    forceCooldown = FORCE_COOLDOWN;
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

    if (!FindPlayerVehicleNative)
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
