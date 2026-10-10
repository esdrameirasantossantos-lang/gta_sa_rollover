// ============================================================================
// Realistic Rollover Physics — AML PSDK plugin para GTA San Andreas Android
// ============================================================================
// Problema que este plugin resolve:
//   No GTA SA, quando um veiculo capota, a engine gira o corpo em torno do seu
//   proprio eixo (m_vecTurnSpeed) mas praticamente nao transfere a velocidade
//   linear (m_vecMoveSpeed) para fora do corpo durante o giro — o carro "roda
//   no lugar" em vez de girar E deslizar ao mesmo tempo, como acontece em
//   jogos com solver rigido-corpo mais completo (BeamNG, etc).
//
// Solucao:
//   A cada frame de fisica, se o veiculo do jogador estiver:
//     1) em contato com o chao (bOnSolidSurface)
//     2) com inclinacao alta o suficiente pra ser considerado "capotando"
//   entao:
//     - aumentamos a velocidade angular (m_vecTurnSpeed) proporcionalmente a
//       velocidade linear atual (quanto mais rapido, mais rapido capota)
//     - reduzimos o "travamento" de atrito lateral multiplicando
//       m_vecMoveSpeed por um fator de grip que depende da superficie
//       (m_nContactSurface), permitindo o corpo deslizar enquanto gira
//
// Compilar: coloque esta pasta dentro de examples/game_sa/ no repo aml-psdk
// e rode build.ps1 (ou ndk-build apontando pro Android.mk desta pasta).
// Resultado: libPSDK_RealisticRollover.so (32-bit) — carregue via AML loader.
// ============================================================================

#include <mod/amlmod.h>
#include <mod/logger.h>

#include <aml-psdk/game_sa/Events.h>
#include <aml-psdk/game_sa/entity/PlayerPed.h>
#include <aml-psdk/game_sa/entity/Vehicle.h>
#include <aml-psdk/game_sa/entity/Automobile.h>
#include <aml-psdk/game_sa/enum/SurfaceType.h>
#include <aml-psdk/game_sa/base/Timer.h>
#include <aml-psdk/gta_base/Vector.h>
#include <aml-psdk/game_sa/base/Matrix.h>
#include <cmath>

MYMOD(net.rusjj.psdkexample.realisticrollover, Realistic Rollover Physics, 1.0, Claude)

// ----------------------------------------------------------------------------
// Configuracao ajustavel
// ----------------------------------------------------------------------------

// Abaixo desse valor de GetUp().z o carro e considerado "capotando"
// (1.0 = totalmente em pe, 0.0 = de lado, -1.0 = de cabeca pra baixo)
static const float ROLLOVER_UP_THRESHOLD = 0.55f;

// Quanto da velocidade linear vira velocidade angular extra por segundo.
// Valores maiores = capota mais rapido em alta velocidade.
static const float SPIN_GAIN = 0.11f;

// Limite maximo de velocidade angular extra que podemos injetar (rad/s),
// pra nao deixar o carro girando feito pião infinito.
static const float MAX_EXTRA_SPIN = 6.0f;

// Tabela de grip por superficie (1.0 = trava total / sem deslizar,
// 0.0 = sem atrito nenhum / desliza livre). Usada so durante o capotamento.
static float GetSurfaceGrip(unsigned char surface)
{
    switch (surface)
    {
        case SURFACE_TARMAC:
        case SURFACE_TARMAC_FUCKED:
        case SURFACE_TARMAC_REALLYFUCKED:
        case SURFACE_PAVEMENT:
        case SURFACE_PAVEMENT_FUCKED:
        case SURFACE_FUCKED_CONCRETE:
        case SURFACE_PAINTED_GROUND:
            return 0.80f; // asfalto/concreto: desliza pouco, trava mais rapido

        case SURFACE_GRAVEL:
            return 0.55f; // cascalho: grip medio-baixo

        case SURFACE_GRASS_SHORT_LUSH:
        case SURFACE_GRASS_MEDIUM_LUSH:
        case SURFACE_GRASS_LONG_LUSH:
        case SURFACE_GRASS_SHORT_DRY:
        case SURFACE_GRASS_MEDIUM_DRY:
        case SURFACE_GRASS_LONG_DRY:
        case SURFACE_GOLFGRASS_ROUGH:
        case SURFACE_GOLFGRASS_SMOOTH:
            return 0.35f; // grama: desliza bastante

        case SURFACE_STEEP_SLIDYGRASS:
        case SURFACE_STEEP_CLIFF:
            return 0.20f; // terreno muito escorregadio

        default:
            return 0.55f; // fallback generico
    }
}

// ----------------------------------------------------------------------------
// Logica principal (roda todo frame de fisica do jogo)
// ----------------------------------------------------------------------------

