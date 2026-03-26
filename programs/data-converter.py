import os
from pathlib import Path

import pandas as pd
from pgmpy.readwrite import BIFReader

def export_all_bifs(
    bif_dir: str,
    out_dir: str = "generated_data",
    n_samples: int = 5000,
    seed: int = 0,
):
    bif_dir = Path(bif_dir)
    out_dir = Path(out_dir)
    out_dir.mkdir(parents=True, exist_ok=True)

    bif_files = list(bif_dir.glob("*.bif"))

    if not bif_files:
        print("No .bif files found.")
        return

    print(f"Found {len(bif_files)} BIF files\n")

    for bif_path in bif_files:
        name = bif_path.stem
        print(f"Processing: {name}")

        try:
            reader = BIFReader(str(bif_path))
            model = reader.get_model()

            # Generate samples
            df = model.simulate(n_samples=n_samples, seed=seed)

            # Encode to integers
            df_encoded = df.copy()
            for col in df.columns:
                df_encoded[col] = df[col].astype("category").cat.codes

            # Save samples
            samples_path = out_dir / f"{name}_samples.csv"
            df_encoded.to_csv(samples_path, index=False)

            # Save edges
            edges_df = pd.DataFrame(model.edges(), columns=["src", "dst"])
            edges_path = out_dir / f"{name}_edges.csv"
            edges_df.to_csv(edges_path, index=False)

            print(f"  Saved: {samples_path}")
            print(f"  Saved: {edges_path}")

        except Exception as e:
            print(f"  Failed: {name} ({e})")

    print("\nDone.")


if __name__ == "__main__":
    export_all_bifs(
        bif_dir="../raw-datasets",
        out_dir="../cleaned-datasets",
        n_samples=10000,
        seed=1,
    )