"""
Converts row_ptr.txt, col_idx.txt, val.txt (from create_csr) into Intel
.mif files that Quartus can use to initialize on-chip ROM/RAM at compile
time -- this is what actually gets the CSR data onto real hardware,
since a real FPGA has no filesystem to read .txt files from at runtime.

Usage:
    python3 make_mif.py output_16x16/ mif_output/ --frac-bits 16
"""

import argparse
import os


def to_fixed_point_int(x, frac_bits):
    """Convert a float to a signed Q(int).(frac) fixed-point integer,
    then wrap to unsigned 32-bit two's complement for MIF output."""
    val = int(round(x * (1 << frac_bits)))
    return val & 0xFFFFFFFF


def write_mif(path, values, width, depth, radix="HEX"):
    with open(path, "w") as f:
        f.write(f"WIDTH={width};\n")
        f.write(f"DEPTH={depth};\n")
        f.write("ADDRESS_RADIX=UNS;\n")
        f.write(f"DATA_RADIX={radix};\n")
        f.write("CONTENT BEGIN\n")
        for addr, v in enumerate(values):
            if radix == "HEX":
                hex_digits = width // 4
                f.write(f"    {addr} : {v:0{hex_digits}X};\n")
            else:
                f.write(f"    {addr} : {v};\n")
        # Quartus requires every address up to DEPTH-1 to be defined;
        # pad any remaining addresses with zero.
        for addr in range(len(values), depth):
            pad = "0" * (width // 4) if radix == "HEX" else "0"
            f.write(f"    {addr} : {pad};\n")
        f.write("END;\n")
    print(f"Wrote {path} (width={width}, depth={depth})")


def read_ints(path):
    with open(path) as f:
        return [int(line.strip()) for line in f if line.strip()]


def read_floats(path):
    with open(path) as f:
        return [float(line.strip()) for line in f if line.strip()]


if __name__ == "__main__":
    parser = argparse.ArgumentParser()
    parser.add_argument("csr_dir", help="Folder with row_ptr.txt, col_idx.txt, val.txt")
    parser.add_argument("out_dir", help="Where to write the .mif files")
    parser.add_argument("--frac-bits", type=int, default=16,
                         help="Fractional bits for Q(int).(frac) fixed-point (val.txt only)")
    parser.add_argument("--max-nodes", type=int, default=256,
                         help="Must match MAX_NODES in your Verilog -- sets row_ptr ROM depth")
    parser.add_argument("--max-edges", type=int, default=2048,
                         help="Must match MAX_EDGES in your Verilog -- sets col_idx/val ROM depth")
    args = parser.parse_args()

    os.makedirs(args.out_dir, exist_ok=True)

    row_ptr = read_ints(os.path.join(args.csr_dir, "row_ptr.txt"))
    col_idx = read_ints(os.path.join(args.csr_dir, "col_idx.txt"))
    val     = read_floats(os.path.join(args.csr_dir, "val.txt"))

    # row_ptr and col_idx are plain integers -- 32-bit width, no scaling
    write_mif(os.path.join(args.out_dir, "row_ptr.mif"),
              row_ptr, width=32, depth=args.max_nodes + 1)

    write_mif(os.path.join(args.out_dir, "col_idx.mif"),
              col_idx, width=32, depth=args.max_edges)

    # val is fixed-point (Q16.16 by default) -- convert each float first
    val_fixed = [to_fixed_point_int(v, args.frac_bits) for v in val]
    write_mif(os.path.join(args.out_dir, "val.mif"),
              val_fixed, width=32, depth=args.max_edges)

    print(f"\nDone. {len(row_ptr)} row_ptr entries, {len(col_idx)} edges written.")
    print("Reference these in Verilog like:")
    print('  (* ram_init_file = "row_ptr.mif" *) reg [31:0] row_ptr [0:MAX_NODES];')