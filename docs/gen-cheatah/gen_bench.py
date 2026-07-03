# gen_bench.py — the SAME task as gen_bench.purr, in Python (xml.etree, the C-accelerated
# stdlib XML parser). Parse every Doxygen namespace XML + build each module's member HTML.
import os, time
import xml.etree.ElementTree as ET

def child_text(el, tag):
    c = el.find(tag)
    return "".join(c.itertext()) if c is not None else ""

def esc(s):
    return s.replace("&", "&amp;").replace("<", "&lt;").replace(">", "&gt;")

def render(src):
    root = ET.fromstring(src)
    out = ""
    for cd in root.iter("compounddef"):
        out += "<h1><code>" + esc(child_text(cd, "compoundname")) + "</code></h1>\n"
        for md in cd.iter("memberdef"):
            brief = esc(child_text(md, "briefdescription"))
            out += "<section class=\"member\"><span class=\"badge\">"
            out += (md.get("kind", "") or "") + "</span> <code>"
            out += esc(child_text(md, "name")) + esc(child_text(md, "argsstring"))
            out += "</code><p>" + brief + "</p></section>\n"
    return out

srcs = []
for f in sorted(os.listdir("docs/xml")):
    if f.startswith("namespacecheatah_") and f.endswith(".xml"):
        srcs.append(open(os.path.join("docs/xml", f), encoding="utf-8").read())

best = 1e9
for _ in range(25):
    t0 = time.perf_counter()
    b = 0
    for s in srcs:
        b += len(render(s))
    best = min(best, time.perf_counter() - t0)
print(f"python   files={len(srcs)} out_bytes={b} best_ms={best*1000:.3f}")
