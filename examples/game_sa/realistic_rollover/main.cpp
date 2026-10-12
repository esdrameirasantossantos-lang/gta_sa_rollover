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
    constexpr float FULL_SPEED = 0.75f;
    constexpr float LATERAL_THRESHOLD = 0.08f;

    // Forças iniciais para calibração
    constexpr float LIFT_FACTOR = 0.25f;
    constexpr float ROLL_FACTOR = 0.35f;

    // Distâncias relativas ao centro do veículo
    constexpr float REAR_DISTANCE = 1.5f;
    constexpr float SIDE_DISTANCE = 0.9f;
    constexpr float HEIGHT_OFFSET = 0.4f;

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

        // Velocidade linear atual do veículo
        const CVector velocity = car->m_vecMoveSpeed;
        const float speed = velocity.Magnitude();

        if (speed < Blowover::MIN_SPEED)
            return;

        // Eixos da carroceria
        const CVector right = car->GetRightDirection();
        const CVector forward = car->GetTopDirection();
        const CVector up = car->GetAtDirection();

        // Velocidade lateral relativa ao carro
        const float lateralSpeed =
            velocity.x * right.x +
            velocity.y * right.y +
            velocity.z * right.z;

        const float lateral = std::fabs(lateralSpeed);

        if (lateral < Blowover::LATERAL_THRESHOLD)
            return;

        // Intensidade cresce com velocidade e deslizamento
        const float speedFactor = Blowover::Clamp(
            (speed - Blowover::MIN_SPEED) /
            (Blowover::FULL_SPEED - Blowover::MIN_SPEED),
            0.0f,
            1.0f
        );

        const float slipFactor = Blowover::Clamp(
            lateral * 3.0f,
            0.0f,
            1.0f
        );

        const float strength =
            Blowover::STRENGTH *
            speedFactor *
            slipFactor;

        // Ponto de aplicação na região traseira
        const CVector rearOffset(
            -forward.x * Blowover::REAR_DISTANCE
                + up.x * Blowover::HEIGHT_OFFSET,
            -forward.y * Blowover::REAR_DISTANCE
                + up.y * Blowover::HEIGHT_OFFSET,
            -forward.z * Blowover::REAR_DISTANCE
                + up.z * Blowover::HEIGHT_OFFSET
        );

        // Força vertical adicional na traseira
        const float liftMagnitude =
            speed * speed *
            Blowover::LIFT_FACTOR *
            strength;

        const CVector liftForce(
            up.x * liftMagnitude,
            up.y * liftMagnitude,
            up.z * liftMagnitude
        );

        car->ApplyForce(liftForce, rearOffset, true);

        // Direção da rolagem baseada no deslizamento lateral
        const float rollDirection =
            lateralSpeed >= 0.0f ? -1.0f : 1.0f;

        const float rollMagnitude =
            speed * lateral *
            Blowover::ROLL_FACTOR *
            strength;

        const CVector rollForce(
            up.x * rollMagnitude,
            up.y * rollMagnitude,
            up.z * rollMagnitude
        );

        // Ponto deslocado lateralmente para produzir rolagem
        const CVector sideOffset(
            right.x * Blowover::SIDE_DISTANCE * rollDirection,
            right.y * Blowover::SIDE_DISTANCE * rollDirection,
            right.z * Blowover::SIDE_DISTANCE * rollDirection
        );

        car->ApplyTurnForce(rollForce, sideOffset);
    };
}
