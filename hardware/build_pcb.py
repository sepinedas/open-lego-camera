#!/usr/bin/env python3
"""
build_pcb.py — one-shot PCB builder for the Open Lego Camera CM4 carrier.

Targets KiCad 10's `pcbnew` Python API (also works on 8/9 — the calls used
here are shared, and the version-sensitive ones are guarded). It must run
inside a KiCad install, because it needs the real footprints from your
libraries; `pcbnew` does not exist as a standalone pip package.

WHAT IT DOES
------------
Starting from a board that already has the footprints imported
(run **Tools > Update PCB from Schematic** / F8 once first), it:
  1. places every footprint into a sensible per-subsystem cluster,
  2. draws the 100 x 80 mm Edge.Cuts board outline (if missing),
  3. creates GND ground-plane zones on F.Cu and B.Cu (if missing),
  4. fills all zones (pours the planes onto the pads),
  5. exports a Specctra `.dsn` for autorouting,
  6. if a router is available, autoroutes and imports the `.ses` back,
  7. re-fills the zones and saves.

ROUTING
-------
`pcbnew` has no built-in autorouter, so "all routes" is done by Freerouting
(https://github.com/freerouting/freerouting). Point the env var
FREEROUTING_JAR at freerouting.jar (needs Java) and this script will call it
headless. Without it, the script still exports the `.dsn`; route it in
Freerouting, then run **File > Import > Specctra Session** on the `.ses`.

HOW TO RUN
----------
GUI (recommended): open the .kicad_pcb, then in the Scripting Console:
    exec(open('/full/path/hardware/build_pcb.py').read())

Headless with kicad-cli's python (KiCad 10):
    kicad-cli python hardware/build_pcb.py hardware/open-lego-camera-cm4.kicad_pcb
    (or:  python hardware/build_pcb.py <board.kicad_pcb>   using KiCad's python)
"""
import os
import sys
import subprocess

import pcbnew

MM = pcbnew.FromMM

# --- subsystem clusters: center (mm) on the 100 x 80 board -----------------
CLUSTERS = {
    "power": (22.0, 34.0),
    "cm4":   (54.0, 38.0),
    "cam":   (86.0, 24.0),
    "audio": (86.0, 62.0),
    "io":    (50.0, 71.0),
    "misc":  (12.0, 72.0),
}

# refdes -> cluster (prefixes matched longest-first; explicit refs win)
POWER = {"U1", "U2", "U3", "U4", "J1", "J2", "L1", "D1", "D2", "D3",
         "C1", "C2", "C3", "C4", "C5",
         "R1", "R2", "R3", "R4", "R19", "R20", "R21", "R23"}
CM4 = {"U5", "J3", "J9", "C6", "C7", "C11", "R5", "R17", "R18",
       "TP1", "TP2", "TP3"}
CAM = {"J4", "J5", "J6", "R6", "R7"}
AUDIO = {"U6", "U7", "U8", "J7", "C8", "C9", "C10", "R8", "R22"}
IO = {"J8", "R9", "R10", "R11", "R12", "R13", "R14", "R15", "R16"}


def cluster_of(ref):
    for name, s in (("power", POWER), ("cm4", CM4), ("cam", CAM),
                    ("audio", AUDIO), ("io", IO)):
        if ref in s:
            return name
    return "misc"


