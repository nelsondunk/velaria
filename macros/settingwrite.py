"""Serialize the Python form of a Fusion .setting (from settingparse.py) back to Lua-table text."""
import re

IDENT = re.compile(r"^[A-Za-z_][A-Za-z0-9_]*$")


def lua_str(s):
    return '"' + s.replace("\\", "\\\\").replace('"', '\\"').replace("\n", "\\n").replace("\r", "\\r").replace("\t", "\\t") + '"'


def fmt_num(v):
    if isinstance(v, bool):
        return "true" if v else "false"
    if isinstance(v, int):
        return str(v)
    r = repr(float(v))
    return r


def emit(v, indent):
    pad = "\t" * indent
    if isinstance(v, bool):
        return "true" if v else "false"
    if isinstance(v, (int, float)):
        return fmt_num(v)
    if isinstance(v, str):
        return lua_str(v)
    if v is None:
        return "nil"
    if isinstance(v, list):
        inner = ", ".join(emit(x, indent + 1) for x in v)
        return "{ " + inner + " }"
    if isinstance(v, dict):
        typ = v.get("__type")
        items = [(k, x) for k, x in v.items() if k not in ("__type", "__ordered")]
        nums = sorted([(k, x) for k, x in items if isinstance(k, (int, float)) and not isinstance(k, bool)], key=lambda kv: kv[0])
        seq = [int(k) for k, _ in nums] == list(range(1, len(nums) + 1)) and all(float(k).is_integer() for k, _ in nums)
        pos = nums if seq else []
        named = ([] if seq else nums) + [(k, x) for k, x in items if not isinstance(k, (int, float)) or isinstance(k, bool)]
        head = (typ + " " if typ else "") + ("ordered() " if v.get("__ordered") else "") + "{"
        if not items:
            return head + " }"
        # short form for tiny leaf tables
        simple = all(not isinstance(x, (dict, list)) for _, x in items) and len(items) <= 3
        if simple:
            parts = [emit(x, indent + 1) for _, x in pos] + [f"{key_str(k)} = {emit(x, indent + 1)}" for k, x in named]
            return head + " " + ", ".join(parts) + ", }"
        out = [head]
        for _, x in pos:
            out.append(pad + "\t" + emit(x, indent + 1) + ",")
        for k, x in named:
            out.append(pad + "\t" + key_str(k) + " = " + emit(x, indent + 1) + ",")
        out.append(pad + "}")
        return "\n".join(out)
    raise TypeError(type(v))


def key_str(k):
    if isinstance(k, (int, float)):
        return f"[{fmt_num(k)}]"
    if IDENT.match(k):
        return k
    return "[" + lua_str(k) + "]"


def write_setting(root, path):
    open(path, "w", encoding="utf-8").write(emit(root, 0) + "\n")
