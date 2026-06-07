# Dungeon Instances

Dungeon instances are **private, single-player copies** of a custom map. A player enters with
`/dungeon N`, clears every monster, gets a reward, and is sent back to town. Each player who enters
gets their **own** copy of the map — players never see each other inside a dungeon.

This feature is built on top of the [custom map pipeline](dev-editor.md): you author a map in the
editor, import it to the server, then flag its warp as a dungeon.

## For admins — turning a custom map into a dungeon

1. Author and import the custom map as usual (it gets a map number and a custom world folder, e.g.
   "Old Maze" → world folder `102`).
2. In the **warp list** (admin panel → game configuration → warps), find or create the warp entry
   that targets the map, and tick **`IsDungeon`**.
3. Set the warp's **`LevelRequirement`** to the minimum character level for the dungeon.
4. On the **map definition**, set **`SafezoneMap`** to a town (e.g. Lorencia). This is what the player
   respawns to if they die inside, which is how a death ends the run (see rules below). Make sure the
   map has at least one **spawn gate** exit (`IsSpawnGate`) — that's where the player lands.

A warp flagged `IsDungeon` is **removed from the player Move List** (it is excluded from the
server-pushed map manifest), so it can only be entered with the `/dungeon` command.

The `N` in `/dungeon N` is the warp list **Index**. For "Old Maze" we use index `102` to match its
custom world folder, so the command is `/dungeon 102` — but any warp index works.

## For players — entering a dungeon

1. Type `/dungeon N` (e.g. `/dungeon 102`).
2. The server checks you meet the level requirement and have a **Devil's Key** in your inventory. If
   not, you get a message explaining why and nothing else happens.
3. If the checks pass, a confirmation prompt appears: *"Are you sure you want to enter this dungeon
   instance (name)?"* with **OK / Cancel**.
   - **Cancel** closes the prompt; nothing changes.
   - **OK** consumes **one Devil's Key**, creates your private instance, and teleports you into the
     dungeon's safe zone.

## Rules (v1)

These are the rules the dungeon system enforces, and why:

- **Per-player isolation.** Each player gets a separate instance of the map (its own server-side
  `GameMap`), so multiple players can run the same dungeon at once without seeing each other.
- **Entry cost: one Devil's Key.** The key is required *and consumed* on entry (re-running the dungeon
  needs another key). The existing Devil's Key item is reused (item group 14, number 18).
- **Monsters spawn once and never respawn.** Every monster spawn area on the map is spawned a single
  time when the instance is created; killing a monster does not respawn it — regardless of how the
  spawn was authored in the editor. This is deliberate: a dungeon is a finite clear, not a farm.
- **All spawns are one group.** A dungeon can have many spawn points; they are counted together as a
  single pool. The dungeon is **completed** when the *last* monster across *all* spawn areas dies.
- **Completion reward: 1,000,000 Zen.** On completion the Zen is added directly to your inventory
  Zen (not dropped). If your Zen is already at the cap, you get a message and no reward is granted.
- **Return countdown: 60 seconds.** After completion a one-second-interval countdown is shown as
  system messages (`Returning to Lorencia in 59…`, `58…`, …). When it reaches zero you are teleported
  to **Lorencia** and the instance is torn down.
- **Leaving ends the run.** Warping away, disconnecting, or **dying** (you respawn at the map's
  `SafezoneMap` town) removes you from the instance, which immediately closes and disposes it. A run
  abandoned this way gives no reward, and re-entry costs another key.

## Notes / limitations (v1)

- Dungeon tuning (reward amount, countdown length, the key item) is hardcoded as named constants in
  `DungeonInstanceContext`. A future version may move these to a per-dungeon configuration entity.
- No party support yet — instances are one-per-player.
- The instance framework (`GameInstanceContext`) is intentionally generic so future **arena** /
  **battleground** PvP instances can reuse the same lifecycle, map-isolation and registry.
