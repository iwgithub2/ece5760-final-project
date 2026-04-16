import numpy as np
import pandas as pd
from sklearn.ensemble import RandomForestClassifier
from itertools import combinations
from scipy.special import gammaln

# --- Configuration ---
NUM_NODES = 5             # Adjust based on your dataset
MAX_PARENTS = 2           # Max K (parents per node)
MAX_NEIGHBORS = 3         # How many top features to consider as potential parents
EQUIVALENT_SAMPLE_SIZE = 1.0 # 'alpha' hyperparameter for BDe

def calculate_bde_binary(dataset, target_col, parent_cols):
    """
    Calculates the BDe score for a target node given parent nodes (Binary data assumption).
    dataset: numpy array (samples x nodes)
    """
    num_samples = dataset.shape[0]
    
    if len(parent_cols) == 0:
        # Base case: No parents
        counts = np.bincount(dataset[:, target_col], minlength=2)
        alpha_ijk = EQUIVALENT_SAMPLE_SIZE / 2.0
        alpha_ij = EQUIVALENT_SAMPLE_SIZE
        
        score = gammaln(alpha_ij) - gammaln(alpha_ij + num_samples)
        for c in counts:
            score += gammaln(alpha_ijk + c) - gammaln(alpha_ijk)
        return score

    # General case: With parents
    # Create a unique hash for each parent state combination
    parent_states = dataset[:, parent_cols]
    state_hashes = np.packbits(parent_states, axis=1).flatten() if len(parent_cols) > 0 else np.zeros(num_samples)
    unique_states = np.unique(state_hashes)
    
    score = 0.0
    # Number of possible parent configurations (2^num_parents for binary)
    q_i = 2 ** len(parent_cols)
    alpha_ij = EQUIVALENT_SAMPLE_SIZE / q_i
    alpha_ijk = alpha_ij / 2.0 # 2 is the number of states for the target variable
    
    for state in unique_states:
        # Find rows where parents match this state
        mask = (state_hashes == state)
        target_vals = dataset[mask, target_col]
        
        counts = np.bincount(target_vals, minlength=2)
        N_ij = np.sum(counts)
        
        score += gammaln(alpha_ij) - gammaln(alpha_ij + N_ij)
        for c in counts:
            score += gammaln(alpha_ijk + c) - gammaln(alpha_ijk)
            
    return score

def main():
    # 1. Create a dummy binary dataset for testing (Replace with your actual data)
    # 1000 samples, 5 variables
    np.random.seed(42)
    data = np.random.randint(0, 2, size=(1000, NUM_NODES))
    # Make variable 1 dependent on variable 0 to test if it finds the edge
    data[:, 1] = data[:, 0] ^ np.random.randint(0, 2, size=1000) 
    
    database = []

    print("Running Pre-computation...")
    for target in range(NUM_NODES):
        # 2. Neighborhood Discovery (Using Random Forest Feature Importances)
        X = np.delete(data, target, axis=1) # All columns except target
        y = data[:, target]
        
        clf = RandomForestClassifier(n_estimators=50, random_state=42)
        clf.fit(X, y)
        
        # Get original indices of the most important features
        original_indices = [i if i < target else i + 1 for i in range(NUM_NODES - 1)]
        importances = clf.feature_importances_
        
        # Sort and pick top MAX_NEIGHBORS
        top_indices = np.argsort(importances)[-MAX_NEIGHBORS:]
        neighborhood = [original_indices[idx] for idx in top_indices]
        
        print(f"Node {target} Neighborhood: {neighborhood}")

        # 3. Generate Candidate Parent Sets and Score
        for k in range(MAX_PARENTS + 1):
            for parent_combo in combinations(neighborhood, k):
                # Create Bitmask
                bitmask = 0
                for p in parent_combo:
                    bitmask |= (1 << p)
                
                # Calculate BDe Score
                score = calculate_bde_binary(data, target, list(parent_combo))
                database.append((target, bitmask, score))

    # 4. Export to CSV (This bridges Python to C)
    df = pd.DataFrame(database, columns=["node_id", "parent_mask", "local_score"])
    df.to_csv("scores.csv", index=False)
    print("Exported to scores.csv")

if __name__ == "__main__":
    main()