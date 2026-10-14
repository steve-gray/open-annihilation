# Controls

The keys a match answers, beyond the buttons on the order panel.

## Groups

Ctrl and a digit from 1 to 9 stores the selection in that group. Alt and
the digit selects the group. With **Select groups without Alt** on
(`+switchalt`), the digit alone selects the group and Alt and the digit
picks a build button.

The first press selects the group, or adds it when Shift is held. A later
press, at once or after a pause, centres the view on the group when that
group is already exactly the selection. The view goes to the average of
the units' positions, and the camera does not
start following a unit. T follows the selection. Home centres on the
selected unit and does not follow it.

A gamepad's group buttons do the same. With the groups grip held, a tap
selects the group and a second tap centres on it. See
[controllers.md](controllers.md).

## The window's edge

**Window edge**, under Settings > Controls, decides what a window does with
the pointer while a match's battlefield shows. It has three ways: Capture,
Sticky and Free. Full screen keeps the pointer on the screen whichever is
chosen.

**Capture**, the default, keeps the pointer in the window while you play.
No push and no flick takes it out, so it rests on the last pixel of every
edge and the view edge-scrolls there, as it does in full screen. The game
asks the system to confine it: the window server keeps it in the window on
macOS, the cursor clip on Windows, a pointer grab on X11 and the pointer
constraint on Wayland. The pointer is free again:

- in the game menu, Settings and the other panels it opens, and on the
  pause screen;
- after you press Escape, until you click the battlefield again;
- in Open Annihilation's own menus and setup, and outside a match;
- when the window loses the focus, with Alt+Tab, Command+Tab or a dialog
  of the system's.

A click on the battlefield captures it again. After the window lost the
focus, that first click only brings the window back: it selects nothing and
gives no order, and its release does nothing either. After Escape the click
captures the pointer and acts as any click does. Closing the game menu with
Resume, or the game going on after a pause, captures the pointer again
while it is over the window.

**Sticky** keeps the pointer on the window's edges, as firmly as **Edge
stickiness**, the slider under Window edge, says. It rests on the last
pixel and the view scrolls. A firm push outward, or arriving at the edge
quickly, lets it leave. After that it is held again only once it has come
back in and moved about 24 points away from the edges. Motion along an edge
does not let it go. Menus, the pause screen and a window without the focus
leave the pointer free, as they do with Capture.

**Free** leaves a window's edge as the game always did.

### Edge stickiness

Edge stickiness runs from 0.0 to 5.0 in steps of 0.1, 1.0 by default. It is
used only by Sticky: with Capture or Free the slider is greyed and says
Needs Sticky, and keeps its value for when Sticky is chosen again. At 1.0 a
push of about 300 points past an edge within half a second lets the pointer
go, and so does arriving at the edge at about 6250 points a second, 625
points in the last tenth of a second. An ordinary fast move toward the
edge during play stays held; a deliberate shove leaves. Both distances
grow with the level: 2.0 needs twice as much, 5.0 five times as much, and
at 0.0 the edge never holds, as with Free.

On macOS, Windows and X11 a sticky edge leaves the pointer free to move.
When it crosses an edge the game puts it back on that edge's last pixel and
reads how far past the edge the desktop pointer had gone. That distance adds
up while the push continues, and once it reaches the push the game stops
putting the pointer back until it has come in from the edge. Resting on the
edge adds nothing, so the view keeps scrolling.

On Wayland a program cannot place the pointer, so the game confines it to
the window while a sticky edge should hold. The flick is the approach: the
movement toward an edge over the last tenth of a second, including the
movement before the pointer reached the edge. A slow arrival stays held and
the view scrolls.

## Panning

A middle click on the battlefield starts a pan. A small mark stays where
you clicked, and the view scrolls from that mark towards the pointer. Close
to the mark the view stays still. Further away it speeds up, up to the
game's Scroll speed, and the pointer shows the way. Another mouse button, a
turn of the wheel, any key or leaving the window stops the pan, and that
press or key does nothing else. A middle press that moves before you
release it pans while you hold it and stops when you let go.

It does not start while the allied whiteboard is in use, in the menus, or
on the panels. Settings > Controls, **Middle-click pans**, turns it off.
