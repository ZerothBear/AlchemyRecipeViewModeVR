# Crash Analysis Report -- AlchemyRecipeViewVR Ghost-Item Branch (2026-03-20)

## 1. Executive Summary

The ghost-item branch has **at least two distinct crash classes**, plus several design-level liabilities that persist even after crash fixes. Main-thread dispatch via `SKSE::GetTaskInterface()->AddTask()` is the right P0 stabilization step for the test 17 crash, but it does not close the branch.

**Test 17 crash:** Inventory/state corruption caused by off-thread mutation. All recipe mode operations execute on the input thread (thread 55072 in the logs) while the main game loop (thread 29576) concurrently iterates `InventoryChanges`. Strongly supported by thread IDs and timing, but the exact corruption mechanism (torn pointer vs. other data race) is inference, not proven.

**Test 16 crash:** Separate crash inside `AlchemyRecipeViewVR.dll` on the main thread during menu close with recipe mode enabled, with `AHZmoreHUDInventory` on the call stack. Not a threading race -- likely a stale pointer or post-cleanup access issue.

**Ongoing design costs (not crash-specific):** Even on the main thread, the current design still mutates global form state (`fullName`), injects/removes real inventory items (triggering Papyrus events for other mods), splices menu entries directly, and blocks crafting only at the UI/callback layer.

---

## 2. Crash Analysis

### 2.1 Test 17: Off-Thread Inventory Mutation

**Crash signature:**
```
EXCEPTION_ACCESS_VIOLATION at SkyrimVR.exe+01F5DE8
  InventoryChanges::VisitWornItems -- mov rcx, [rbx]
  RBX = 0x040000000000 (corrupted pointer)
  Access Violation: Tried to read memory at 0x040000000000
  Crash Thread: 29576 (main game thread)
```

**Call stack:** `VisitWornItems` -> `WornHasKeyword("ArmorGauntlets")` -> `TESCondition` evaluation -> `ActiveEffect` ("Alchemy Background Effect", AlchEquipment.esp) -> `PlayerCharacter::Update` -> `Main::Update` -> `MainLoop`

**Plugin log timeline:**
```
[09:14:59.848] [55072] InputWatcher: toggling recipe mode on key 35
[09:14:59.848] [55072] PlayerAlchemySnapshot: owned=16 known=7
[09:14:59.848] [55072] RecipeModeSession: built 4 ghost ingredients
[09:14:59.849] [55072] added ghost 'Fly Amanita (0)'
[09:14:59.854] [55072] added ghost 'Creep Cluster (0)'
[09:14:59.854] [55072] added ghost 'Mara's Eye (0)'
[09:14:59.854] [55072] added ghost 'Giant's Toe (0)'
[09:14:59.854] [55072] appended 4 ghost entries to ingredientEntries (total=20)
[09:14:59.856] [55072] menu refreshed
[09:14:59.856] [55072] enabled recipe mode with 4 ghost ingredients
                        <-- NO MORE LOG ENTRIES (crash at 09:15:09, 10 seconds later)
```

All operations ran on thread 55072 (input thread). The crash occurred on thread 29576 (main thread). The entire `EnableRecipeMode()` -- including `AddObjectToContainer`, `AppendGhostEntriesToMenu`, `RefreshMenu`, and `SetCraftingBlocked` -- executed from the input event handler.

**Reconstructed mechanism (theory, not proven):**
1. Input thread (55072) calls `player->AddObjectToContainer()`, modifying `InventoryChanges::entryList` (a `BSSimpleList`, not thread-safe)
2. Main thread (29576) concurrently iterates the same `entryList` via `VisitWornItems`
3. Concurrent read/write produces a corrupted pointer (RBX = `0x040000000000`)
4. Main thread dereferences corrupted pointer -> crash

The value `0x040000000000` (2^42) is consistent with a torn read, but this is a plausible interpretation, not a confirmed fact from the crash log alone. The stronger supported claim is: inventory/state corruption caused by off-thread mutation.

### 2.2 Test 16: Menu Lifecycle Crash (Separate Class)

**Crash signature:**
```
EXCEPTION_ACCESS_VIOLATION at AlchemyRecipeViewVR.dll+0026E6B
  mov rax, [rbx]
  RBX = 0x1D6B0D4C000
  Crash Thread: 52120 (main thread)
```

