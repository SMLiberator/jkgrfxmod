"""Static native wiring guards for local-player HUD ammo COG extension."""
from pathlib import Path
s=(Path(__file__).resolve().parents[1]/"renderer/main.cpp").read_text()
for token in (
  "native_hud_get_ammo_addr=0x00408F40U",
  "native_inventory_get_bin_addr=0x004D33B0U",
  "native_set_weapon_addr=0x004D1A10U",
  "native_world_global=0x00833A10U",
  "hud_ammo_selection.observe(local.world,local.thing,local.weapon)",
  'cog_verb_entry{"setammobin", cog_set_ammo_bin}',
  'cog_verb_entry{"getammobin", cog_get_ammo_bin}',
  "DetourAttach(&(PVOID &)RealHudGetWeaponAmmo,",
  "DetourAttach(&(PVOID &)RealSetCurrentWeapon,",
  "DetourDetach(&(PVOID &)RealSetCurrentWeapon,HookedSetCurrentWeapon)",
  "DetourDetach(&(PVOID &)RealHudGetWeaponAmmo,HookedHudGetWeaponAmmo)",
  "if(!hud_ammo_selection.overridden)",
  "return RealHudGetWeaponAmmo(thing);",
  "if(bin==-1)return -999;",
  "return static_cast<int>(std::clamp(*item,0.0f,",
  "std::strcmp(verb.name,\"setammobin\")==0",
):
    assert token in s,token
assert s.index("RealSetCurrentWeapon(thing,weapon);") < s.index(
    "update_local_hud_selection(local);",s.index("void __cdecl HookedSetCurrentWeapon"))
print("HUD ammo COG and native hook wiring OK")
