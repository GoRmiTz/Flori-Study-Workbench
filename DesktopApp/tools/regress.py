import sys, os, subprocess
from PIL import Image, ImageChops

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
EXE = os.path.join(ROOT, "build", "Flori.exe")
SHOTS = os.path.join(ROOT, "build", "shots", "regress")
CI = os.path.join(ROOT, "build", "shots", "ci")
os.makedirs(SHOTS, exist_ok=True)

def shot(route, out_path):
    r = subprocess.run([EXE, "--shot", out_path, "--route", route, "--at", "1.5"],
                       cwd=ROOT, capture_output=True, text=True)
    if r.returncode != 0:
        sys.stderr.write("SHOT FAILED rc=%d\n%s\n" % (r.returncode, r.stderr))
        sys.exit(1)
    if not os.path.exists(out_path):
        sys.stderr.write("SHOT MISSING: %s\n" % out_path)
        sys.exit(1)

def compare(a, b):
    ia = Image.open(a).convert("RGB"); ib = Image.open(b).convert("RGB")
    if ia.size != ib.size:
        w = min(ia.size[0], ib.size[0]); h = min(ia.size[1], ib.size[1])
        ia = ia.crop((0, 0, w, h)); ib = ib.crop((0, 0, w, h))
        print("SIZE MISMATCH cropped to %s" % (ia.size,))
    d = ImageChops.difference(ia, ib); g = d.convert("L")
    total = ia.size[0] * ia.size[1]
    # 直方图统计：bin i = 灰阶差值为 i 的像素数（比逐像素遍历快两个数量级，且无弃用告警）
    hist = g.histogram()
    nz = total - hist[0]
    hot = sum(hist[9:])
    pct = 100.0 * nz / total; hotpct = 100.0 * hot / total
    print("%s: %s nonzero=%d (%.4f%%) diff>8=%d (%.4f%%)" %
          (os.path.basename(a), ia.size, nz, pct, hot, hotpct))
    if hot == 0:
        print("VERDICT: ZERO-REGRESSION")
    elif hotpct < 0.5:
        print("VERDICT: OK(<0.5%)")
    else:
        print("VERDICT: REGRESSION")

def main():
    if len(sys.argv) < 2:
        sys.stderr.write("usage: regress.py <route> [base|cmp <basepath>]\n")
        sys.exit(2)
    route = sys.argv[1]
    if len(sys.argv) > 2 and sys.argv[2] == "base":
        out = os.path.join(CI, route + ".png")
        shot(route, out)
        print("baseline -> %s (%d bytes)" % (out, os.path.getsize(out)))
        return
    new = os.path.join(SHOTS, route + "_new.png")
    shot(route, new)
    if len(sys.argv) > 3 and sys.argv[2] == "cmp":
        base = sys.argv[3]
    else:
        base = os.path.join(CI, route + ".png")
    if not os.path.exists(base):
        sys.stderr.write("NO BASELINE: %s (run: regress.py %s base)\n" % (base, route))
        sys.exit(3)
    compare(new, base)

if __name__ == "__main__":
    main()