**Call stack:** Four frames inside `AlchemyRecipeViewVR.dll` -> `Main::Update` -> `MainLoop`. Stack data includes `AHZmoreHUDInventory.dll+0013C6E`, `moreHUD Events.cpp ProcessEvent`, and `"Crafting Menu"` string.

**Plugin log:** The last entries before the crash are ghost cleanup on the main thread:
```
[22:58:05.148] [52120] skipping duplicate AlchemyMenu bind for 0x1d6af64ce00
[22:58:05.153] [52120] removed ghost 'Fly Amanita (0)'
...
[22:58:05.154] [52120] restored name 'Giant's Toe'
<-- CRASH (no "crafting menu closed" logged)
```

This crash happens during or immediately after the `OnCraftingMenuClosed` cleanup path, on the main thread. It is **not** a threading race. It appears to be a stale pointer access after ghost cleanup, possibly triggered by a menu event listener (moreHUD) accessing state that our cleanup just invalidated.

This crash persists even after the test 15 menu-close teardown fix (which removed GFx operations from `OnCraftingMenuClosed`).

### 2.3 Test 14: Overlay-Phase Crash (Historical, Different Code)

For completeness: test 14 crashed inside `AlchemyRecipeViewVR.dll+0026816` during the overlay-only implementation phase, after rapid toggle spam (8 cycles in ~30 seconds) followed by a menu close/reopen. `AHZmoreHUDPlugin.dll` and moreHUD `Events.cpp` were on the call stack. The overlay phase was not crash-free.

---

## 3. Issues Identified in the Codebase

### ISSUE 1 -- Thread-Safety Violation (CRITICAL, test 17 root cause)

**Severity**: CRITICAL
**Files**: `src/Runtime/RecipeModeSession.cpp`, `src/Events/InputWatcher.cpp`

`InputWatcher::ProcessEvent` runs on the BSInputDeviceManager input thread. It calls `session.Toggle()` which invokes the full `EnableRecipeMode()` / `DisableRecipeMode()` pipeline directly. This pipeline performs:

- `player->AddObjectToContainer()` -- modifies `InventoryChanges::entryList` (not thread-safe)
- `player->RemoveItem()` -- modifies `InventoryChanges::entryList` (not thread-safe)
- `ghost.ingredient->fullName = ...` -- mutates a global `TESForm` property read by render/main threads
- `alchemyMenu_->ingredientEntries.push_back()` -- modifies a `BSTArray` owned by the menu system
- `movie_->SetVariable()` / `Invoke()` -- GFx calls not guaranteed thread-safe

All of these happen concurrently with the main thread's `PlayerCharacter::Update` which iterates inventory, evaluates conditions, and renders UI.

**Why the original Papyrus mod doesn't have this problem**: Papyrus `AddItem()` is dispatched to the game's script processing thread and executed in frame-synchronized batches. The Papyrus `WaitMenuMode()` loop ensures operations happen in step with the game loop. Native C++ bypasses all of this synchronization.

### ISSUE 2 -- Menu Lifecycle / Stale Pointer Access (HIGH, test 16 root cause)

**Severity**: HIGH
**Files**: `src/Runtime/RecipeModeSession.cpp`

The test 16 crash occurs inside our DLL on the main thread during menu close with recipe mode enabled. Ghost cleanup runs (remove items, restore names) but the crash occurs before "crafting menu closed" is logged, with `AHZmoreHUDInventory ProcessEvent` on the stack.

Possible mechanisms:
- Our cleanup modifies `ingredientEntries` or inventory state, then a menu event listener (moreHUD) fires and accesses entries we just removed
- The `alchemyMenu_` pointer is set to nullptr during cleanup, but a concurrent event dispatch still holds a reference
- `RemoveGhostEntriesFromMenu()` clears and rebuilds `ingredientEntries`; a listener iterating the array during the rebuild dereferences a freed entry

This is a separate fix from the threading issue and needs its own investigation.

### ISSUE 3 -- Craft Blocking is Incomplete (HIGH)

**Severity**: HIGH -- gameplay integrity risk
**Files**: `src/Hooks/AlchemyMenuHooks.cpp:26`, `src/Runtime/RecipeModeSession.cpp:366`

Crafting is only blocked at the UI/callback layer:
- `AlchemyMenuHooks.cpp` intercepts `CraftButtonPress` via `FxDelegateHandler::CallbackProcessor`
- `RecipeModeSession.cpp` sets `_root.Menu._bCanCraft = false` via Scaleform

