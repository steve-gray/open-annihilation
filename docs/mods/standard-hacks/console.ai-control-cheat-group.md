# AI and Control Cheats

<!-- BEGIN GENERATED: facts -->
| Fact | Value |
| --- | --- |
| Hack id | `console.ai-control-cheat-group` |
| Area | Console (`console`) |
| Scope | sim: part of the profile hash; every machine must agree |
| Runs on | Every machine in the game. |
| Parameters | 0 |
| Status | implemented |
<!-- END GENERATED: facts -->

## Description

`+AI` and `+Control` are cheats. They work in every single-player game, and in a multiplayer game whenever cheats are allowed. The hack no longer chooses that class: on or off, the commands are cheats. It stays in the profile hash. In 3.1c they need developer mode.

## Configuration example

```yaml
hacks:
  console.ai-control-cheat-group: true
```

## Details

### Behaviour

Every console command belongs to a class that decides when it runs:

| Class | Runs when |
| --- | --- |
| Option | Always |
| Cheat | Cheats are allowed: in a campaign or a skirmish, or when the multiplayer lobby's Cheats option is on |
| Developer | Developer mode is on |

`+AI [player]` switches a player between local and computer control, and `+Control <player>` makes that player the local and viewed player. Both are cheats whether or not this hack is on. With cheats allowed, a player can type them on the chat line and the line is echoed to every player. `+AI` is applied on every machine: the machine that plays that player changes who gives the orders, and the others record the same choice. `+Control` changes only the machine that typed it. With cheats not allowed, both are refused. The developer passphrase does not allow them.

Every other command keeps its class. The hack's on or off value is still part of the profile hash, and it no longer changes a class.

### 3.1c baseline

`+AI` and `+Control` are developer commands. They run only in developer mode, and the multiplayer Cheats option does not enable them.

### Network games

Every machine registers `+AI` and `+Control` as cheats. The line is sent to every player. Each machine runs `+AI`; only the machine that typed `+Control` runs that. The hack remains part of the profile hash so old profiles still match, and every machine must still agree on it.

### Interactions

- [`console.lostype-cheat-group`](console.lostype-cheat-group.md) moves `+LOSType` into the cheat class in the same way; the two are independent.

### Implementation notes

- Applied in `console_apply_rules` in `src/ui/console/src/commands.cpp` (module `src/ui/console`). The enabled flag is ignored. `AI` and `Control` are always registered as cheats. The application calls it when it binds the console to a match.
- Tested by `test_mod_console_rules` in `src/ui/console/tests/console_test.cpp` (ctest `ui-console`): the commands run under the cheat class with the hack off and on, and a chat line runs `+ai` only while cheats are allowed.

## Full configuration schema

<!-- BEGIN GENERATED: schema -->
Hack id `console.ai-control-cheat-group`, written under the profile's `hacks` block.

| Write | Means |
| --- | --- |
| `console.ai-control-cheat-group: true` | On. |
| `console.ai-control-cheat-group: false` | Off, the same as leaving it out. `+AI` and `+Control` stay cheats either way. |

### Parameters

This hack has no parameters.

### Presets

This hack has no presets.

### Shorthand

This hack has no parameters to set: write `true` or `false`.

### Every parameter at its default

```yaml
hacks:
  console.ai-control-cheat-group: true
```
<!-- END GENERATED: schema -->
