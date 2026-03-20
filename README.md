# Alchemy Recipe View VR - SKSE

Native Skyrim VR SKSE plugin that reveals missing-known alchemy ingredients in the crafting menu, letting the player preview what potions they could make if they had a given ingredient. Replaces the original Papyrus-based [Alchemy Recipe View](https://www.nexusmods.com/skyrimspecialedition/mods/87803) with a C++ runtime.

## Architecture Status

The plugin has gone through several implementation approaches. The current WIP uses **ghost inventory items** -- the same technique as the original Papyrus mod, ported to native C++. Unlike Papyrus, native SKSE/CommonLibSSE-NG code bypasses the VM's frame-synchronized dispatch, so thread ownership and lifecycle safety are the caller's responsibility.

### What the current WIP does (ghost items, tests 15+)
- On toggle: adds real `IngredientItem` forms to player inventory via `AddObjectToContainer`, renames them with a "(0)" suffix via `fullName` mutation, appends corresponding `MenuIngredientEntry` structs to `AlchemyMenu::ingredientEntries`
- On untoggle or menu close: removes items via `RemoveItem`, restores original form names, removes menu entries
- Blocks crafting via `_bCanCraft` Scaleform variable and `CraftButtonPress` callback interception
- Detects alchemy subtype through Scaleform `_subtypeName` inspection (not `GetCraftingSubMenu()`)
- Injects a button shim SWF for UI state sync

### What was tried and abandoned

**Synthetic `InventoryEntryData` injection (tests 9-12):** Constructed fake `InventoryEntryData` entries and spliced them into `AlchemyMenu::ingredientEntries`. Crashed on ingredient selection -- the engine calls `TESForm::GetEnchantment` and other methods on the entry's `object` pointer, and synthetic entries lacked the full expected object graph.

**Display-only Scaleform overlay (tests 13-14):** Used `createTextField` to render missing ingredient names as an overlay panel. Less invasive than inventory injection, but not fully stable -- test 14 crashed inside the DLL during rapid toggle spam with `AHZmoreHUD` on the call stack. Also failed the core product requirement: ingredients appeared in a separate panel, not in the alchemy ingredient list, so the player could not select them, see effect combinations, or preview potions.

**Submenu probing (tests 1-6):** Early attempts to obtain `AlchemyMenu*` via `GetCraftingSubMenu()` before the submenu was stable. The pointer was an executable-image sentinel during initialization. Resolved by switching to the Accept vfunc hook for menu binding.

## Known Issues (Current WIP)

The ghost-item branch has at least two distinct crash classes. Main-thread dispatch is the right first stabilization step for the test 17 crash, but it should not be read as closing the branch.

1. **Thread safety (test 17 crash, P0):** `InputWatcher::ProcessEvent` runs on the input thread and calls `Toggle()` directly, which performs `AddObjectToContainer`, `RemoveItem`, `fullName` writes, `ingredientEntries` mutation, and GFx calls -- all from a non-main thread. The game's main loop concurrently iterates `InventoryChanges` in `PlayerCharacter::Update`. This is the most likely cause of the test 17 crash (`InventoryChanges::VisitWornItems` at `SkyrimVR.exe+01F5DE8`, corrupted pointer in RBX), strongly supported by thread IDs and timing in the logs. Immediate next step: dispatch toggle work to the main thread via `SKSE::GetTaskInterface()->AddTask()` and re-validate `menuOpen_`, `movie_`, `alchemyMenu_` state before mutating inventory or GFx. Note: a queued `AddTask()` can outlive the menu instance that scheduled it -- the task must recheck all pointers and generation counters, not just booleans, especially under rapid toggle or close/reopen between enqueue and execution.

2. **Menu lifecycle crash (test 16 crash):** Separate crash inside `AlchemyRecipeViewVR.dll+0026E6B` on the main thread during menu close with recipe mode enabled. The plugin log shows ghost cleanup completing (remove items, restore names) then the crash occurs, with `AHZmoreHUDInventory.dll` and moreHUD `Events.cpp ProcessEvent` on the call stack. This is likely a stale pointer or post-cleanup access issue, not a threading race. It persists even after the test 15 menu-close teardown fix.

3. **Craft blocking is incomplete:** Crafting is only blocked at the UI/callback layer. `AlchemyMenuHooks.cpp` intercepts `CraftButtonPress` and `RecipeModeSession.cpp` flips `_bCanCraft` via Scaleform. If any alternate craft path exists -- VR controller input, engine-side action, or another mod's craft trigger that bypasses the `FxDelegateHandler` callback -- the player can still craft with ghost-backed ingredients. There is no gameplay-path block at the native alchemy action level.

4. **Global form rename side effects:** `IngredientItem::fullName` is a global form property. The "(0)" rename is visible to vendor inventories, container listings, HUD, and other mods for the duration of recipe mode. If the plugin crashes during enable, names remain corrupted until the next game load.

5. **Partial enable cleanup:** If a crash occurs between `AddGhostItemsToInventory()` and `enabled_ = true`, ghost items are stranded in inventory because `OnCraftingMenuClosed()` gates cleanup on `enabled_`.

6. **Inventory event side effects:** `AddObjectToContainer` and `RemoveItem` fire `OnItemAdded`/`OnItemRemoved` Papyrus events, which other mods (CACO, moreHUD, etc.) may respond to.

7. **Compatibility surface:** Crash stacks in the test archive (including test 14 overlay-phase and test 16 ghost-item phase) show `AHZmoreHUD`/`AHZmoreHUDInventory` and other crafting/menu listeners as active participants on multiple crash stacks. These are not confirmed root causes but they widen the interaction surface. Papyrus logs also show stale `xzAlchemyHelper*` save residue from the old Papyrus mod and unrelated `OnItemRemoved` churn, which reduce signal quality during diagnosis.

## Component Overview

| Directory | Purpose |
|-----------|---------|
| `src/Runtime/` | `RecipeModeSession` -- core state machine for recipe mode toggle, ghost item lifecycle |
| `src/Events/` | `InputWatcher` (hotkey), `MenuLifecycleWatcher` (menu open/close) |
| `src/Hooks/` | `AlchemyMenuHooks` (Accept vfunc hook, CraftButtonPress interception) |
| `src/Alchemy/` | `IngredientRegistry` (348-ingredient index), `PlayerAlchemySnapshot` (owned/known classification) |
| `src/UI/` | `AlchemyUiInjector` (Scaleform state sync, SWF injection, alchemy subtype detection) |
| `src/Config/` | `Settings` (INI loading) |

## Build

```powershell
xmake build
```

## Deploy

Assets staged in:
- `deploy/SKSE/Plugins/AlchemyRecipeViewVR.ini`
- `deploy/Interface/AlchemyRecipeViewVR.swf`
- `deploy/Interface/Translations/`

## Dependencies

- local `commonlibsse-ng` under `lib/commonlibsse-ng`, or
- shared clone at `../CustomSkillsFrameworkVR/lib/commonlibsse-ng`
