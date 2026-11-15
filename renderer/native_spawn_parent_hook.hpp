#pragma once

namespace jkgm::native_spawn_parent_hook {
// Call only after the main Detours transaction succeeds. Patches five
// signature-verified native CALL instructions on the supplied jkgm(5).exe.
bool install();
void uninstall();
}
