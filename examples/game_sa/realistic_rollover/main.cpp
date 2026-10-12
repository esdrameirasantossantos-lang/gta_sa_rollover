#include <mod/amlmod.h>
#include <aml-psdk/game_sa/Events.h>
#include <aml-psdk/game_sa/entity/PlayerPed.h>
#include <aml-psdk/game_sa/entity/Automobile.h>
#include <aml-psdk/game_sa/entity/Physical.h>
#include <cmath>

MYMOD(
    net.psdk.realisticrollover,
    Realistic Rollover,
    1.0,
    Esdra
)

namespace Blowover
{
    constexpr float STRENGTH = 0.50f;

    constexpr float MIN_SPEED = 0.25f;
    constexpr float LATERAL_THRESHOLD = 0.08f;

    // Intensidade do atrito e da rolagem artificiais
    constexpr float FRICTION_FACTOR = 0.80f;
    constexpr float ROLL_FACTOR = 0.65f;
    constexpr float LIFT_FACTOR = 0.50f;

    constexpr float REAR_DISTANCE = 1.5f;
    constexpr float SIDE_DISTANCE = 0.9f;

    inline float Clamp(float value, float minimum, float maximum)
    {
        return value < minimum ? minimum :
               value > maximum ? maximum : value;
    }

    inline float Dot(const CVector& a, const CVector& b)
    {
        return a.x * b.x + a.y * b.y + a.z * b.z;
    }

    inline CVector Scale(const CVector& v, float s)
    {
        return CVector(v.x * s, v.y * s, v.z * s);
    }

    inline CVector Add(const CVector& a, const CVector& b)
    {
        return CVector(a.x + b.x, a.y + b.y, a.z + b.z);
    }
}

ON_MOD_LOAD()
{
    Events::gameProcessEvent += []()
    {
        CPlayerPed* player = FindPlayerPed(-1);
        if (!player)
            return;

        CVehicle* vehicle = FindPlayerVehicle(-1, false);
        if (!vehicle || !vehicle->IsAutomobile())
            return;

        CAutomobile* car = vehicle->AsAutomobile();

        const CVector velocity = car->m_vecMoveSpeed;
        const float speed = velocity.Magnitude();

        if (speed < Blowover::MIN_SPEED)
            return;

        const CVector right = car->GetRightDirection();
        const CVector forward = car->GetTopDirection();
        const CVector up = car->GetAtDirection();

        const float lateralSpeed = Blowover::Dot(velocity, right);
        const float lateral = std::fabs(lateralSpeed);

        const float speedFactor = Blowover::Clamp(
            (speed - Blowover::MIN_SPEED) / 0.50f,
            0.0f,
            1.0f
        );

        const float slipFactor = Blowover::Clamp(
            lateral * 3.0f,
            0.0f,
            1.0f
        );

        const float strength =
            Blowover::STRENGTH * speedFactor * slipFactor;

        if (strength <= 0.0f)
            return;

        // Ponto traseiro relativo à orientação do veículo.
        const CVector rearOffset = Blowover::Scale(
            forward, -Blowover::REAR_DISTANCE
        );

        // Elevação adicional na traseira.
        const float liftMagnitude =
            speed * speed * Blowover::LIFT_FACTOR * strength;

        const CVector liftForce = Blowover::Scale(up, liftMagnitude);

        car->ApplyForce(liftForce, rearOffset, true);

        // Torque de rolagem gerado por uma força vertical
        // aplicada fora do centro da carroceria.
        const float rollDirection =
            lateralSpeed >= 0.0f ? -1.0f : 1.0f;

        const CVector sideOffset = Blowover::Scale(
            right, Blowover::SIDE_DISTANCE * rollDirection
        );

        const float rollMagnitude =
            speed * lateral * Blowover::ROLL_FACTOR * strength;

        const CVector rollForce = Blowover::Scale(up, rollMagnitude);

        car->ApplyTurnForce(rollForce, sideOffset);

        // Atrito artificial estimado durante grande inclinação.
        // GetUp().z baixo indica que o carro está muito inclinado.
        const float tilt = Blowover::Clamp(
            1.0f - std::fabs(up.z),
            0.0f,
            1.0f
        );

        if (tilt > 0.35f)
        {
            // Componente lateral da velocidade.
            const CVector lateralVelocity =
                Blowover::Scale(right, lateralSpeed);

            // Força contrária ao deslizamento lateral.
            const CVector frictionForce = Blowover::Scale(
                lateralVelocity,
                -Blowover::FRICTION_FACTOR *
                strength * tilt
            );

            // Braço de alavanca lateral para adicionar rotação.
            const CVector frictionOffset = Blowover::Scale(
                right, rollDirection * Blowover::SIDE_DISTANCE
            );

            car->ApplyForce(frictionForce, frictionOffset, true);
        }
    };
}