If any alternate craft path exists -- VR controller input, engine-side action, or another mod's craft trigger that bypasses the `FxDelegateHandler` callback -- the player can still craft with ghost-backed ingredients. There is no gameplay-path block at the native alchemy action level.

### ISSUE 4 -- Shared State Without Synchronization (HIGH)

**Severity**: HIGH
**File**: `src/Runtime/RecipeModeSession.h`

All session state is accessed from both the input thread and main thread without synchronization:

```cpp
RE::GFxMovieView* movie_{ nullptr };          // read/written from both threads
AlchemyMenu*      alchemyMenu_{ nullptr };    // read/written from both threads
bool menuOpen_{ false };                      // read/written from both threads
bool enabled_{ false };                       // read/written from both threads
std::vector<GhostIngredient> ghostIngredients_{};  // read/written from both threads
```

Race scenarios:
- Input thread reads `menuOpen_=true`, main thread sets it to `false`, input thread proceeds to use `movie_` which is now `nullptr`
- Input thread is in `EnableRecipeMode()` building ghost ingredients; main thread fires `OnCraftingMenuClosed()` which clears `ghostIngredients_` mid-iteration
- TOCTOU in `EnableRecipeMode`: checks `if (!movie_ || !alchemyMenu_)` at line 114, but by line 130 those could be null

### ISSUE 5 -- Global Form Rename Side Effects (MEDIUM)

**Severity**: MEDIUM -- cosmetic corruption risk
**File**: `src/Runtime/RecipeModeSession.cpp:207-208`

`IngredientItem::fullName` is a global form property. The "(0)" rename is visible to vendor inventories, container listings, HUD, and other mods for the duration of recipe mode. If the plugin crashes during enable, names remain corrupted until the next game load.

### ISSUE 6 -- No Error Recovery on Partial Enable (MEDIUM)

**Severity**: MEDIUM -- leaves ghost items stranded in inventory
**File**: `src/Runtime/RecipeModeSession.cpp:108-141`

If a crash occurs between `AddGhostItemsToInventory()` (items added) and `enabled_ = true` (state not set), the ghost items are in the player's inventory but `enabled_` is false. `OnCraftingMenuClosed()` checks `if (enabled_)` before cleanup -- so the ghost items and renamed forms would be orphaned.

### ISSUE 7 -- Inventory Event Side Effects (MEDIUM)

**Severity**: MEDIUM
**Files**: `src/Runtime/RecipeModeSession.cpp:210`, `src/Runtime/RecipeModeSession.cpp:319`

`AddObjectToContainer` and `RemoveItem` fire `OnItemAdded`/`OnItemRemoved` Papyrus events, which other mods (CACO, moreHUD, etc.) may respond to. This widens the interaction surface and may contribute to the test 16 crash class.

### ISSUE 8 -- Stale Callback Pointers in AlchemyMenuHooks (LOW)

**Severity**: LOW -- theoretical risk
**File**: `src/Hooks/AlchemyMenuHooks.cpp:93-96`

The `callbacks_` map accumulates function pointers across menu sessions and is never cleared.

### ISSUE 9 -- Debug/Probe Code Still Compiled (LOW)

**Severity**: LOW -- dead code
**Files**: `src/Hooks/CraftingMenuHook.cpp`, `src/Hooks/FrameProbe.cpp`, `src/Events/CraftingMenuWatcher.cpp`

Not installed in `SKSEPlugin.cpp` but still compiled. Should be removed or gated.

---

## 4. Proposed Solutions

### 4.1 P0: Main-Thread Dispatch (Fixes Test 17)

Defer all toggle work to the main thread via `SKSE::GetTaskInterface()->AddTask()`.

```cpp
// InputWatcher::ProcessEvent (input thread) -- only queues:
session.RequestToggle();

// RecipeModeSession:
void RecipeModeSession::RequestToggle() {
    SKSE::GetTaskInterface()->AddTask([this]() {
        Toggle();  // Now runs on main thread
    });
}
```

**Critical caveat:** A queued `AddTask()` can outlive the menu instance that scheduled it. The task must recheck all pointers and state when it executes, not just booleans. Under rapid toggle or close/reopen between enqueue and execution, simple `if (menuOpen_)` checks are insufficient -- consider a menu generation counter that invalidates stale tasks.

