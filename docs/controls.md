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
default, under Settings > Controls) holds the pointer on the window's edges.
It rests on the last pixel and the view scrolls, as it does in full screen.
A firm push outward, or a flick onto the edge, lets it leave. After that it
is held again only once it has come back in and moved about 24 points away
from the edges. Motion along an edge does not let it go. Menus, the pause
screen, a window without the focus and Free leave the pointer free. Full
screen keeps its own hold.

Through SDL 3, macOS, Windows, X11 and Wayland do not report movement while
the pointer is held at the edge. A flick is how it leaves on each of them:
arrive at the edge fast, or pull back in and flick out. A slow rest stays
held and the view scrolls. Where a window system does report movement past
the edge, a push of that movement lets the pointer go too.