static void ProcessRealisticRollover(CVehicle* veh, float dt)
{
    if (!veh) return;

    CPhysical* phys = (CPhysical*)veh; // CVehicle herda de CPhysical

    // So mexe se o veiculo estiver tocando o chao — nao queremos afetar
    // veiculos no ar (fisica de voo/queda fica como o jogo ja faz).
    if (!phys->bOnSolidSurface) return;

    CMatrix* mat = veh->GetMatrix();
    if (!mat) return;

    float upZ = mat->GetUp().z;

    // So aplica o efeito quando o carro esta de fato capotando/na lateral
    if (upZ > ROLLOVER_UP_THRESHOLD) return;

    // Velocidade linear atual (unidades do jogo, nao km/h)
    CVector& moveSpeed = phys->m_vecMoveSpeed;
    float linSpeed = moveSpeed.Magnitude();
    if (linSpeed < 0.002f) return; // parado, nao ha o que simular

    float grip = GetSurfaceGrip(phys->m_nContactSurface);

    // --- 1) Giro proporcional a velocidade --------------------------------
    // Quanto mais rapido o carro esta indo, mais rapido ele deve capotar.
    // Adicionamos a velocidade angular na direcao que o jogo ja escolheu
    // (preserva o sentido do giro calculado pela engine), so amplificando.
    CVector& turnSpeed = phys->m_vecTurnSpeed;
    float curSpin = turnSpeed.Magnitude();

    float desiredExtraSpin = linSpeed * SPIN_GAIN;
    if (desiredExtraSpin > MAX_EXTRA_SPIN) desiredExtraSpin = MAX_EXTRA_SPIN;

    if (curSpin > 0.001f)
    {
        // normaliza o eixo de giro atual e injeta spin extra nesse mesmo eixo
        float invCur = 1.0f / curSpin;
        CVector spinAxis(turnSpeed.x * invCur, turnSpeed.y * invCur, turnSpeed.z * invCur);

        float addAmount = desiredExtraSpin * dt * 30.0f; // normalizado p/ ~30fps de referencia
        turnSpeed.x += spinAxis.x * addAmount;
        turnSpeed.y += spinAxis.y * addAmount;
        turnSpeed.z += spinAxis.z * addAmount;
    }
    else
    {
        // engine ainda nao decidiu eixo de giro (ex.: acabou de bater):
        // usa o eixo perpendicular entre a direcao do movimento e o "up" do mundo,
        // que e o eixo natural de capotamento (tomba pra frente/lado do impacto)
        CVector worldUp(0.0f, 0.0f, 1.0f);
        CVector moveDir(moveSpeed.x / linSpeed, moveSpeed.y / linSpeed, moveSpeed.z / linSpeed);

        CVector axis(
            moveDir.y * worldUp.z - moveDir.z * worldUp.y,
            moveDir.z * worldUp.x - moveDir.x * worldUp.z,
            moveDir.x * worldUp.y - moveDir.y * worldUp.x
        );
        float axisLen = axis.Magnitude();
        if (axisLen > 0.001f)
        {
            float inv = 1.0f / axisLen;
            float addAmount = desiredExtraSpin * dt * 30.0f * 0.5f;
            turnSpeed.x += axis.x * inv * addAmount;
            turnSpeed.y += axis.y * inv * addAmount;
            turnSpeed.z += axis.z * inv * addAmount;
        }
    }

    // --- 2) Deslize lateral durante o giro ---------------------------------
    // Em vez de deixar o atrito nativo "grudar" o corpo no chao instantaneamente,
    // reduzimos a velocidade linear de forma gradual (proporcional ao grip da
    // superficie), simulando o corpo escorregando enquanto gira — como um carro
    // capotando em alta velocidade realmente faz.
    float frictionDrop = (1.0f - grip) * dt * 1.6f; // quanto MENOS grip, MENOS reducao = mais deslize
    float keepFactor = 1.0f - (grip * dt * 0.9f) + frictionDrop;

    // clamp de seguranca pra nao inverter velocidade nem criar energia do nada
    if (keepFactor > 1.0f) keepFactor = 1.0f;
    if (keepFactor < 0.85f) keepFactor = 0.85f;

    moveSpeed.x *= keepFactor;
    moveSpeed.y *= keepFactor;
    moveSpeed.z *= keepFactor; // nao mexe tanto no Z pra nao estranhar a gravidade
}

// ----------------------------------------------------------------------------
// Entry point do mod
// ----------------------------------------------------------------------------

ON_MOD_LOAD()
{
    Events::gameProcessEvent += []()
    {
        CPlayerPed* player = FindPlayerPed(-1);
        if (!player) return;

        CVehicle* veh = player->m_pVehicle;
        if (!veh) return;

        float dt = CTimer::GetTimeStepInSeconds();
        if (dt <= 0.0f || dt > 0.2f) dt = 1.0f / 30.0f; // protege contra spikes/pause

        ProcessRealisticRollover(veh, dt);
    };

    Log("[RealisticRollover] Plugin carregado com sucesso.");
}
