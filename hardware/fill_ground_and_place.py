#!/usr/bin/env python3
"""
Optional helper — run INSIDE KiCad's Python (it needs the `pcbnew` module,
which only exists in a KiCad install; it is not a standalone script).

After you have run "Update PCB from Schematic" so the board has footprints,
this script:
  * spreads the footprints out on a coarse grid (so they are not stacked),
  * makes sure a GND zone exists on F.Cu and B.Cu,
  * fills all zones (pours the ground planes onto the real pads).

It does NOT route the board — pcbnew has no autorouter. Route manually, or
export a Specctra .dsn (File > Export > Specctra DSN), autoroute it with
Freerouting, and import the .ses (File > Import > Specctra Session).

Usage (KiCad Scripting Console, with the board open):
    exec(open('/path/to/fill_ground_and_place.py').read())
or from a terminal that has KiCad's python:
    python fill_ground_and_place.py open-lego-camera-cm4.kicad_pcb
"""
import sys
import pcbnew

MM = pcbnew.FromMM


def load_board():
    board = pcbnew.GetBoard()          # works inside the GUI
    if board is None or not board.GetFileName():
        if len(sys.argv) > 1:
            board = pcbnew.LoadBoard(sys.argv[1])
        else:
            raise SystemExit("Open a board first, or pass a .kicad_pcb path.")
    return board


def spread_footprints(board, cols=8, dx=14.0, dy=16.0, x0=8.0, y0=8.0):
    """Coarse auto-placement so parts do not overlap; refine by hand after."""
    fps = sorted(board.GetFootprints(), key=lambda f: f.GetReference())
    for i, fp in enumerate(fps):
        r, c = divmod(i, cols)
        fp.SetPosition(pcbnew.VECTOR2I(MM(x0 + c * dx), MM(y0 + r * dy)))


def ensure_gnd_zones(board):
    """Add full-board GND zones on F.Cu and B.Cu if none exist."""
    gnd = board.FindNet("GND")
    if gnd is None:
        print("No GND net found — run Update PCB from Schematic first.")
        return
    bb = board.GetBoardEdgesBoundingBox()
    if bb.GetWidth() == 0:
        # no Edge.Cuts yet: use a default 100 x 80 mm outline area
        x0, y0, x1, y1 = MM(1), MM(1), MM(99), MM(79)
    else:
        m = MM(0.5)
        x0, y0 = bb.GetLeft() + m, bb.GetTop() + m
        x1, y1 = bb.GetRight() - m, bb.GetBottom() - m
    have = {z.GetLayer() for z in board.Zones()
            if z.GetNetname() == "GND"}
    for layer in (pcbnew.F_Cu, pcbnew.B_Cu):
        if layer in have:
            continue
        z = pcbnew.ZONE(board)
        z.SetLayer(layer)
        z.SetNet(gnd)
        z.SetIsFilled(True)
        outline = z.Outline()
        outline.NewOutline()
        for x, y in ((x0, y0), (x1, y0), (x1, y1), (x0, y1)):
            outline.Append(x, y)
        board.Add(z)
        print("Added GND zone on layer", board.GetLayerName(layer))


def main():
    board = load_board()
    spread_footprints(board)
    ensure_gnd_zones(board)
    pcbnew.ZONE_FILLER(board).Fill(board.Zones())
    pcbnew.Refresh()
    if not pcbnew.GetBoard():        # standalone (loaded from a path)
        pcbnew.SaveBoard(board.GetFileName(), board)
    print("Done: footprints spread and ground planes poured.")


if __name__ == "__main__":
    main()
