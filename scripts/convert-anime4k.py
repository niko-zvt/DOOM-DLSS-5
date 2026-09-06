# Copyright (C) 2026 Nikolai Zhivotenko. GPLv2; see LICENSE.TXT.
# Converts official MIT Anime4K mpv GLSL hooks to HLSL compute shaders.
import os
import re
import sys
import urllib.request

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
GLSL_DIR = os.path.join(ROOT, "third_party", "anime4k", "glsl")
OUT_HLSL = os.path.join(ROOT, "win32", "shaders", "anime4k", "mode_c_fast.hlsl")
BASE = "https://raw.githubusercontent.com/bloc97/Anime4K/master/glsl"

FILES = [
    ("Upscale+Denoise/Anime4K_Upscale_Denoise_CNN_x2_M.glsl", "denoise_m.glsl"),
    ("Upscale/Anime4K_Upscale_CNN_x2_S.glsl", "upscale_s.glsl"),
]


def fetch():
    os.makedirs(GLSL_DIR, exist_ok=True)
    for rel, name in FILES:
        dest = os.path.join(GLSL_DIR, name)
        if os.path.exists(dest) and os.path.getsize(dest) > 100:
            continue
        url = BASE + "/" + rel.replace("+", "%2B")
        print("download", url)
        urllib.request.urlretrieve(url, dest)


def split_hooks(text):
    parts = re.split(r"(?=//!DESC )", text)
    hooks = []
    for p in parts:
        if "//!DESC" not in p or "vec4 hook()" not in p:
            continue
        desc = re.search(r"//!DESC ([^\n]+)", p).group(1).strip()
        binds = re.findall(r"//!BIND (\S+)", p)
        save = re.search(r"//!SAVE (\S+)", p)
        width = re.search(r"//!WIDTH (.+)", p)
        comps = re.search(r"//!COMPONENTS (\d+)", p)
        body = re.search(r"vec4 hook\(\)\s*\{(.*)\}\s*$", p, re.S)
        defs = []
        for line in p.splitlines():
            if line.startswith("#define "):
                defs.append(line[8:].strip())
        hooks.append({
            "desc": desc,
            "binds": binds,
            "save": save.group(1) if save else "",
            "width": width.group(1).strip() if width else "",
            "comps": int(comps.group(1)) if comps else 4,
            "body": body.group(1) if body else "",
            "defs": defs,
            "raw": p,
        })
    return hooks


def parse_define_fn(defs):
    """go_0(x_off, y_off) -> (relu_sign, tex_name) or g_N -> (relu_sign, tex_name)"""
    go = {}
    g = {}
    for d in defs:
        m = re.match(
            r"(go_\d)\(x_off, y_off\)\s+\((.+)\)", d)
        if m:
            expr = m.group(2)
            relu = 0
            if "max(-(" in expr.replace(" ", ""):
                relu = -1
            elif "max((" in expr.replace(" ", ""):
                relu = 1
            tm = re.search(r"(\w+)_texOff", expr)
            go[m.group(1)] = (relu, tm.group(1) if tm else "MAIN")
            continue
        m = re.match(r"(g_\d+)\s+\((.+)\)", d)
        if m:
            expr = m.group(2)
            relu = -1 if "max(-(" in expr.replace(" ", "") else 1
            tm = re.search(r"(\w+)_tex\(", expr)
            g[m.group(1)] = (relu, tm.group(1) if tm else "MAIN")
    return go, g


def mat4_mul(nums, vec):
    c0 = ", ".join(nums[0:4])
    c1 = ", ".join(nums[4:8])
    c2 = ", ".join(nums[8:12])
    c3 = ", ".join(nums[12:16])
    return (
        f"Mul4(float4({c0}), float4({c1}), float4({c2}), float4({c3}), {vec})"
    )


def tex_index(binds, name):
    if name == "HOOKED":
        name = binds[0] if binds else "MAIN"
    try:
        return binds.index(name)
    except ValueError:
        return 0


