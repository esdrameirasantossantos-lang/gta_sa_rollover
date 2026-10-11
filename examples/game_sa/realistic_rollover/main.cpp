#include <mod/amlmod.h>
#include <mod/logger.h>
#include <cmath>
#include <algorithm>

MYMOD(
    net.esdragames.realisticrollover,
    RealisticRollover,
    1.0,
    EsdraGames
)

static float clampf(float v, float lo, float hi)
{
    return std::max(lo, std::min(v, hi));
}

struct RolloverState
{
    float forwardSpeed;
    float lateralSpeed;
    float impactSpeed;
    float wheelSideLoad;
    float bodyContact;
};

struct RolloverResult
{
    float slide;
    float rotationFactor;
};

static RolloverResult calculateRollover(
    const RolloverState& s)
{
    const float lateral = std::fabs(s.lateralSpeed);
    const float impact = clampf(
        std::fabs(s.impactSpeed) / 18.0f,
        0.0f, 1.0f);

    const float wheel = clampf(
        s.wheelSideLoad, 0.0f, 1.0f);

    const float body = clampf(
        s.bodyContact, 0.0f, 1.0f);

    const float slip = clampf(
        lateral / 8.0f, 0.0f, 1.0f);

    RolloverResult result{};

    // Não modifica a velocidade lateral do jogo.
    result.slide = slip;

    // Calcula apenas a intensidade relativa da rotação.
    result.rotationFactor = clampf(
        wheel * slip * 0.70f +
        body * impact * 0.30f,
        0.0f, 1.0f);

    return result;
}

extern "C" void OnModLoad()
{
    logger->SetTag("RealisticRollover");

    if (!aml->GetLib("libGTASA.so"))
    {
        logger->Error("GTA SA nao encontrado");
        return;
    }

    logger->Info("RealisticRollover carregado");
    logger->Info("Surface.dat original preservado");
}
