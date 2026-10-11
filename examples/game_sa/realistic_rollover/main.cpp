
#include <cmath>

enum class SurfaceType {
    Asphalt,
    Concrete,
    Dirt,
    Grass,
    Sand,
    Metal,
    WetRoad
};

struct SurfaceProfile {
    float tireGrip;
    float bodyFriction;
    float rollingResistance;
    float tireCatch;
};

struct ContactState {
    float forwardSpeed;
    float lateralSpeed;
    float impactSpeed;

    // 0.0 a 1.0: carga lateral nas rodas
    float wheelSideLoad;

    // 0.0 a 1.0: contato da carroceria com o chão
    float bodyContact;
};

struct SurfaceResult {
    float tireGrip;
    float bodySlide;
    float rollingResistance;

    // Intensidade relativa que pode contribuir
    // para o capotamento. Não é torque físico real.
    float rolloverContribution;
};

static float clampf(float v, float lo, float hi) {
    return v < lo ? lo : (v > hi ? hi : v);
}

static SurfaceProfile getSurface(SurfaceType type) {
    switch (type) {
        case SurfaceType::Asphalt:
            return {1.00f, 0.80f, 0.08f, 1.00f};

        case SurfaceType::Concrete:
            return {0.95f, 0.85f, 0.10f, 0.95f};

        case SurfaceType::Dirt:
            return {0.65f, 0.55f, 0.22f, 0.65f};

        case SurfaceType::Grass:
            return {0.50f, 0.45f, 0.28f, 0.50f};

        case SurfaceType::Sand:
            return {0.38f, 0.65f, 0.65f, 0.35f};

        case SurfaceType::Metal:
            return {0.45f, 0.30f, 0.06f, 0.40f};

        case SurfaceType::WetRoad:
            return {0.58f, 0.65f, 0.10f, 0.55f};
    }

    return {0.70f, 0.60f, 0.15f, 0.70f};
}

static SurfaceResult calculateSurface(
    SurfaceType type,
    const ContactState& contact
) {
    SurfaceProfile s = getSurface(type);
    SurfaceResult out{};

    float speed = std::fabs(contact.forwardSpeed);
    float lateral = std::fabs(contact.lateralSpeed);
    float impact = clampf(contact.impactSpeed / 18.0f,
                          0.0f, 1.0f);

    float wheelLoad = clampf(contact.wheelSideLoad,
                             0.0f, 1.0f);

    float body = clampf(contact.bodyContact, 0.0f, 1.0f);

    // A velocidade lateral aumenta a tendência de deslizar.
    float lateralSlip = clampf(lateral / 8.0f, 0.0f, 1.0f);

    out.tireGrip = s.tireGrip;

    out.bodySlide = clampf(
        (1.0f - s.bodyFriction) * (0.35f + lateralSlip),
        0.0f, 1.0f
    );

    out.rollingResistance = clampf(
        s.rollingResistance * (0.25f + speed / 10.0f),
        0.0f, 1.0f
    );

    // A roda precisa estar carregada lateralmente
    // e existir movimento lateral para contribuir.
    float wheelContribution =
        wheelLoad *
        lateralSlip *
        s.tireGrip *
        s.tireCatch;

    // O contato da carroceria acrescenta influência
    // conforme o impacto e o atrito da superfície.
    float bodyContribution =
        body * impact * s.bodyFriction * 0.25f;

    // Não existe rotação automática por inclinação.
    // O jogo ainda precisa aplicar a força resultante.
    out.rolloverContribution = clampf(
        wheelContribution + bodyContribution,
        0.0f, 1.0f
    );

    return out;
}