def convert_conv(hook, entry, binds_override=None):
    binds = binds_override or hook["binds"]
    go, g = parse_define_fn(hook["defs"])
    body = hook["body"]

    def replace_go(m):
        fn = m.group(1)
        offs = m.group(2)
        xy = [x.strip() for x in offs.split(",")]
        ox = xy[0].replace(".0", "")
        oy = xy[1].replace(".0", "")
        relu, tex = go.get(fn, (0, binds[0]))
        idx = tex_index(binds, tex)
        sample = f"TexOff(Tex{idx}, pos, {ox}, {oy}, inSz)"
        if relu > 0:
            return f"Relu({sample})"
        if relu < 0:
            return f"ReluNeg({sample})"
        return sample

    body = re.sub(r"(go_\d)\(([^)]+)\)", replace_go, body)

    def replace_g(m):
        fn = m.group(1)
        relu, tex = g.get(fn, (1, binds[0]))
        idx = tex_index(binds, tex)
        sample = f"Tex{idx}[pos]"
        if relu < 0:
            return f"ReluNeg({sample})"
        return f"Relu({sample})"

    body = re.sub(r"\b(g_\d+)\b", replace_g, body)

    def replace_mat(m):
        nums = [n.strip() for n in m.group(1).split(",")]
        vec = m.group(2).strip()
        if len(nums) != 16:
            raise SystemExit(f"mat4 expected 16, got {len(nums)} in {entry}")
        return mat4_mul(nums, vec)

    body = re.sub(
        r"mat4\(([^)]+)\)\s*\*\s*([^;+\n]+)",
        replace_mat,
        body,
    )
    body = body.replace("vec4", "float4")
    body = re.sub(r"\bresult\b", "acc", body)
    body = re.sub(r"\s*return acc;\s*", "\n", body)
    # leftover GLSL
    if "mat4" in body or "texOff" in body or "vec2" in body:
        raise SystemExit(f"unconverted GLSL in {entry}:\n{body[:400]}")

    ntex = min(max(len(binds), 1), 8)
    lines = [
        f"[numthreads(8, 8, 1)]",
        f"void {entry}(uint3 id : SV_DispatchThreadID)",
        "{",
        "    if (id.x >= OutWidth || id.y >= OutHeight) return;",
        "    int2 pos = int2(id.xy);",
        "    int2 inSz = int2(InWidth, InHeight);",
        body.rstrip(),
        "    OutTex[pos] = acc;",
        "}",
        "",
    ]
    return "\n".join(lines), ntex


HEADER = r'''// MIT License
// Copyright (c) 2019-2021 bloc97
// Anime4K v3.2/v4 Mode C Fast (Upscale_Denoise_CNN_x2_M + Upscale_CNN_x2_S
// + Clamp_Highlights). Ported from official mpv GLSL to HLSL CS.
// https://github.com/bloc97/Anime4K
//
// Permission is hereby granted, free of charge, to any person obtaining a copy
// of this software and associated documentation files (the "Software"), to deal
// in the Software without restriction, including without limitation the rights
// to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
// copies of the Software, and to permit persons to whom the Software is
// furnished to do so, subject to the following conditions:
// The above copyright notice and this permission notice shall be included in
// all copies or substantial portions of the Software.

cbuffer A4KParams : register(b0)
{
    uint InWidth;
    uint InHeight;
    uint OutWidth;
    uint OutHeight;
};

Texture2D<float4> Tex0 : register(t0);
Texture2D<float4> Tex1 : register(t1);
Texture2D<float4> Tex2 : register(t2);
Texture2D<float4> Tex3 : register(t3);
Texture2D<float4> Tex4 : register(t4);
Texture2D<float4> Tex5 : register(t5);
Texture2D<float4> Tex6 : register(t6);
Texture2D<float4> Tex7 : register(t7);
RWTexture2D<float4> OutTex : register(u0);
SamplerState LinClamp : register(s0);

float4 Mul4(float4 c0, float4 c1, float4 c2, float4 c3, float4 v)
{
    return c0 * v.x + c1 * v.y + c2 * v.z + c3 * v.w;
}

float4 Relu(float4 v) { return max(v, 0.0); }
float4 ReluNeg(float4 v) { return max(-v, 0.0); }

float4 TexOff(Texture2D<float4> tex, int2 pos, int ox, int oy, int2 sz)
{
    int2 p = clamp(pos + int2(ox, oy), int2(0, 0), sz - 1);
    return tex[p];
}

float Luma(float4 rgba)
{
    return dot(rgba, float4(0.299, 0.587, 0.114, 0.0));
}

'''

