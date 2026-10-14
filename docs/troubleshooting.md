# When something goes wrong

## Zooming out

Zooming out draws the units as dots when the picture of them would take more
memory than the game can spare. The game keeps running. After you zoom back
in, that extra memory is given back.

If a box says Open Annihilation ran out of memory, close other programs and
start the game again. The box says how full the heap is. ErrorLog.txt, beside
the program, keeps the line "Out of memory! Your hard disk may be full".

A run can limit the memory the units' pictures take by setting
`OA_RENDERED_UNITS_BUDGET` to a number of bytes.