**This fix addresses test 17 but does NOT address:**
- Test 16 crash (menu lifecycle / stale pointer -- separate investigation needed)
- Craft blocking incompleteness
- Global form rename leakage
- Inventory event side effects
- moreHUD/listener compatibility

### 4.2 P1: Menu Lifecycle Safety (Addresses Test 16)

Needs investigation. Candidate approaches:
- Defer `ingredientEntries` manipulation to after all event listeners have finished processing
- Use a generation counter on `alchemyMenu_` to detect stale access
- Investigate whether `RemoveGhostEntriesFromMenu()` triggers re-entrant event dispatch that hits our own cleared state

### 4.3 P1: Partial Enable Cleanup Guard

Use `SKSE::stl::scope_exit` (available in CommonLibSSE-NG PCH) instead of `sg::make_scope_guard` (not in repo):

```cpp
void RecipeModeSession::EnableRecipeMode()
{
    // ... build ghost ingredients ...
    AddGhostItemsToInventory();

    auto guard = SKSE::stl::scope_exit([this]() {
        RemoveGhostItemsFromInventory();
        RestoreOriginalNames();
        ghostIngredients_.clear();
    });

    AppendGhostEntriesToMenu();
    enabled_ = true;
    SyncRootState();
    SetCraftingBlocked(true);
    RefreshMenu();

    guard.release();  // success -- don't undo
}
```

### 4.4 P1: Atomic Guards for Cross-Thread Reads

Even with main-thread dispatch, `InputWatcher::ProcessEvent` reads `menuOpen_` from the input thread:

```cpp
std::atomic<bool> menuOpen_{ false };
std::atomic<bool> enabled_{ false };
```

### 4.5 P2: Craft Blocking at Gameplay Level

The current UI/callback-only blocking needs a deeper investigation into whether a native-level craft block is feasible (e.g., hooking the actual `CraftSelectedItem` or the constructible object creation path).

---

## 5. Files to Modify

| File | Change | Priority |
|------|--------|----------|
| `src/Runtime/RecipeModeSession.h` | Add `RequestToggle()`, make `menuOpen_`/`enabled_` atomic, add menu generation counter | P0 |
| `src/Runtime/RecipeModeSession.cpp` | Implement `RequestToggle()` via `AddTask()` with full state revalidation | P0 |
| `src/Events/InputWatcher.cpp` | Change `session.Toggle()` to `session.RequestToggle()` | P0 |
| `src/Runtime/RecipeModeSession.cpp` | Investigate test 16 crash -- menu lifecycle / stale pointer after cleanup | P1 |
| `src/Runtime/RecipeModeSession.cpp` | Add `SKSE::stl::scope_exit` guard for partial enable cleanup | P1 |
| `src/Hooks/AlchemyMenuHooks.cpp` | Investigate deeper craft blocking (gameplay-path, not just UI) | P2 |
| `xmake.lua` or source files | Remove or gate debug probe code | P2 |

---

## 6. Verification Plan

1. **Build**: `cd "D:/Dev/Skyrim/Projects/Alchemy Recipe View VR - SKSE" && xmake build`
2. **Deploy** DLL to game
3. **Test in VR** (with full load order including `AHZmoreHUD`, `AHZmoreHUDInventory`, and other crafting/menu listeners):
   - Open alchemy menu -> verify normal behavior (select, craft)
   - Press toggle key -> verify ghost ingredients appear with "(0)" suffix without crash
   - Wait 30+ seconds with recipe mode enabled (test 17 crashed at ~10s)
   - Select ghost + real ingredient -> verify potion preview works
   - Verify craft button is blocked (keyboard and VR controller)
   - Attempt craft via any alternate path (VR grip, controller button)
   - Press toggle key again -> verify ghost ingredients disappear, normal behavior restored
   - Close menu while recipe mode is on -> verify clean teardown (test 16 crash class)
   - Reopen menu -> verify normal state
   - Toggle on/off rapidly (5+ cycles in <10 seconds) -> stress test task lifecycle
   - Close menu mid-toggle-spam -> verify no orphaned items or corrupted names
   - Verify no `OnItemAdded`/`OnItemRemoved` side effects fire for other mods (check Papyrus log)
4. **Log verification**:
   - Confirm all inventory operations log from the main thread (same thread ID as menu lifecycle events)
   - Confirm no stale task executes after menu close (generation counter should reject it)
5. **Save residue check**: Load a save with stale `xzAlchemyHelper*` data, verify no interaction with native plugin
