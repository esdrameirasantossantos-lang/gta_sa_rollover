
#include <mod/amlmod.h>
#include <aml-psdk/game_sa/entity/Automobile.h>
#include <aml-psdk/game_sa/base/ColModel.h>
#include <cmath>

MYMOD(net.esdra.realisticrollover, Realistic Rollover, 1.3, Esdra)

BEGIN_DEPLIST()
    ADD_DEPENDENCY_VER(net.rusjj.aml, 1.3.0)
END_DEPLIST()

static bool g_groundContact = false;
static int g_cooldown = 0;

static constexpr float BODY_FRICTION = 1.12f;
static constexpr float MIN_FORWARD = 0.45f;
static constexpr float MIN_LATERAL = 0.12f;
static constexpr float SPEED_RAMP = 1.10f;
static constexpr float SLIP_RAMP = 0.65f;
static constexpr float MAX_FORCE = 12.0f;
static constexpr float SIDE_OFFSET = 0.95f;
static constexpr float REAR_OFFSET = 0.35f;

static float Clamp01(float x)
{
    return x < 0.0f ? 0.0f : (x > 1.0f ? 1.0f : x);
}

static float Abs(float x)
{
    return std::fabs(x);
}

DECL_HOOK(bool, PhysicalApplyFriction,
    void* self, float friction, CColPoint& cp)
{
    CAutomobile* car =
        (CAutomobile*)FindPlayerVehicle(0, false);

    if (car && self == car)
    {
        if (cp.m_vecNormal.z > 0.55f)
            g_groundContact = true;

        CMatrix* matrix = car->m_matrix;

        if (matrix)
        {
            CVector rel(
                cp.m_vecPoint.x - matrix->pos.x,
                cp.m_vecPoint.y - matrix->pos.y,
                cp.m_vecPoint.z - matrix->pos.z
            );

            float localX =
                rel.x * matrix->right.x +
                rel.y * matrix->right.y +
                rel.z * matrix->right.z;

            float localZ =
                rel.x * matrix->up.x +
                rel.y * matrix->up.y +
                rel.z * matrix->up.z;

            float normalUp = Abs(
                cp.m_vecNormal.x * matrix->up.x +
                cp.m_vecNormal.y * matrix->up.y +
                cp.m_vecNormal.z * matrix->up.z
            );

            float normalSide = Abs(
                cp.m_vecNormal.x * matrix->right.x +
                cp.m_vecNormal.y * matrix->right.y +
                cp.m_vecNormal.z * matrix->right.z
            );

            bool bodyContact =
                (normalUp > 0.55f &&
                 (localZ > 0.25f ||
                  (Abs(localX) > 0.70f && localZ > -0.12f)))
                ||
                (normalSide > 0.65f && Abs(localX) > 0.55f);

            if (bodyContact)
                friction *= BODY_FRICTION;
        }
    }

    return PhysicalApplyFriction(self, friction, cp);
}

DECL_HOOK(void, AutomobileProcessControl, CAutomobile* car)
{
    AutomobileProcessControl(car);

    if (!car)
        return;

    if (FindPlayerVehicle(0, false) != car)
        return;

    bool grounded = g_groundContact;
    g_groundContact = false;

    if (g_cooldown > 0)
        --g_cooldown;

    if (!grounded || g_cooldown > 0)
        return;

    CMatrix* matrix = car->m_matrix;
    if (!matrix)
        return;

    // Evita aplicar o efeito quando o carro já está muito tombado.
    if (matrix->up.z < 0.20f || matrix->up.z > 0.94f)
        return;

    CVector velocity = car->m_vecMoveSpeed;

    float forwardSpeed = Abs(
        velocity.x * matrix->at.x +
        velocity.y * matrix->at.y +
        velocity.z * matrix->at.z
    );

    float lateralVelocity =
        velocity.x * matrix->right.x +
        velocity.y * matrix->right.y +
        velocity.z * matrix->right.z;

    float lateralSpeed = Abs(lateralVelocity);

    if (forwardSpeed < MIN_FORWARD ||
        lateralSpeed < MIN_LATERAL)
        return;

    float speedFactor =
        Clamp01((forwardSpeed - MIN_FORWARD) / SPEED_RAMP);

    float slipFactor =
        Clamp01((lateralSpeed - MIN_LATERAL) / SLIP_RAMP);

    float forceAmount =
        MAX_FORCE * speedFactor * slipFactor;

    if (forceAmount < 0.5f)
        return;

    float side = lateralVelocity >= 0.0f ? -1.0f : 1.0f;

    // Deslocamento relativo ao carro, não posição absoluta no mundo.
    CVector offset(
        matrix->right.x * side * SIDE_OFFSET -
            matrix->at.x * REAR_OFFSET,
        matrix->right.y * side * SIDE_OFFSET -
            matrix->at.y * REAR_OFFSET,
        matrix->right.z * side * SIDE_OFFSET -
            matrix->at.z * REAR_OFFSET
    );

    CVector force(0.0f, 0.0f, forceAmount);

    car->ApplyForce(force, offset, true);

    g_cooldown = 8;
}

ON_MOD_LOAD()
{
    void* handle = aml->GetLibHandle("libGTASA.so");
    if (!handle)
        return;

    if (!aml->GetSym(handle, "_Z17FindPlayerVehicleib"))
        return;

    HOOKSYM(
        PhysicalApplyFriction,
        handle,
        "_ZN9CPhysical13ApplyFrictionEfR9CColPoint"
    );

    HOOKSYM(
        AutomobileProcessControl,
        handle,
        "_ZN11CAutomobile14ProcessControlEv"
    );
}
