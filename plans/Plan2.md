# Team Review Plan: Alchemy Recipe View VR Current WIP

## Summary
Produce a chat-native technical review centered on the current dirty working tree, using the `tests` archive to explain how the implementation evolved and why the latest branch is failing. The review will be structured for the team, not as a code patch: findings first, then failure theory, then solution paths and a recommended remediation order.

## Review Structure
- Start with the mod’s intended purpose in VR terms: reveal missing-known alchemy ingredients in the crafting UI, keep crafting blocked when entries are display-only, and avoid Papyrus/fake-inventory dependence where SkyrimVR + SKSEVR + CommonLibNG already provide safer native hooks.
- State the current authoritative baseline explicitly: the local WIP branch that reintroduced ghost inventory items and live `IngredientItem` renaming in [`src/Runtime/RecipeModeSession.cpp`](/d:/Dev/Skyrim/Projects/Alchemy%20Recipe%20View%20VR%20-%20SKSE/src/Runtime/RecipeModeSession.cpp) and simplified callback interception in [`src/Hooks/AlchemyMenuHooks.cpp`](/d:/Dev/Skyrim/Projects/Alchemy%20Recipe%20View%20VR%20-%20SKSE/src/Hooks/AlchemyMenuHooks.cpp).
- Separate findings into four implementation phases so the team can see regressions, not just isolated crashes:
  1. Early submenu-probing phase from `tests/1-6`
  2. Committed synthetic-row phase from `tests/9-12`
  3. Overlay-only intermediate phase from `tests/13-14`
  4. Current ghost-item WIP from `tests/16` and root `tests`
- For each phase, include:
  - what the code was trying to do
  - what SkyrimVR/CommonLibNG makes safe vs unsafe there
  - the concrete log/crash signature
  - the failure theory
  - whether the issue is architectural, implementation, or environment-induced

## Core Findings To Present
- Highlight the top architectural gap: the repo README says the mod should avoid fake inventory injection, but the current WIP does the opposite by adding/removing real player inventory entries and mutating shared ingredient names.
- Call out the main current-WIP defects:
  - inventory mutation as display transport is unsafe in VR and leaks into unrelated systems
  - writing to `IngredientItem::fullName` mutates global form state, not menu-local presentation
  - appended menu entries are derived from real `InventoryChanges` nodes, then later removed while external menu listeners are still active
  - craft blocking is only partial UI/callback blocking, not a guaranteed gameplay-path block
  - no stable provenance exists between logs and source revisions because the archive spans multiple untagged local variants
- Call out the main historical defects:
  - `tests/1-6`: `GetCraftingSubMenu()` probing occurs before the submenu is stable; repeated `PostDisplay` polling confirms the pointer is still an executable-image sentinel, not a real `AlchemyMenu`
  - `tests/9-12`: synthetic `InventoryEntryData` row injection plus `SetData`/`ShowItem3D` interception destabilizes the native alchemy/constructible-object path and menu teardown
  - `tests/13-14`: overlay-only approach avoids inventory injection but still appears to hold stale menu/session state across open-close/toggle churn
- Include environment findings from logs as secondary, not primary, causes:
  - save/log pollution from prior Papyrus implementation (`xzAlchemyHelper*` classes missing in `tests/16/Papyrus.0.log`)
  - unrelated Papyrus noise (`Complete Alchemy & Cooking Overhaul.esp` missing, `OnItemRemoved` spam) reduces signal quality
  - `AHZmoreHUD`/`AHZmoreHUDInventory` and other crafting/menu listeners repeatedly appear on crash stacks, so compatibility pressure is real even if they are not root cause

## Evidence To Use
- Use the current WIP logs as the primary evidence set:
  - [`tests/AlchemyRecipeViewVR.log`](/d:/Dev/Skyrim/Projects/Alchemy%20Recipe%20View%20VR%20-%20SKSE/tests/AlchemyRecipeViewVR.log)
  - [`tests/crash-2026-03-20-13-15-09.log`](/d:/Dev/Skyrim/Projects/Alchemy%20Recipe%20View%20VR%20-%20SKSE/tests/crash-2026-03-20-13-15-09.log)
  - [`tests/Papyrus.0.log`](/d:/Dev/Skyrim/Projects/Alchemy%20Recipe%20View%20VR%20-%20SKSE/tests/Papyrus.0.log)
- Use representative historical runs to systemize failure classes:
  - synthetic-row crash set: `tests/9`, `tests/11`, `tests/12`
  - overlay set: `tests/13`, `tests/14`
  - submenu-probe set: `tests/5`, `tests/6`
  - ghost-item teardown set: `tests/16`
- Where crash offsets cannot be symbol-resolved due missing PDBs, state that explicitly and treat source mapping as inference from call shape, surrounding stack data, and the active code path.

## Solution Paths To Recommend
- Recommend abandoning ghost inventory injection as the forward path.
- Recommend returning to a native-display approach, but with narrower hooks than the earlier synthetic-row implementation:
  - keep native player ingredient snapshot/classification in C++
  - keep SWF/button shim only as a trigger or overlay host
  - present missing ingredients through menu-local UI data or overlay rendering, not by modifying player inventory or base form names
  - block crafting in the actual alchemy action path, not just `_bCanCraft`/visual state
  - own menu lifecycle explicitly and treat open/close as invalidation boundaries
- Present an ordered remediation sequence:
  1. remove inventory/base-form mutation from the current branch
  2. restore a non-destructive display model
  3. tighten lifecycle ownership and menu pointer validity rules
  4. reintroduce compatibility testing against moreHUD/crafting listeners
  5. clean save/mod residue before final validation

## Validation Scenarios
- Include the scenarios the team should use to judge any future fix:
  - open alchemy in VR, toggle once, verify missing-known ingredients appear immediately
  - rapid toggle spam while staying in alchemy
  - close menu while enabled, reopen, repeat across multiple stations
  - select ingredients while recipe mode is active and verify 3D/item-preview hooks remain stable
  - attempt craft input through VR controls, keyboard, and delegate callbacks while display-only entries are shown
  - verify no real inventory count/name changes occur
  - verify no `OnItemAdded`/`OnItemRemoved` side effects are triggered by the feature
  - run with `AHZmoreHUD`, `AHZmoreHUDInventory`, and the current VR load order active

## Assumptions
- Canonical baseline is the current WIP working tree, not `HEAD`.
- Final deliverable is a structured chat review, not a repo document.
- Older logs will be used only to explain regressions and failure classes, not to overstate certainty about code that is no longer present.
- No repo edits or implementation work are included in this plan.
