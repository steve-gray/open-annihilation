# Which games a search lists

A search lists a game when the game was hosted under the same application
GUID this game is looking for. A game this one hosts answers a search that
asks for that same GUID. A search for any other GUID goes unanswered, and
a reply under any other GUID is left off the list. The player then sees no
game, the same as when nobody is hosting.

The GUID is Total Annihilation's own unless
[`--dplay-guid`](../command-line.md) names another. With none named, Open
Annihilation and 3.1c list each other's games, as they do today.

A launcher such as TA Forever gives each mod a GUID of its own, a UUIDv5
of Total Annihilation's GUID and the mod's name, and starts the game under
it. Pass that GUID to Open Annihilation and the games of that mod are the
ones listed, including a game a 3.1c player hosts under it. Games hosted
under Total Annihilation's own GUID stay off that list, and a player
looking under the own GUID does not see the mod's game. For a mod such as
Ridge, use the GUID the launcher shows for Ridge.
