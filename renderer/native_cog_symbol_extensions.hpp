#pragma once
// JKGM(5)-specific native COG symbols: vector defaults and new resource refs.
// request() is called inside attach_hooks()'s Detours transaction.
namespace jkgm::native_cog_symbol_extensions {
bool request();
void detach();
void on_failed_commit();
}
