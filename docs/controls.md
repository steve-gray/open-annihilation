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

In a window, while a match's battlefield shows, **Window edge** Sticky (the
default, under Settings > Controls) keeps the pointer on the window's edges.
It rests on the last pixel and the view scrolls, as it does in full screen.
A firm push outward, or arriving at the edge quickly, lets it leave. After
that it is held again only once it has come back in and moved about 24
points away from the edges. Motion along an edge does not let it go. Menus,
the pause screen, a window without the focus and Free leave the pointer
free. Full screen keeps its own hold. Losing the focus, with Alt+Tab,
Command+Tab or a dialog of the system's, lets the pointer go.

On macOS, Windows and X11 the pointer stays free to move. When it crosses
an edge the game puts it back on that edge's last pixel and reads how far
past the edge the desktop pointer had gone. That distance adds up while
the push continues. About 120 points within half a second lets it go, and
the game stops putting it back until the pointer has come in from the edge.
Resting on the edge adds nothing, so the view keeps scrolling.

On Wayland a program cannot place the pointer, so the game confines it to
the window while it should hold. The flick is the approach: the movement
toward an edge over the last tenth of a second, including the movement
before the pointer reached the edge. Arriving at about 2500 points a
second lets it go. A slow arrival stays held and the view scrolls.

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
