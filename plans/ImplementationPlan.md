# Ghost-Item Branch Stabilization -- Implementation Checklist

**Objective:** Fix the test 17 off-thread crash, harden against the test 16 menu-close crash, close the craft-blocking gap, and clean up dead code -- without changing the ghost-item product model.

**Baseline:** Current dirty working tree as of 2026-03-20.

---

## Phase 1: Main-Thread Dispatch (P0 -- fixes test 17)

### 1.1 Add handshake atomics and new declarations to `RecipeModeSession.h`

- [ ] Add `#include <atomic>` (already in PCH, but explicit is fine)
- [ ] Add public method: `void RequestToggle()`
- [ ] Change `menuOpen_` from `bool` to `std::atomic<bool>`
- [ ] Change `enabled_` from `bool` to `std::atomic<bool>`
- [ ] Add private member: `std::atomic<std::uint64_t> menuGeneration_{ 0 }`
- [ ] Add private member: `std::atomic<std::int32_t> pendingToggleCount_{ 0 }`
- [ ] Add private member: `std::atomic<bool> toggleTaskQueued_{ false }`
- [ ] Add private method: `void ExecuteQueuedToggle(std::uint64_t capturedGeneration)`
- [ ] Add private members for partial-state tracking: `bool inventoryInjected_{ false }` and `bool menuEntriesInjected_{ false }`
- [ ] Add deferred cleanup buffer: `std::vector<GhostIngredient> deferredCleanup_`
- [ ] Add private method: `void FlushDeferredCleanup()`

### 1.2 Implement `RequestToggle()` in `RecipeModeSession.cpp`

