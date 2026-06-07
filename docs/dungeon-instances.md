# Dungeon Instances

Dungeon instances are **private, per-party copies** of a custom map. A party member enters with
`/dungeon N`; after a ready check, the whole party is warped into a shared private instance, clears
every monster, and is rewarded. A solo player runs the same flow as a party of one. Each party gets
its **own** copy of the map — other parties running the same dungeon never see each other.

This feature is built on top of the [custom map pipeline](dev-editor.md): you author a map in the
editor, import it to the server, then flag its warp as a dungeon.

## For admins — turning a custom map into a dungeon

1. Author and import the custom map as usual (it gets a map number and a custom world folder, e.g.
   "Old Maze" → world folder `102`).
2. In the **warp list** (admin panel → game configuration → warps), find or create the warp entry
   that targets the map, and tick **`IsDungeon`**.
3. Set the warp's **`LevelRequirement`** to the minimum character level for the dungeon.
4. On the **map definition**, set **`SafezoneMap`** to a town (e.g. Lorencia) — this is where members
   are sent when the run ends. Make sure the map has at least one **spawn gate** exit (`IsSpawnGate`),
   which is where members land and where they respawn after dying (while lives remain).

A warp flagged `IsDungeon` is **removed from the player Move List** (it is excluded from the
server-pushed map manifest), so it can only be entered with the `/dungeon` command.

The `N` in `/dungeon N` is the warp list **Index** (not the map number/world folder). Use the actual
warp index — e.g. `/dungeon 50`.

## For players — entering a dungeon

1. A party member types `/dungeon N`.
2. The server validates: the **whole party must be online**, **every member** must meet the level
   requirement, and the **initiator** must hold a **Devil's Key**. If a member is under-level you get
   `"{name} does not meet the requirements of (level Y)"`; if anyone is offline or the initiator has no
   key, you're told why and nothing happens.
3. A **ready-check window** opens for every party member:

   ```
   Ready check!
   Dungeon: Old Maze
   Players ready (0/3)
   [ Ready ]   [ Cancel ]
   ```

   - Clicking **Ready** marks you ready (the button becomes **Unready**); the count updates live for
     everyone. Click again to un-ready.
   - Any member clicking **Cancel** closes the window for everyone with
     `"{name} canceled the ready check"`. (The check also auto-cancels if a member disconnects or after
     a timeout.)
   - When **all** members are ready, the initiator's Devil's Key is consumed and the whole party is
     warped into one shared instance safe zone.

## Rules

- **Per-party isolation.** Each party gets a separate `GameMap` instance; other parties don't see it.
- **Entry cost: one Devil's Key** (item group 14 / number 18), consumed from the **initiator** only.
- **Monsters spawn once, never respawn, and drop nothing.** Every spawn area is spawned a single time
  with drops disabled (focus on the fight). All spawns count as **one group**; clearing the *last*
  monster completes the run.
- **Shared lives (5).** The party shares 5 lives. When a member dies, a life is spent and they respawn
  at the dungeon safe zone **inside the same instance**. When a member dies while **0 lives remain**,
  the run ends — everyone is sent to town (no reward) and the instance is deleted.
- **Completion reward: 1,000,000 Zen to each present member**, then a 60-second countdown
  (`Returning to Lorencia in 59…`, …) before everyone is teleported to **Lorencia**.
- **Disconnect / reconnect.** Disconnecting doesn't end the run for the others. While the instance is
  still alive (at least one member present), a reconnecting member **rejoins** it; if the instance was
  already deleted, they spawn in town instead.
- **Empty instance is deleted.** Once every member has left (warped out or disconnected) the instance
  is torn down. (A server restart deletes all instances; members then log back into town.)

## Notes / limitations

- Lives (5), reward (1,000,000 Zen per present member) and the ready-check timeout are hardcoded
  constants in `DungeonInstanceContext` / `DungeonReadyCheck`. A future version may move these to a
  per-dungeon configuration entity.
- The reward is granted to each present member in full (not split).
- The instance framework (`GameInstanceContext`) is intentionally generic so future **arena** /
  **battleground** PvP instances can reuse the same lifecycle, member tracking and registry.
