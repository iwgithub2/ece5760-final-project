import math

# Q16.16 configuration
FRACTIONAL_BITS = 20
LUT_DEPTH = 1024
MAX_X = 16.0 # At x=16, log(1+e^-16) is practically 0

# Step size based on mapping 0-16 into 1024 address bins
step_size = MAX_X / LUT_DEPTH

with open("log_lut.hex", "w") as f:
    for i in range(LUT_DEPTH):
        x = i * step_size
        # Calculate log(1 + e^-x)
        val = math.log(1.0 + math.exp(-x))
        
        # Convert to Q16.16 fixed point integer
        val_fixed = int(round(val * (1 << FRACTIONAL_BITS)))
        
        # Write as 8-character hex (32 bits)
        f.write(f"{val_fixed:08X}\n")
        
print("Generated log_lut.hex successfully!")