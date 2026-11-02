"""Parse a Fusion .setting / .comp file with lupa and print the tool graph.

Usage: python3 settingparse.py FILE [--inputs]
Each `Name = Type { ... }` becomes a table with `__type = "Type"`; `ordered()` is a no-op.
"""
import sys
import lupa

PRELUDE = r"""
local mt = {}
local function tagger(name)
    return function(t)
        if type(t) ~= "table" then t = {} end
        rawset(t, "__type", name)
        return t
    end
end
setmetatable(_G, {__index = function(_, k)
    if k == "ordered" then return function() return function(t) return t end end end
    return tagger(k)
end})
"""


def load(path):
    L = lupa.LuaRuntime(unpack_returned_tuples=True)
    L.execute(PRELUDE)
    src = open(path, encoding="utf-8").read()
    return L.execute("return " + src), L


def py(obj):
    """Recursively convert a Lua table to Python dict/list."""
    if lupa.lua_type(obj) == "table":
        keys = list(obj.keys())
        if keys and all(isinstance(k, int) for k in keys):
            return [py(obj[k]) for k in sorted(keys)]
        return {k: py(obj[k]) for k in keys}
    return obj


def tools_of(root):
    """Return {name: dict} for the tools of a macro setting (the MacroOperator's Tools) or a plain comp."""
    r = py(root)
    tools = r.get("Tools", r)
    # a macro: single entry of type MacroOperator/GroupOperator holding Tools
    for name, t in tools.items():
        if isinstance(t, dict) and t.get("__type") in ("MacroOperator", "GroupOperator") and "Tools" in t:
            return name, t, t["Tools"]
    return None, None, tools


def links(tool):
    out = []
    for iname, iv in (tool.get("Inputs") or {}).items():
        if isinstance(iv, dict) and iv.get("SourceOp"):
            out.append((iname, iv["SourceOp"], iv.get("Source")))
    return out


if __name__ == "__main__":
    path = sys.argv[1]
    root, L = load(path)
    mname, macro, tools = tools_of(root)
    print("macro:", mname, "tools:", len(tools))
    if macro:
        print("published inputs:")
        for iname, iv in (macro.get("Inputs") or {}).items():
            if isinstance(iv, dict):
                print(f"  {iname:16s} {iv.get('__type','?'):16s} -> {iv.get('SourceOp')}.{iv.get('Source')}  [{iv.get('Page')}] {iv.get('Name')!r}")
    print("tools:")
    for name, t in tools.items():
        if not isinstance(t, dict):
            continue
        ls = links(t)
        print(f"  {name:28s} {t.get('__type','?')}")
        for iname, so, s in ls:
            print(f"      {iname} <- {so}.{s}")
        if "--inputs" in sys.argv:
            for iname, iv in (t.get("Inputs") or {}).items():
                if isinstance(iv, dict) and not iv.get("SourceOp"):
                    print(f"      {iname} = {iv.get('Value')!r} {('expr=' + repr(iv.get('Expression'))) if iv.get('Expression') else ''}")
