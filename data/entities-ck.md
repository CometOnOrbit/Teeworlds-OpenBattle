# CK Entity Layer

Use `entities-ck.png` as the Game-layer image for CK maps.

| Tile | Purpose |
| --- | --- |
| 1-4 | Solid, death, no-hook, water collision tiles |
| 6, 160, 176-188 | Supply, anti-tank, anti-air, classes, restrictions and water-combat tiles; `186-188` select harpoon, arrows and throwing star |
| 192-202 | Standard spawn, team spawn, flagstand and pickup entities |
| 197-200 | Health supply, ammunition supply, smoke launcher and water enable |
| 214-223, 230-239, 246-255 | Neutral, red and blue helicopter, jet, tank, car, submarine, ship and mini-vehicle spawns; red/blue variants are visibly team-coloured |
| 240 | DDNet Door entity (place it on the Switch layer and set Number) |

Place consecutive Switch-layer Door tiles with the same Number to draw a CK
door. Horizontally, vertically or diagonally continuous tiles are merged into
one laser for each straight run, extending to the outer edges of the first and
last tile. The tile path therefore controls the exact length and shape without
stacking one laser object per brick. A single isolated Door tile is ignored.
Door Number `N` gates entry to CP `N`:
it opens when CP `N` is attackable through any predecessor and remains open
after CP `N` is captured. Number `255` is the final-stage base door.
When closed, a numbered Door blocks attacker characters only; defenders pass
through it, while projectiles and vehicle weapons remain blocked for both teams.

CK objectives use Switch `TILE_SWITCHOPEN` Number `1-16`. CK Tele In Numbers
`1-16` are attacker entrances for the matching CP; `17-32` are defender
entrances. Tele Out Numbers `1-16` are shared by both sides for their matching
CP. Consecutive Tele In tiles with the same Number form one full-length laser
marker spanning the outer edges of the first and last tile, and every tile in
that entrance triggers the teleport. Those
layers use their editor-provided images, so they intentionally have no duplicate
icons in this Game-layer atlas. A Tele In follows its matching CP's ownership:
the defender entrance is active until the attackers
fully capture that CP, then only the attacker entrance is active. A retake
switches it back, so both teams can never use the same CP entrance at once.
When an active Tele In overlaps a closed Door, the Door is shown as the single
laser marker; the short Tele marker returns after that Door opens.

To show a CP flag, place either standard Flagstand entity in the **Switch**
layer and set its Number to the same Number as the objective. It is cosmetic:
the server snapshots it in the team colour that currently controls that Switch
objective, and it can never be picked up.

## CK point graph

Every battle map must ship `maps/<map name>.battle.json`. Its `modes` array
declares `openbattle` and/or `ck`; a CK-capable map additionally supplies the
`ck` object. `ck.points[].number` matches the Switch objective Number; `from`
lists direct predecessor Numbers. Any one predecessor being owned by the
attackers opens an attack on that node. See `maps/ck-graph.example.battle.json`
for the `A → B → C/C-SideA → D` layout.

When a node is captured, every upstream predecessor is made attacking; when it
is retaken, every downstream successor is made defending. All listed `goals`
must be captured before the final flag stage starts.

The old Battlefield single-tile doors, checkpoint lines, checkpoint destinations
and A-C checkpoint tiles are intentionally absent. CK uses the numbered Door,
Switch and Tele mechanisms above instead.
