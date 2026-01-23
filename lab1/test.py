#!/usr/bin/env python3

import subprocess

def to_fixed_point(value, total_bits, frac_bits):
    scaled_value = round(value * (2**frac_bits))

    min_val = -2**(total_bits - 1)
    max_val = 2**(total_bits - 1) - 1

    if not (min_val <= scaled_value <= max_val):
        return f"Error: Value {value} out of range for {total_bits}-bit representation."

    if scaled_value < 0:
        scaled_value = (1 << total_bits) + scaled_value

    binary_str = format(scaled_value, f'0{total_bits}b')
    
    dot_index = total_bits - frac_bits
    return f"{binary_str[:dot_index]}.{binary_str[dot_index:]}"

def run_simulation():
    compile_cmd = [
        "iverilog",
         "-o",
         "-g2012",
        # f"build/{sim_name}.vvp",
        # f"-DX_VAL=27'sd{x_init}", # 'sd' treats it as a signed decimal
        # f"-DY_VAL=27'sd{y_init}",
        "testbench.v"
    ]
    
    print(f"Compiling...")
    subprocess.run(compile_cmd, check=True)

    # 2. Execute the simulation
    print(f"Running...")
    subprocess.run(["vvp", f"build/{sim_name}.vvp"], check=True)


def main():
    print("Hello World")
    run_simulation()


if __name__ == "__main__":
    main()