"""Per-draw GPU cost of a RenderDoc capture (run inside qrenderdoc, which embeds the renderdoc module).

Usage:
  set RDC_IN=<capture.rdc>& set RDC_OUT=<out.tsv>& qrenderdoc.exe --python rdc_pass_costs.py

Writes one TSV row per draw/dispatch/clear/copy: event id, GPU microseconds, marker path,
action name, index/instance counts, colour targets and depth target (name WxH format).
Also writes <out>.summary.txt: totals per marker and per colour-target set, largest first.
"""
import os
import sys
import traceback

import renderdoc as rd


def main():
    src, out = os.environ["RDC_IN"], os.environ["RDC_OUT"]
    cap = rd.OpenCaptureFile()
    if cap.OpenFile(src, "", None) != rd.ResultCode.Succeeded:
        raise RuntimeError("cannot open " + src)
    status, ctl = cap.OpenCapture(rd.ReplayOptions(), None)
    if status != rd.ResultCode.Succeeded:
        raise RuntimeError("cannot replay: %s" % status)
    sf = ctl.GetStructuredFile()

    tex = {}
    for t in ctl.GetTextures():
        tex[t.resourceId] = "%dx%d %s" % (t.width, t.height, t.format.Name())
    names = {r.resourceId: r.name for r in ctl.GetResources()}

    def target(rid):
        if rid == rd.ResourceId.Null():
            return ""
        return "%s[%s]" % (names.get(rid, str(rid)), tex.get(rid, "?"))

    dur = {}
    for r in ctl.FetchCounters([rd.GPUCounter.EventGPUDuration]):
        dur[r.eventId] = r.value.d * 1e6  # seconds -> microseconds

    rows = []

    def walk(actions, path):
        for a in actions:
            name = a.GetName(sf)
            if a.children:
                walk(a.children, path + [name])
                continue
            us = dur.get(a.eventId, 0.0)
            outs = ";".join(target(o) for o in a.outputs if o != rd.ResourceId.Null())
            rows.append((a.eventId, us, "/".join(path), name, a.numIndices, a.numInstances, outs,
                         target(a.depthOut)))

    walk(ctl.GetRootActions(), [])
    with open(out, "w", encoding="utf-8") as f:
        f.write("eid\tus\tmarker\tname\tindices\tinstances\tcolor\tdepth\n")
        for r in rows:
            f.write("\t".join(str(x) for x in r) + "\n")

    total = sum(r[1] for r in rows)
    by_marker, by_target = {}, {}
    for r in rows:
        by_marker[r[2] or "(none)"] = by_marker.get(r[2] or "(none)", 0) + r[1]
        key = (r[6] or "-") + " | " + (r[7] or "-")
        c, n = by_target.get(key, (0.0, 0))
        by_target[key] = (c + r[1], n + 1)
    with open(out + ".summary.txt", "w", encoding="utf-8") as f:
        f.write("events %d, GPU total %.2f ms\n\n== by marker ==\n" % (len(rows), total / 1000))
        for k, v in sorted(by_marker.items(), key=lambda x: -x[1])[:60]:
            f.write("%8.3f ms  %s\n" % (v / 1000, k))
        f.write("\n== by render targets (color | depth) ==\n")
        for k, (v, n) in sorted(by_target.items(), key=lambda x: -x[1][0])[:80]:
            f.write("%8.3f ms  %5d ev  %s\n" % (v / 1000, n, k))
    ctl.Shutdown()
    cap.Shutdown()


try:
    main()
except Exception:
    with open(os.environ.get("RDC_OUT", "rdc_error") + ".error.txt", "w") as f:
        traceback.print_exc(file=f)
os._exit(0)