def place_footprints(board):
    """Grid-place footprints inside their subsystem cluster."""
    buckets = {}
    for fp in board.GetFootprints():
        buckets.setdefault(cluster_of(fp.GetReference()), []).append(fp)
    for name, fps in buckets.items():
        cx, cy = CLUSTERS[name]
        fps.sort(key=lambda f: f.GetReference())
        n = len(fps)
        cols = max(1, int(n ** 0.5 + 0.999))
        dx = dy = 7.0
        x0 = cx - (cols - 1) * dx / 2.0
        y0 = cy - ((n + cols - 1) // cols - 1) * dy / 2.0
        for i, fp in enumerate(fps):
            r, c = divmod(i, cols)
            fp.SetPosition(pcbnew.VECTOR2I(MM(x0 + c * dx), MM(y0 + r * dy)))
    print("Placed footprints into %d clusters." % len(buckets))


def ensure_outline(board, w=100.0, h=80.0):
    bb = board.GetBoardEdgesBoundingBox()
    if bb.GetWidth() > 0:
        return
    pts = [(0, 0), (w, 0), (w, h), (0, h), (0, 0)]
    for (x1, y1), (x2, y2) in zip(pts, pts[1:]):
        seg = pcbnew.PCB_SHAPE(board)
        seg.SetShape(pcbnew.SHAPE_T_SEGMENT)
        seg.SetStart(pcbnew.VECTOR2I(MM(x1), MM(y1)))
        seg.SetEnd(pcbnew.VECTOR2I(MM(x2), MM(y2)))
        seg.SetLayer(pcbnew.Edge_Cuts)
        seg.SetWidth(MM(0.15))
        board.Add(seg)
    print("Drew board outline %g x %g mm." % (w, h))


def ensure_gnd_zones(board):
    gnd = board.FindNet("GND")
    if gnd is None:
        print("!! No GND net — run Update PCB from Schematic first.")
        return
    bb = board.GetBoardEdgesBoundingBox()
    m = MM(0.5)
    x0, y0 = bb.GetLeft() + m, bb.GetTop() + m
    x1, y1 = bb.GetRight() - m, bb.GetBottom() - m
    have = {z.GetLayer() for z in board.Zones() if z.GetNetname() == "GND"}
    for layer in (pcbnew.F_Cu, pcbnew.B_Cu):
        if layer in have:
            continue
        z = pcbnew.ZONE(board)
        z.SetLayer(layer)
        z.SetNet(gnd)
        z.SetIsFilled(True)
        try:
            z.SetZoneName("GND_plane")
        except Exception:
            pass
        ol = z.Outline()
        ol.NewOutline()
        for x, y in ((x0, y0), (x1, y0), (x1, y1), (x0, y1)):
            ol.Append(int(x), int(y))
        board.Add(z)
        print("Added GND zone on", board.GetLayerName(layer))


def fill_zones(board):
    pcbnew.ZONE_FILLER(board).Fill(board.Zones())
    print("Filled all zones (ground planes poured).")


def route(board, board_path):
    """Export DSN, autoroute with Freerouting if available, import SES."""
    dsn = os.path.splitext(board_path)[0] + ".dsn"
    ses = os.path.splitext(board_path)[0] + ".ses"
    try:
        ok = pcbnew.ExportSpecctraDSN(dsn)  # GUI/global helper
        print("Exported Specctra DSN ->", dsn)
    except Exception as e:
        print("Could not auto-export DSN (%s)." % e)
        print("   Do it in the GUI: File > Export > Specctra DSN, then autoroute.")
        return
    jar = os.environ.get("FREEROUTING_JAR")
    if not jar or not os.path.exists(jar):
        print("Set FREEROUTING_JAR to freerouting.jar to autoroute automatically.")
        print("   Meanwhile: route %s in Freerouting, then File > Import > Specctra Session." % dsn)
        return
    try:
        # Freerouting CLI (v1.x): -de input.dsn -do output.ses ; newer builds
        # accept --input/--output. Try the classic flags first.
        subprocess.run(["java", "-jar", jar, "-de", dsn, "-do", ses],
                       check=True)
    except Exception as e:
        print("Freerouting run failed (%s); route the DSN manually." % e)
        return
    if os.path.exists(ses):
        try:
            pcbnew.ImportSpecctraSES(ses)
            print("Imported routed session ->", ses)
        except Exception as e:
            print("Import the .ses via File > Import > Specctra Session (%s)." % e)


def get_board():
    b = pcbnew.GetBoard()
    if b is not None and b.GetFileName():
        return b, b.GetFileName()
    if len(sys.argv) > 1:
        return pcbnew.LoadBoard(sys.argv[1]), sys.argv[1]
    raise SystemExit("Open a board in KiCad, or pass a .kicad_pcb path.")


def main():
    board, path = get_board()
    place_footprints(board)
    ensure_outline(board)
    ensure_gnd_zones(board)
    fill_zones(board)
    route(board, path)
    fill_zones(board)  # re-pour after routing
    try:
        pcbnew.Refresh()
    except Exception:
        pass
    # Save when running headless; in the GUI let the user review then Ctrl+S.
    if pcbnew.GetBoard() is None or not pcbnew.GetBoard().GetFileName():
        pcbnew.SaveBoard(path, board)
        print("Saved", path)
    print("Done. Run DRC (Inspect > Design Rules Checker) before fabrication.")


if __name__ == "__main__":
    main()
