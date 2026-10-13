# `--dplay-guid`

`--dplay-guid GUID` sets the application GUID this game hosts under and
looks for. A search lists a game only when that game was hosted under the
same GUID, and a game this one hosts answers a search only when the search
asks for that GUID.

Without the option the GUID is Total Annihilation's own,
`{99797420-F5F5-11CF-9827-00A0241496C8}`. Games then stay visible to 3.1c
and to Open Annihilation started without the option. Nothing else about
hosting or joining changes.

The GUID is written with or without braces, in any case:

```text
--dplay-guid {99797420-F5F5-11CF-9827-00A0241496C8}
--dplay-guid 99797420-f5f5-11cf-9827-00a0241496c8
```

Those two are the same GUID. Anything that is not a GUID of that form is
refused, with the message `--dplay-guid expects a GUID, with or without
braces`, and the game ends as it does for any other refused option.

A launcher such as TA Forever starts each mod under a GUID of its own, a
UUIDv5 of Total Annihilation's GUID and the mod's name. Pass that GUID and
this game hosts under it and lists only games hosted under it. A player of
that mod on 3.1c, started with the same GUID, then sees the game. A player
looking under Total Annihilation's own GUID does not. For a mod such as
Ridge, the GUID is the one the launcher uses for Ridge; this game does not
derive it.

`--dplay-guid` stands with `--host` and `--join`. It needs neither: a game
opened from the multiplayer screens uses it too.
See [which games are listed](multiplayer/application-guid.md).
