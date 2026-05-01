import re
import xenakios


def to_python_constant(name: str) -> str:
    name = name.upper()
    name = name.replace(" ", "_")
    name = re.sub(r"[^A-Z0-9_]", "_", name)  # replace any remaining invalid chars
    name = re.sub(r"_+", "_", name)  # collapse multiple underscores
    name = name.strip("_")
    return name


def gen_param_metadata(justprintmd: bool):
    g = xenakios.ToneGranulator()
    meta = g.get_metadata()
    if justprintmd:
        print(meta)
        return
    with open(
        r"C:\Users\teemu\AppData\Local\Python\pythoncore-3.14-64\Lib\site-packages\xenakios_params.py",
        "w",
    ) as f:
        f.write("# Auto-generated - do not edit manually\n")
        f.write("# Run generate_metadata.py to regenerate\n\n")
        f.write("from enum import IntEnum\n\n")
        f.write("class Param(IntEnum):\n")
        for param in meta["parameters"]:
            cname = to_python_constant(param["name"])
            f.write(f"    {cname} = {param['id']}\n")
        f.write("\nclass ModSources(IntEnum):\n")
        for ms in meta["modsources"]:
            cname = to_python_constant(ms["name"])
            f.write(f"    {cname} = {ms['id']}\n")
        f.write("\nclass ModCurves(IntEnum):\n")
        f.write("    LINEAR = 1\n")
        f.write("    SQUARE = 2\n")
        f.write("    CUBE = 3\n")
        f.write("    POWER16 = 26\n")
        f.write("    STEPS2 = 4\n")
        f.write("    STEPS3 = 5\n")
        f.write("    STEPS4 = 6\n")
        f.write("    STEPS5 = 7\n")
        f.write("    XOR1 = 12\n")
        f.write("    XOR2 = 13\n")


if __name__ == "__main__":
    gen_param_metadata(False)
