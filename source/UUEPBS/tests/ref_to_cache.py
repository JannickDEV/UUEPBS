"""Writes a reference skeleton (roku_ref.txt: name parent qx qy qz qw px py pz sx sy sz per line) in the
skeleton cache format test_ground / test_audit read, so Auto Ground runs in CI without a game's cache.
    python3 ref_to_cache.py roku_ref.txt <out.txt>"""
import sys

names, parents, values = [], [], []
with open(sys.argv[1], encoding="utf-8") as f:
    for line in f:
        p = line.split()
        if len(p) < 12:
            continue
        names.append(p[0])
        parents.append(p[1])
        values.extend(p[2:12])
with open(sys.argv[2], "w", encoding="utf-8", newline="\n") as out:
    out.write("skel\tRoku#%d\n" % len(names))
    out.write("names\t" + "\t".join(names) + "\n")
    out.write("parents\t" + "\t".join(parents) + "\n")
    out.write("ref\t" + " ".join(values) + "\n")
