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
    // Intensidade fixa: 50%
    constexpr float STRENGTH = 0.50f;

    // Limites de ativação
    constexpr float MIN_SPEED = 0.25f;
    constexpr float LATERAL_THRESHOLD = 0.08f;

    // Forças adicionais (valores iniciais para calibração)
    constexpr float LIFT_FACTOR = 0.0008f;
    constexpr float ROLL_FACTOR = 0.0004f;

    inline float Clamp(float value, float minValue, float maxValue)
    {
        return value < minValue ? minValue :
               value > maxValue ? maxValue : value;
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

        // Velocidade linear atual
        const CVector velocity = car->m_vecMoveSpeed;

        // Eixo lateral da carroceria
        const CVector right = car->GetRightDirection();

        // Velocidade lateral relativa ao próprio veículo
        const float lateralSpeed =
            velocity.x * right.x +
            velocity.y * right.y +
            velocity.z * right.z;

        const float speed = velocity.Magnitude();

        if (speed < Blowover::MIN_SPEED)
            return;

        const float lateral = std::fabs(lateralSpeed);

        if (lateral < Blowover::LATERAL_THRESHOLD)
            return;

        // Intensidade baseada na velocidade e no deslizamento
        const float speedFactor = Blowover::Clamp(
            (speed - Blowover::MIN_SPEED) * 2.0f,
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

        // Elevação adicional na região traseira
        const CVector liftForce(
            0.0f,
            0.0f,
            speed * speed *
            Blowover::LIFT_FACTOR *
            strength
        );

        const CVector rearOffset(
            0.0f,
            -1.5f,
            0.4f
        );

        car->ApplyForce(liftForce, rearOffset, true);

        // Torque adicional de rolagem
        const float rollDirection =
            lateralSpeed >= 0.0f ? -1.0f : 1.0f;

        const CVector rollForce(
            0.0f,
            0.0f,
            rollDirection *
            speed *
            lateral *
            Blowover::ROLL_FACTOR *
            strength
        );

        car->ApplyTurnForce(rollForce, rearOffset);
    };
}
