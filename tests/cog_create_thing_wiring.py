"""Prevent reintroducing JK.dll's null template dereference or losing its aliases."""
from pathlib import Path

root=Path(__file__).resolve().parents[1]
main=(root/"renderer/main.cpp").read_text(encoding="utf-8")
helper=(root/"renderer/cog_create_thing.hpp").read_text(encoding="utf-8")
doc=(root/"doc/cog_create_thing.md").read_text(encoding="utf-8")
project=(root/"renderer/renderer.vcxproj").read_text(encoding="utf-8")

assert "cog_get_create_thing_template_id" in helper
assert "if(!read(thing+0x7cU,&template_ptr" in helper
assert "template_ptr<0x10000U" in helper
assert "std::uintptr_t(template_ptr)+4U" in helper
assert "cog_pop_thing(ctx)" in main
assert "cog_push_int(ctx,jkgm::cog_get_create_thing_template_id(" in main
assert 'std::strcmp(name,"getthingcreatethingtemplate")==0' in main
assert "jk_create_thing_getter_matches(handler)" in main
assert "0x46ba8U" in main and "0x51130U" in main
assert "register_verb(ctx,cog_get_thing_create_thing,\"getthingcreatething\")" in main
assert "mesh_revision::register_mutator" in main
assert "TrueJkInitNewCogVerbs(register_verb ?" in main
assert "cog_create_thing.hpp" in project
assert "GetThingCreateThingTemplate" in doc and "GetThingCreateThing" in doc
print("JK.dll createThing COG registration/alias guard passed")
