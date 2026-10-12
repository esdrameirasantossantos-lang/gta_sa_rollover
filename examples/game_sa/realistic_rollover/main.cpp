#include <mod/amlmod.h>
#include <aml-psdk/game_sa/Events.h>
#include <aml-psdk/game_sa/entity/PlayerPed.h>
#include <aml-psdk/game_sa/entity/Automobile.h>
#include <aml-psdk/game_sa/entity/Physical.h>

MYMOD(
    net.psdk.realisticrollover,
    Realistic Rollover,
    1.0,
    Esdra
)

ON_MOD_LOAD()
{
    Events::gameProcessEvent += []()
    {
        CPlayerPed* player = FindPlayerPed(-1);

        if (!player)
            return;

        CVehicle* vehicle = FindPlayerVehicle(-1, false);

        if (!vehicle)
            return;

        // A lógica de capotamento entra aqui.
    };
}
