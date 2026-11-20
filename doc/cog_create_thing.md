# GetThingCreateThingTemplate null-pointer fix

The supplied `JK.dll` registers `GetThingCreateThingTemplate` via the
`InitNewCogVerbs` export. Its handler at image RVA `0x51130` checks
whether the passed Thing is null but **does not check** `Thing+0x7c`
(the native `createThing` template pointer) before reading
`*(template+4)`. A null template therefore causes an access violation
at address `0x00000004`.

The renderer replaces this verb **during JK.dll verb registration**, not
by patching the DLL. Both these names invoke the same guarded handler:

```c
int id = GetThingCreateThingTemplate(thing);
int id = GetThingCreateThing(thing);
```

An absent/invalid Thing or template returns `-1`; otherwise the existing
template index is returned. This is intentionally read-only: setter behavior
is unchanged.

The interception is enabled only when the known JK.dll registration thunk
and body match the uploaded binary, and the native JKGM COG helper signatures
are valid. On any other JK.dll the original registration is passed through
unchanged and the alias is not added. Existing mesh-revision interception
remains on the registration path.
