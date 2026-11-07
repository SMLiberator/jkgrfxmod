#pragma once
#include <cstdint>

namespace jkgm::hud_ammo {
    // JKGM's native JK HUD uses weapon BIN ids 0..10, not weapon COG ammo
    // consumption. Keep this default only for GetAmmoBin(); without an
    // override the intercepted HUD calls the original JK function.
    inline int default_bin(int weapon)
    {
        switch(weapon) {
        case 2: case 3: return 11; // Energy
        case 4: return 4;         // Thermal detonators
        case 5: case 6: case 9: return 12; // Power
        case 7: return 15;        // Rail charges
        case 8: return 8;         // Sequencer
        default: return -1;       // Fists, saber, invalid
        }
    }

    struct selection {
        uintptr_t world=0, player=0;
        int weapon=-1;
        int override_bin=-1;
        bool overridden=false;

        void observe(uintptr_t next_world,uintptr_t next_player,int next_weapon)
        {
            if(world==next_world && player==next_player && weapon==next_weapon)
                return;
            world=next_world;
            player=next_player;
            weapon=next_weapon;
            override_bin=-1;
            overridden=false;
        }

        void set(int bin)
        {
            override_bin=bin;
            overridden=true; // -1 explicitly disables the HUD ammo number
        }

        int get() const
        {
            return overridden ? override_bin : default_bin(weapon);
        }
    };
}