CLAMP = r'''
[numthreads(8, 8, 1)]
void CS_ClampH(uint3 id : SV_DispatchThreadID)
{
    if (id.x >= OutWidth || id.y >= OutHeight) return;
    int2 pos = int2(id.xy);
    int2 sz = int2(InWidth, InHeight);
    float gmax = 0.0;
    int i;
    for (i = 0; i < 5; i++)
        gmax = max(gmax, Luma(TexOff(Tex0, pos, i - 2, 0, sz)));
    OutTex[pos] = float4(gmax, 0, 0, 0);
}

[numthreads(8, 8, 1)]
void CS_ClampV(uint3 id : SV_DispatchThreadID)
{
    if (id.x >= OutWidth || id.y >= OutHeight) return;
    int2 pos = int2(id.xy);
    int2 sz = int2(InWidth, InHeight);
    float gmax = 0.0;
    int i;
    for (i = 0; i < 5; i++)
        gmax = max(gmax, TexOff(Tex0, pos, 0, i - 2, sz).x);
    OutTex[pos] = float4(gmax, 0, 0, 0);
}

[numthreads(8, 8, 1)]
void CS_D2S(uint3 id : SV_DispatchThreadID)
{
    if (id.x >= OutWidth || id.y >= OutHeight) return;
    int2 pos = int2(id.xy);
    int2 parent = int2((int)id.x / 2, (int)id.y / 2);
    int2 lastSz = int2(InWidth, InHeight);
    parent = clamp(parent, int2(0, 0), lastSz - 1);
    float4 last = Tex0[parent];
    int ch = ((int)id.y & 1) * 2 + ((int)id.x & 1);
    float c0 = last[ch];
    float2 uv = (float2(id.xy) + 0.5) / float2(OutWidth, OutHeight);
    float4 mainc = Tex1.SampleLevel(LinClamp, uv, 0);
    OutTex[pos] = float4(c0, c0, c0, 1.0) + mainc;
}

[numthreads(8, 8, 1)]
void CS_ClampFinal(uint3 id : SV_DispatchThreadID)
{
    if (id.x >= OutWidth || id.y >= OutHeight) return;
    int2 pos = int2(id.xy);
    float4 rgb = Tex0[pos];
    float2 uv = (float2(id.xy) + 0.5) / float2(OutWidth, OutHeight);
    float cap = Tex1.SampleLevel(LinClamp, uv, 0).x;
    float cur = Luma(rgb);
    float nl = min(cur, cap);
    rgb -= (cur - nl);
    OutTex[pos] = float4(rgb.rgb, 1.0);
}
'''


def main():
    fetch()
    denoise = split_hooks(open(os.path.join(GLSL_DIR, "denoise_m.glsl"), encoding="utf-8").read())
    upscale = split_hooks(open(os.path.join(GLSL_DIR, "upscale_s.glsl"), encoding="utf-8").read())
    chunks = [HEADER, CLAMP]
    entries = [
        ("CS_ClampH", 1, 0),
        ("CS_ClampV", 1, 0),
        ("CS_D2S", 2, 1),
        ("CS_ClampFinal", 2, 1),
    ]

    di = 0
    for h in denoise:
        if "Depth-to-Space" in h["desc"]:
            continue
        if "1x1x56" in h["desc"] or "4x1x1x56" in h["desc"]:
            name = "CS_Denoise1x1"
        else:
            name = f"CS_Denoise{di}"
            di += 1
        code, ntex = convert_conv(h, name)
        chunks.append(f"/* {h['desc']} */\n")
        chunks.append(code)
        entries.append((name, ntex, 0))

    si = 0
    for h in upscale:
        if "Depth-to-Space" in h["desc"]:
            continue
        name = f"CS_Upscale{si}"
        si += 1
        code, ntex = convert_conv(h, name)
        chunks.append(f"/* {h['desc']} */\n")
        chunks.append(code)
        entries.append((name, ntex, 0))

    os.makedirs(os.path.dirname(OUT_HLSL), exist_ok=True)
    src = "".join(chunks)
    with open(OUT_HLSL, "w", encoding="utf-8", newline="\n") as f:
        f.write(src)
    inc = os.path.join(os.path.dirname(OUT_HLSL), "mode_c_fast.inc.h")
    with open(inc, "w", encoding="utf-8", newline="\n") as f:
        f.write("/* Generated. Do not edit. Anime4K MIT, bloc97. */\n")
        f.write("static const char g_a4k_hlsl[] =\n")
        for line in src.splitlines(True):
            esc = (line.replace("\\", "\\\\")
                       .replace('"', '\\"')
                       .replace("\n", "\\n"))
            f.write('"' + esc + '"\n')
        f.write(";\n")
    print("wrote", OUT_HLSL, "bytes", os.path.getsize(OUT_HLSL))
    print("wrote", inc)
    print("entries:")
    for e in entries:
        print(" ", e)
    return 0


if __name__ == "__main__":
    sys.exit(main())
