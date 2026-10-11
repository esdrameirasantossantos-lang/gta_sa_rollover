#include <cmath>
#include "plugin.h"
#include "CPhysical.h"
#include "CVehicle.h"
#include "CAutomobile.h"

namespace RolloverPhysics
{
    constexpr float ROLL_FORCE = 0.018f;
    constexpr float SLIDE_FACTOR = 0.985f;
    constexpr float MAX_ROLL_SPEED = 0.22f;
    constexpr float MIN_IMPACT_SPEED = 0.045f;

    inline float Clamp(float v, float min, float max)
    {
        return v < min ? min : (v > max ? max : v);
    }

    void ApplyRollover(CAutomobile* car, float impact)
    {
        if (!car)
            return;

        CPhysical* physical =
            reinterpret_cast<CPhysical*>(car);

        if (impact < MIN_IMPACT_SPEED)
            return;

        const float strength =
            Clamp(impact * ROLL_FORCE, 0.0f, MAX_ROLL_SPEED);

        // Preserve the original linear motion.
        // The collision position should determine the
        // actual torque direction in the final hook.

        physical->m_vecTurnSpeed.x += strength;
    }

    void PreserveBodySlide(CAutomobile* car)
    {
        if (!car)
            return;

        CPhysical* physical =
            reinterpret_cast<CPhysical*>(car);

        // Small damping only; do not erase angular velocity.
        physical->m_vecFrictionMoveSpeed.x *= SLIDE_FACTOR;
        physical->m_vecFrictionMoveSpeed.y *= SLIDE_FACTOR;
    }
}
