"""Inspect specific events of a RenderDoc capture (run inside qrenderdoc).

Usage:
  set RDC_IN=<capture.rdc>& set RDC_OUT=<dir>& set RDC_EIDS=2326,2302& qrenderdoc.exe --python rdc_inspect.py

For each event: pipeline state summary (shaders, bound textures with names/sizes, depth/blend/raster
state) into <dir>/events.txt, the pixel-shader disassembly into <dir>/ps_<eid>.txt, and PNGs of the
first colour target just before and just after the draw (<dir>/<eid>_before.png / _after.png), plus
the same with only this draw highlighted (overdraw-free "what did it cover" mask is not available in
the API, so compare before/after).
"""
import os
import traceback

import renderdoc as rd


def save_rt(ctl, rid, path):
    ts = rd.TextureSave()
    ts.resourceId = rid
    ts.destType = rd.FileType.PNG
    ts.alpha = rd.AlphaMapping.Discard
    ctl.SaveTexture(ts, path)


def main():
    src, out = os.environ["RDC_IN"], os.environ["RDC_OUT"]
    eids = [int(x) for x in os.environ["RDC_EIDS"].split(",")]
    os.makedirs(out, exist_ok=True)
    cap = rd.OpenCaptureFile()
    cap.OpenFile(src, "", None)
    _, ctl = cap.OpenCapture(rd.ReplayOptions(), None)
    names = {r.resourceId: r.name for r in ctl.GetResources()}
    tex = {t.resourceId: t for t in ctl.GetTextures()}
    targets = ctl.GetDisassemblyTargets(True)
    log = open(os.path.join(out, "events.txt"), "w", encoding="utf-8")

    for eid in eids:
        ctl.SetFrameEvent(eid - 1, True)
        st = ctl.GetPipelineState()
        outs = [o.resource for o in st.GetOutputTargets() if o.resource != rd.ResourceId.Null()]
        if outs:
            save_rt(ctl, outs[0], os.path.join(out, "%d_before.png" % eid))
        ctl.SetFrameEvent(eid, True)
        st = ctl.GetPipelineState()
        d11 = ctl.GetD3D11PipelineState()
        log.write("===== EID %d =====\n" % eid)
        for stage in (rd.ShaderStage.Vertex, rd.ShaderStage.Pixel):
            refl = st.GetShaderReflection(stage)
            sid = st.GetShader(stage)
            log.write("%s shader %s (%s)\n" % (stage, names.get(sid, sid), refl.debugInfo.files[0].filename
                                                if refl and refl.debugInfo.files else ""))
            if stage == rd.ShaderStage.Pixel and refl:
                dis = ctl.DisassembleShader(st.GetGraphicsPipelineObject(), refl, targets[0])
                open(os.path.join(out, "ps_%d.txt" % eid), "w", encoding="utf-8").write(dis)
                log.write("  PS instructions (lines): %d, discard: %s, samples: %d\n" % (
                    dis.count("\n"), "discard" in dis, dis.count("sample")))
            for ro in st.GetReadOnlyResources(stage):
                for b in ro.resources if hasattr(ro, "resources") else [ro.descriptor]:
                    rid = b.resourceId if hasattr(b, "resourceId") else b.resource
                    if rid != rd.ResourceId.Null() and rid in tex:
                        t = tex[rid]
                        log.write("  tex %s %dx%d %s\n" % (names.get(rid, rid), t.width, t.height, t.format.Name()))
        om = d11.outputMerger
        log.write("depth: enable=%s write=%s func=%s | blend0: enable=%s | a2c=%s\n" % (
            om.depthStencilState.depthEnable, om.depthStencilState.depthWrites,
            om.depthStencilState.depthFunction, om.blendState.blends[0].enabled,
            om.blendState.alphaToCoverage))
        log.write("raster: cull=%s fill=%s\n" % (d11.rasterizer.state.cullMode, d11.rasterizer.state.fillMode))
        log.write("outputs: %s\n\n" % ", ".join(str(names.get(o, o)) for o in outs))
        log.flush()
        if outs:
            save_rt(ctl, outs[0], os.path.join(out, "%d_after.png" % eid))
    log.close()
    ctl.Shutdown()
    cap.Shutdown()


try:
    main()
except Exception:
    with open(os.path.join(os.environ.get("RDC_OUT", "."), "error.txt"), "w") as f:
        traceback.print_exc(file=f)
os._exit(0)