- [ ] `RequestToggle()` increments `pendingToggleCount_` (atomic fetch_add)
- [ ] If `toggleTaskQueued_` is already true, return (coalesce -- don't queue another task)
- [ ] Set `toggleTaskQueued_ = true`
- [ ] Capture `menuGeneration_.load()` into a local
- [ ] Call `SKSE::GetTaskInterface()->AddTask([this, gen]() { ExecuteQueuedToggle(gen); })`
- [ ] Log the enqueue at debug level

### 1.3 Implement `ExecuteQueuedToggle()` in `RecipeModeSession.cpp`

- [ ] Set `toggleTaskQueued_ = false`
- [ ] Drain `pendingToggleCount_` via `exchange(0)` into a local count
- [ ] If count is even, log "toggle cancelled (even count)" and return
- [ ] If `menuGeneration_.load()` != captured generation, log "stale task rejected" and return
- [ ] If `!menuOpen_`, log rejection and return
- [ ] If `!movie_` or `!alchemyMenu_`, log rejection and return
- [ ] If `!IsCurrentMovieAlchemy()`, log rejection and return
- [ ] Call `Toggle()` (which remains unchanged internally)

### 1.4 Update `InputWatcher.cpp`

- [ ] Remove `session.Toggle()` call at line 71
- [ ] Replace with `session.RequestToggle()`
- [ ] Remove the `session.IsCurrentMovieAlchemy()` check at line 43 (this reads `movie_` off-thread)
- [ ] Keep only `session.IsMenuOpen()` as the early-out (reads atomic `menuOpen_`)
- [ ] Log the key event at info level (keep existing log line)

### 1.5 Update menu lifecycle for generation counter

- [ ] In `OnCraftingMenuOpened()`: call `FlushDeferredCleanup()` before any other work
- [ ] In `OnCraftingMenuOpened()`: increment `menuGeneration_` after flush
- [ ] In `OnCraftingMenuClosed()`: increment `menuGeneration_` as the FIRST operation (before any cleanup or pointer invalidation)
- [ ] In `OnCraftingMenuClosed()`: set `menuOpen_ = false` AFTER generation increment

### 1.6 Smoke test (build and verify thread dispatch)

- [ ] Build: `xmake build`
- [ ] Deploy DLL to game
- [ ] Open alchemy menu, press toggle key
- [ ] **Verify**: log shows `ExecuteQueuedToggle` running with same thread ID as "crafting menu opened" (main thread)
- [ ] **Verify**: log does NOT show Toggle() running on the input thread ID
- [ ] Wait 30+ seconds with recipe mode enabled -- no crash (test 17 regression)
- [ ] Toggle spam (5+ rapid presses) -- confirm stale/even-count rejections in log, final state correct

---

## Phase 2: Menu-Close Cleanup Hardening (P1 -- addresses test 16)

### 2.1 Split `OnCraftingMenuClosed()` into deferred cleanup

- [ ] When `enabled_` is true OR `inventoryInjected_` is true OR `menuEntriesInjected_` is true:
  - [ ] Move `ghostIngredients_` into `deferredCleanup_` (std::move, then clear original)
  - [ ] Set `enabled_ = false`, `inventoryInjected_ = false`, `menuEntriesInjected_ = false`
  - [ ] Do NOT call `RemoveGhostItemsFromInventory()` or `RestoreOriginalNames()` inline
  - [ ] Do NOT touch `ingredientEntries`, `selectedIndexes`, `movie_`, or any GFx state
- [ ] Clear `movie_`, `alchemyMenu_`, `uiInjected_` as before
- [ ] Queue deferred cleanup task: `SKSE::GetTaskInterface()->AddTask([this]() { FlushDeferredCleanup(); })`

### 2.2 Implement `FlushDeferredCleanup()`

- [ ] If `deferredCleanup_` is empty, return
- [ ] Get `PlayerCharacter::GetSingleton()`
- [ ] For each ghost in `deferredCleanup_`:
  - [ ] Call `player->RemoveItem(ghost.ingredient, 1, kRemove, nullptr, nullptr)`
  - [ ] Restore `ghost.ingredient->fullName = ghost.originalName`
  - [ ] Log at debug level
- [ ] Clear `deferredCleanup_`
- [ ] Log "deferred cleanup complete"

### 2.3 Gate `FlushDeferredCleanup()` at session entry points

- [ ] Call `FlushDeferredCleanup()` at the start of `OnCraftingMenuOpened()`
- [ ] Call `FlushDeferredCleanup()` at the start of `EnableRecipeMode()`
- [ ] This prevents a fast reopen from letting old deferred cleanup remove newly-added ghost items

### 2.4 Update `DisableRecipeMode()` for live-menu path

- [ ] When menu is still open, keep the current synchronous cleanup (remove menu entries, restore names, remove inventory items, refresh UI)
- [ ] Set `inventoryInjected_ = false` and `menuEntriesInjected_ = false` after cleanup
- [ ] This path does NOT use deferred cleanup -- it runs synchronously on the main thread while the menu is live

### 2.5 Test menu-close hardening

- [ ] Enable recipe mode, close crafting menu -- no crash
- [ ] Verify logs show: generation increment -> session invalidation -> deferred cleanup queued -> deferred cleanup complete
- [ ] Verify NO GFx calls or `ingredientEntries` access during close cleanup
- [ ] Reopen menu immediately after close -- verify `FlushDeferredCleanup()` runs before new session setup
- [ ] Reopen and toggle on again -- verify no orphaned ghost items from previous session

---

## Phase 3: Partial Enable Safety (P1)

### 3.1 Add `SKSE::stl::scope_exit` guard to `EnableRecipeMode()`

- [ ] After `AddGhostItemsToInventory()`, set `inventoryInjected_ = true`
- [ ] Create scope guard:
  ```cpp
  auto guard = SKSE::stl::scope_exit([this]() {
      RemoveGhostItemsFromInventory();
      RestoreOriginalNames();
      ghostIngredients_.clear();
      inventoryInjected_ = false;
      menuEntriesInjected_ = false;
  });
  ```
- [ ] After `AppendGhostEntriesToMenu()`, set `menuEntriesInjected_ = true`
- [ ] After all operations succeed (including `enabled_ = true`), call `guard.release()`

### 3.2 Update cleanup paths to use injection flags

- [ ] `OnCraftingMenuClosed()`: check `inventoryInjected_ || menuEntriesInjected_` in addition to `enabled_`
- [ ] `DisableRecipeMode()`: check `inventoryInjected_` before calling `RemoveGhostItemsFromInventory()`
- [ ] `DisableRecipeMode()`: check `menuEntriesInjected_` before calling `RemoveGhostEntriesFromMenu()`

### 3.3 Test partial enable

- [ ] Manually verify (code review) that if `AppendGhostEntriesToMenu()` throws or fails, the guard undoes inventory injection
- [ ] Build and run -- normal toggle on/off still works with the guard in place

---

## Phase 4: Native Craft Blocking (P1)

### 4.1 Add `ProcessUserEvent` hook declaration to `AlchemyMenuHooks.h`

- [ ] Add type alias: `using ProcessUserEvent_t = bool (AlchemyMenu::*)(RE::BSFixedString*)`
- [ ] Add static relocation: `static inline REL::Relocation<ProcessUserEvent_t> _ProcessUserEvent`
- [ ] Add method: `void ClearCallbacks()`

### 4.2 Hook `ProcessUserEvent` in `AlchemyMenuHooks::Install()`

- [ ] After the existing `Accept` hook (slot 1), add a `ProcessUserEvent` hook at slot 5:
  ```cpp
  _ProcessUserEvent = vtable.write_vfunc(
      5,
      +[](AlchemyMenu* a_menu, RE::BSFixedString* a_control) -> bool
      {
          // Phase 4A: log all controls for discovery
          // Phase 4B: block craft controls when recipe mode is enabled
      });
  ```

### 4.3 Phase 4A -- Discovery build (first iteration)

- [ ] In the hook lambda, log: `spdlog::debug("ProcessUserEvent: control='{}'", a_control ? a_control->c_str() : "<null>")`
- [ ] Always forward to original: `return _ProcessUserEvent(a_menu, a_control)`
- [ ] Build, deploy, test in VR
- [ ] Open alchemy menu, perform crafting actions via keyboard and VR controller
- [ ] **Record** all control names/IDs that appear in the log during craft actions
- [ ] Document the craft-trigger control name(s) for the block list

### 4.4 Phase 4B -- Block craft controls

- [ ] After discovery, add the craft-trigger control(s) to an explicit block:
  ```cpp
  if (RecipeModeSession::GetSingleton().ShouldBlockCraft()) {
      if (*a_control == "Activate"_bsfs || /* other craft triggers */) {
          spdlog::info("ProcessUserEvent: blocked craft control '{}'", a_control->c_str());
          return true;  // handled -- do not forward
      }
  }
  return _ProcessUserEvent(a_menu, a_control);
  ```
- [ ] Keep existing `_bCanCraft` and `CraftButtonPress` interception as UI-level blocking (belt and suspenders)

### 4.5 Add `ClearCallbacks()` to `AlchemyMenuHooks`

- [ ] Implement `ClearCallbacks()`: `callbacks_.clear()`
- [ ] Call `ClearCallbacks()` from `RecipeModeSession::OnCraftingMenuClosed()` before clearing menu pointers
- [ ] Call `ClearCallbacks()` from `RecipeModeSession::BindAlchemyMenu()` when binding a new menu instance (not a duplicate bind)

### 4.6 Test craft blocking

- [ ] Enable recipe mode, attempt craft via keyboard -- blocked
- [ ] Enable recipe mode, attempt craft via VR controller -- blocked
- [ ] Disable recipe mode, craft normally -- works
- [ ] Verify `ProcessUserEvent` log shows the control names that were blocked
- [ ] Verify no potion is created and no ingredients are consumed while recipe mode is enabled

---

## Phase 5: Dead Code Removal (P2)

### 5.1 Exclude dead probe files from xmake target

- [ ] In `xmake.lua`, change `add_files("src/**.cpp")` to exclude the three dead files:
  ```lua
  add_files("src/**.cpp")
  remove_files("src/Hooks/CraftingMenuHook.cpp")
  remove_files("src/Hooks/FrameProbe.cpp")
  remove_files("src/Events/CraftingMenuWatcher.cpp")
  ```
- [ ] Keep the source files in the repo (they document what was tried) but don't compile them
- [ ] Build: `xmake build` -- verify no compile errors from missing symbols

### 5.2 Verify no references to excluded code

- [ ] Grep for `CraftingMenuHook`, `FrameProbe`, `CraftingMenuWatcher` in active source files
- [ ] Confirm `SKSEPlugin.cpp` does not reference them (it already doesn't install them, but verify no includes)
- [ ] Remove any `#include` directives for excluded files if present

---

## Phase 6: Integration Testing

### 6.1 Baseline (no toggle)

- [ ] Open alchemy menu, select ingredients, preview potion, craft -- all normal
- [ ] Close menu, reopen at different station -- still normal
- [ ] With moreHUD active -- no crashes

### 6.2 Test 17 regression (thread safety)

- [ ] Toggle on, ghost ingredients appear with "(0)" suffix
- [ ] Wait 30+ seconds -- no crash
- [ ] Verify all inventory mutation logged from main thread ID
- [ ] Verify no log entries show Toggle/Enable/Disable on input thread ID

### 6.3 Toggle spam

- [ ] Press toggle key 5+ times rapidly while menu stays open
- [ ] Final state matches toggle parity (odd = enabled, even = disabled)
- [ ] Log shows stale/even-count task rejections
- [ ] No orphaned ghost items or corrupted names

### 6.4 Test 16 regression (menu close)

- [ ] Enable recipe mode, close crafting menu -- no crash
- [ ] Logs show: generation increment -> deferred cleanup queued -> cleanup complete
- [ ] No GFx or `ingredientEntries` access during close cleanup
- [ ] moreHUD active during close -- no crash

### 6.5 Reopen race

- [ ] Close menu with recipe mode enabled
- [ ] Reopen immediately (fast as possible)
- [ ] Toggle on again
- [ ] Verify deferred cleanup flushed before new ghost injection
- [ ] No doubled ghost items, no missing cleanup

### 6.6 Interaction test

- [ ] Select ghost ingredient + real ingredient
- [ ] Verify effect combinations and potion preview update correctly
- [ ] 3D item preview works for ghost ingredients

### 6.7 Craft-block test

- [ ] Recipe mode enabled: attempt craft via keyboard -- blocked
- [ ] Recipe mode enabled: attempt craft via VR controller -- blocked
- [ ] Recipe mode disabled: craft normally -- works
- [ ] No potion created and no ingredients consumed while blocked

### 6.8 Side-effect test

- [ ] Check plugin log: no orphaned ghost items after full open/close/toggle cycle
- [ ] Check plugin log: all `fullName` values restored after disable/close
- [ ] Check Papyrus log: minimize `OnItemAdded`/`OnItemRemoved` churn (inherent to ghost-item approach, but should only fire during enable/disable, not continuously)

### 6.9 Compatibility test

- [ ] All above tests with `AHZmoreHUD.esp` and `AHZmoreHUDInventory.esp` loaded
- [ ] Repeat toggle-spam and close-while-enabled with full VR load order
- [ ] No `AlchemyRecipeViewVR.dll` frames on any crash stack

### 6.10 Save residue check

- [ ] Load a save that has stale `xzAlchemyHelper*` Papyrus data from the old mod
- [ ] Verify no interaction between the native plugin and stale Papyrus scripts
- [ ] Verify no ghost items stranded in inventory after load

---

## Summary of Files Modified

| File | Phase | Changes |
|------|-------|---------|
| `src/Runtime/RecipeModeSession.h` | 1, 2, 3 | `RequestToggle()`, atomics, generation counter, injection flags, deferred cleanup buffer |
| `src/Runtime/RecipeModeSession.cpp` | 1, 2, 3 | `RequestToggle()`, `ExecuteQueuedToggle()`, `FlushDeferredCleanup()`, scope_exit guard, deferred close path |
| `src/Events/InputWatcher.cpp` | 1 | Replace `Toggle()` with `RequestToggle()`, remove off-thread state reads |
| `src/Hooks/AlchemyMenuHooks.h` | 4 | `ProcessUserEvent` hook declaration, `ClearCallbacks()` |
| `src/Hooks/AlchemyMenuHooks.cpp` | 4 | `ProcessUserEvent` hook at slot 5, `ClearCallbacks()` implementation |
| `xmake.lua` | 5 | Exclude dead probe files from build |

## Assumptions

- Ghost-item architecture is kept as-is. No overlay or synthetic-entry redesign.
- `SKSE::GetTaskInterface()->AddTask()` is available and runs on main thread (confirmed in CommonLibSSE-NG headers).
- `SKSE::stl::scope_exit` with `release()` is available (confirmed in PCH).
- `AlchemyMenu::ProcessUserEvent` is at vfunc slot 5 (confirmed in `AlchemyMenu.h:69`).
- If `ProcessUserEvent` discovery reveals additional craft paths that bypass both hooks, a follow-up hook into the deeper alchemy action path is required -- but that is a separate task after this stabilization pass.
- The `_bCanCraft` / `CraftButtonPress` interception is kept as UI-level blocking alongside the native `ProcessUserEvent` hook (defense in depth).
