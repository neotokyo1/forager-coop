# Changelog

What changed for players in each version. The release workflow puts the section of the tagged version into the
GitHub release, and the same text goes into the Steam Workshop changenote (see `.claude/RELEASE.md`).

Write it for players: what they can do now and what works that did not, not which commits or PRs did it.
One line per change; group related fixes into one line.

## 0.1.9

- The host and guests now see their own outgoing co-op messages with their player name in the T console.

## 0.1.8

- The F3 trade phone recognizes matrix inventories and stacks with up to eight additional integer attributes. Trading preserves those attributes.
- Install version.dll 0.1.8 on the host and all guests (protocol 25).

## 0.1.7

- Press T, type in the Forager console and press Enter to chat with the group. Install and enable the bundled ForagerCoopChat companion on each PC.
- Press F3 in the shared overworld to offer backpack items and review a trade with another player.
- Use the same DLL (protocol 24) on the host and guests.

## v0.1.6

All players need v0.1.6: it does not play with v0.1.5.

### New

- **Bonk.** Swing your pickaxe at another player up close: they are flattened for a moment, with stars over the head
  and a bonk sound. No damage.
- **/dance** in the chat: your character dances to a chiptune tune. Works alone too; any movement key stops it.
- **Friendly fire from bombs.** The blast of your bomb hurts the other players in its reach.
- **The Void together.** Every player goes into the host's Void level: the same islands and enemies, one timer, and
  the leaderboard for everyone at the end.

### Fixed

- **Puzzles work in co-op**: skull island (gem sockets, skull braziers), the sleeping princess, the eye statue, the
  gem puzzle, the three bells, the desert's battery and dig spot, the fire island's pillars and burning chests, the
  frozen chest. Each is solved once for everyone and gives one chest.
- A gem in a socket can be taken out only by the player who put it in.
- Sword aim and sword hits on the other players; fireballs and explosions are seen by everyone.
- Ice rod shots and what they freeze are seen by every player.
- Signs say the same thing to every player, each in their own game language.
- A guest's craft at a furnace or another station is no longer lost; a station is freed when its crafter walks away.
- In-game text no longer shows icons in place of some letters.
- The game checks the world the host sends before loading it.

### For testers (host only)

- `/puzzlereset <n>` sets a puzzle back; `/chestremove` removes a puzzle chest.

Thanks to @justberlim for the puzzle fixes and to @Caf3z1n for the world checks.
