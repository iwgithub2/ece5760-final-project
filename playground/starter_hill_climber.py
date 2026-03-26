from __future__ import annotations

import math
import random
from dataclasses import dataclass
from pathlib import Path
from typing import Dict, List, Optional, Set, Tuple

import matplotlib.pyplot as plt
import networkx as nx
import numpy as np
import pandas as pd


# ============================================================
# Utility
# ============================================================

def make_state_counts(column: np.ndarray) -> int:
    return int(column.max()) + 1


def has_cycle(parents: List[Set[int]], num_vars: int) -> bool:
    children = [set() for _ in range(num_vars)]
    indeg = [0] * num_vars

    for child in range(num_vars):
        for p in parents[child]:
            children[p].add(child)
            indeg[child] += 1

    queue = [i for i in range(num_vars) if indeg[i] == 0]
    visited = 0

    while queue:
        node = queue.pop()
        visited += 1
        for c in children[node]:
            indeg[c] -= 1
            if indeg[c] == 0:
                queue.append(c)

    return visited != num_vars


def parent_config_index(row_parent_vals: np.ndarray, parent_cardinalities: List[int]) -> int:
    idx = 0
    for val, card in zip(row_parent_vals, parent_cardinalities):
        idx = idx * card + int(val)
    return idx

# ============================================================
# BN representation
# ============================================================

@dataclass
class BayesianNetwork:
    num_vars: int
    parents: List[Set[int]]

    @classmethod
    def empty(cls, num_vars: int) -> "BayesianNetwork":
        return cls(num_vars=num_vars, parents=[set() for _ in range(num_vars)])

    def copy(self) -> "BayesianNetwork":
        return BayesianNetwork(
            num_vars=self.num_vars,
            parents=[set(p) for p in self.parents]
        )

    def has_edge(self, src: int, dst: int) -> bool:
        return src in self.parents[dst]

    def add_edge(self, src: int, dst: int) -> bool:
        if src == dst or src in self.parents[dst]:
            return False
        self.parents[dst].add(src)
        if has_cycle(self.parents, self.num_vars):
            self.parents[dst].remove(src)
            return False
        return True

    def remove_edge(self, src: int, dst: int) -> bool:
        if src not in self.parents[dst]:
            return False
        self.parents[dst].remove(src)
        return True

    def reverse_edge(self, src: int, dst: int) -> bool:
        if src not in self.parents[dst]:
            return False

        self.parents[dst].remove(src)
        self.parents[src].add(dst)

        if has_cycle(self.parents, self.num_vars):
            self.parents[src].remove(dst)
            self.parents[dst].add(src)
            return False
        return True

    def edge_list(self) -> List[Tuple[int, int]]:
        edges = []
        for dst in range(self.num_vars):
            for src in sorted(self.parents[dst]):
                edges.append((src, dst))
        return edges

    def __str__(self) -> str:
        return "{" + ", ".join(f"{s}->{d}" for s, d in self.edge_list()) + "}"


# ============================================================
# Scoring engine (BIC)
# ============================================================

class ScoringEngine:
    def __init__(self, data: np.ndarray):
        if data.ndim != 2:
            raise ValueError("data must be 2D")
        if not np.issubdtype(data.dtype, np.integer):
            raise ValueError("data must contain integer-encoded discrete values")

        self.data = data
        self.num_samples, self.num_vars = data.shape

        # An array for each variable all teh values it can take on
        # Examples if a takes on 0-1, b takes on 0-5, cardinalities: [2, 6]
        # Assuming the data states start at 0
        # not really necessary for simple variables
        self.cardinalities = [make_state_counts(data[:, i]) for i in range(self.num_vars)]
        
        # Memoization for parital DP Later
        # Key is of the form (node, (parent, parent, ...))
        # value is the score
        self.local_score_cache: Dict[Tuple[int, Tuple[int, ...]], float] = {}

    def compute_local_score(self, node: int, parent_set: Set[int]) -> float:
        key = (node, tuple(sorted(parent_set)))
        if key in self.local_score_cache:
            return self.local_score_cache[key]

        x = self.data[:, node]
        r_i = self.cardinalities[node]
        parent_list = sorted(parent_set)

        if len(parent_list) == 0:
            counts = np.bincount(x, minlength=r_i)
            total = counts.sum()

            log_likelihood = 0.0
            for c in counts:
                if c > 0:
                    p = c / total
                    log_likelihood += c * math.log(p)

            k = r_i - 1
            bic = log_likelihood - 0.5 * k * math.log(self.num_samples)
            self.local_score_cache[key] = bic
            return bic

        parent_cards = [self.cardinalities[p] for p in parent_list]
        q_i = int(np.prod(parent_cards))

        counts = np.zeros((q_i, r_i), dtype=np.int64)

        for row in range(self.num_samples):
            parent_vals = self.data[row, parent_list]
            cfg = parent_config_index(parent_vals, parent_cards)
            counts[cfg, x[row]] += 1

        log_likelihood = 0.0
        for cfg in range(q_i):
            Nij = counts[cfg].sum()
            if Nij == 0:
                continue
            for k_state in range(r_i):
                Nijk = counts[cfg, k_state]
                if Nijk > 0:
                    log_likelihood += Nijk * math.log(Nijk / Nij)

        k_params = (r_i - 1) * q_i
        bic = log_likelihood - 0.5 * k_params * math.log(self.num_samples)
        self.local_score_cache[key] = bic
        return bic

    def compute_total_score(self, bn: BayesianNetwork) -> float:
        return sum(self.compute_local_score(node, bn.parents[node]) for node in range(bn.num_vars))

    def delta_score_for_edit(
        self,
        bn: BayesianNetwork,
        op: str,
        src: int,
        dst: int,
    ) -> Optional[Tuple[float, BayesianNetwork]]:
        new_bn = bn.copy()
        affected_nodes: Set[int] = set()

        if op == "add":
            if not new_bn.add_edge(src, dst):
                return None
            affected_nodes.add(dst)
        elif op == "remove":
            if not new_bn.remove_edge(src, dst):
                return None
            affected_nodes.add(dst)
        elif op == "reverse":
            if not new_bn.reverse_edge(src, dst):
                return None
            affected_nodes.add(src)
            affected_nodes.add(dst)
        else:
            raise ValueError(f"Unknown op: {op}")

        old_score = sum(self.compute_local_score(node, bn.parents[node]) for node in affected_nodes)
        new_score = sum(self.compute_local_score(node, new_bn.parents[node]) for node in affected_nodes)
        return new_score - old_score, new_bn


# ============================================================
# Hill climber
# ============================================================

class HillClimber:
    def __init__(self, scorer: ScoringEngine, max_parents: int = 2, seed: int = 0):
        self.scorer = scorer
        self.max_parents = max_parents
        self.rng = random.Random(seed)
        self.seed = seed

    def legal_operations(self, bn: BayesianNetwork) -> List[Tuple[str, int, int]]:
        ops = []
        D = bn.num_vars

        for src in range(D):
            for dst in range(D):
                if src == dst:
                    continue

                if bn.has_edge(src, dst):
                    ops.append(("remove", src, dst))
                    ops.append(("reverse", src, dst))
                else:
                    if len(bn.parents[dst]) < self.max_parents:
                        ops.append(("add", src, dst))

        self.rng.shuffle(ops)
        return ops

    def run(self, bn_init: BayesianNetwork, max_iters: int = 50, verbose: bool = True) -> BayesianNetwork:
        bn = bn_init.copy()
        total_score = self.scorer.compute_total_score(bn)

        if verbose:
            print(f"Seed: {self.seed}")
            print(f"Initial score: {total_score:.4f}")
            print(f"Initial graph: {bn}")

        for step in range(max_iters):
            best_delta = 0.0
            best_bn = None
            best_op = None

            for op, src, dst in self.legal_operations(bn):
                result = self.scorer.delta_score_for_edit(bn, op, src, dst)
                if result is None:
                    continue

                delta, candidate_bn = result
                if delta > best_delta:
                    best_delta = delta
                    best_bn = candidate_bn
                    best_op = (op, src, dst)

            if best_bn is None:
                if verbose:
                    print(f"Stopped at iteration {step}: no improving move found.")
                break

            bn = best_bn
            total_score += best_delta

            if verbose:
                print(
                    f"Iter {step:02d}: {best_op[0]} {best_op[1]}->{best_op[2]} | "
                    f"delta={best_delta:.4f} | score={total_score:.4f}"
                )

        if verbose:
            print(f"Final graph: {bn}")
            print(f"Final score: {total_score:.4f}")

        return bn


# ============================================================
# CSV loading
# ============================================================

def load_dataset(samples_csv: str, edges_csv: Optional[str] = None):
    df = pd.read_csv(samples_csv)
    # get all the numbers
    data = df.to_numpy(dtype=int)
    col_names = list(df.columns)

    true_edges_named: List[Tuple[str, str]] = []
    if edges_csv is not None and Path(edges_csv).exists():
        edges_df = pd.read_csv(edges_csv)
        true_edges_named = list(edges_df.itertuples(index=False, name=None))

    return data, col_names, true_edges_named


# ============================================================
# Graph helpers
# ============================================================

def named_edges_from_bn(bn: BayesianNetwork, col_names: List[str]) -> List[Tuple[str, str]]:
    return [(col_names[s], col_names[d]) for s, d in bn.edge_list()]


def print_parent_sets(bn: BayesianNetwork, col_names: List[str]) -> None:
    print("\nParent sets:")
    for node in range(bn.num_vars):
        parents = [col_names[p] for p in sorted(bn.parents[node])]
        print(f"  {col_names[node]} <- {parents}")


def compare_edges(learned_edges: List[Tuple[str, str]], true_edges: List[Tuple[str, str]]) -> None:
    learned = set(learned_edges)
    true = set(true_edges)

    print("\nEdge comparison:")
    print(f"  Learned edges: {len(learned)}")
    print(f"  True edges:    {len(true)}")
    print(f"  Correct:       {len(learned & true)}")
    print(f"  False pos:     {len(learned - true)}")
    print(f"  Missed:        {len(true - learned)}")

    if learned - true:
        print("  False positives:", sorted(learned - true))
    if true - learned:
        print("  Missed edges:   ", sorted(true - learned))


def draw_graphs(
    learned_edges: List[Tuple[str, str]],
    true_edges: Optional[List[Tuple[str, str]]] = None,
    seed: int = 0,
) -> None:
    if true_edges:
        fig, axes = plt.subplots(1, 2, figsize=(14, 6))
        graphs = [("True graph", true_edges), ("Learned graph", learned_edges)]
    else:
        fig, axes = plt.subplots(1, 1, figsize=(7, 6))
        axes = [axes]
        graphs = [("Learned graph", learned_edges)]

    for ax, (title, edges) in zip(axes, graphs):
        G = nx.DiGraph()
        G.add_edges_from(edges)
        pos = nx.spring_layout(G, seed=seed)
        nx.draw(
            G,
            pos,
            ax=ax,
            with_labels=True,
            node_size=2200,
            font_size=9,
            arrows=True,
        )
        ax.set_title(title)

    plt.tight_layout()
    plt.show()


def inspect_node(
    node_name: str,
    bn: BayesianNetwork,
    col_names: List[str],
    true_edges: List[Tuple[str, str]],
    df: pd.DataFrame,
) -> None:
    if node_name not in col_names:
        print("Unknown node.")
        return

    idx = col_names.index(node_name)

    learned_parents = [col_names[p] for p in sorted(bn.parents[idx])]
    learned_children = [col_names[c] for c in range(bn.num_vars) if idx in bn.parents[c]]

    true_parents = [src for src, dst in true_edges if dst == node_name]
    true_children = [dst for src, dst in true_edges if src == node_name]

    print(f"\nNode: {node_name}")
    print(f"  Learned parents: {learned_parents}")
    print(f"  Learned children: {learned_children}")
    if true_edges:
        print(f"  True parents:    {true_parents}")
        print(f"  True children:   {true_children}")
    print("  Value counts:")
    print(df[node_name].value_counts().sort_index())


# ============================================================
# Main / interactive shell
# ============================================================

def main():
    # Set Parameters
    samples_csv = "../cleaned-datasets/cancer_samples.csv"
    edges_csv = "../cleaned-datasets/cancer_edges.csv"
    seed = 7
    max_parents = 2
    max_iters = 30

    # Read in samples
    data, col_names, true_edges_named = load_dataset(samples_csv, edges_csv)
    df = pd.read_csv(samples_csv)

    print(f"Loaded samples from: {samples_csv}")
    print(f"Shape: {data.shape}")
    print(f"Seed: {seed}")
    print(f"Variables: {col_names}")

    # Create the Scoring Enginer
    scorer = ScoringEngine(data)
    bn0 = BayesianNetwork.empty(num_vars=data.shape[1])

    search = HillClimber(scorer, max_parents=max_parents, seed=seed)
    learned_bn = search.run(bn0, max_iters=max_iters, verbose=True)

    learned_edges_named = named_edges_from_bn(learned_bn, col_names)

    print_parent_sets(learned_bn, col_names)

    if true_edges_named:
        compare_edges(learned_edges_named, true_edges_named)

    draw_graphs(learned_edges_named, true_edges_named, seed=seed)

    while True:
        cmd = input(
            "\nCommand [parents, compare, draw, node <name>, edges, quit]: "
        ).strip()

        if cmd == "quit":
            break
        elif cmd == "parents":
            print_parent_sets(learned_bn, col_names)
        elif cmd == "compare":
            if true_edges_named:
                compare_edges(learned_edges_named, true_edges_named)
            else:
                print("No true edges CSV loaded.")
        elif cmd == "draw":
            draw_graphs(learned_edges_named, true_edges_named, seed=seed)
        elif cmd == "edges":
            print("\nLearned edges:")
            for e in learned_edges_named:
                print(" ", e)
            if true_edges_named:
                print("\nTrue edges:")
                for e in true_edges_named:
                    print(" ", e)
        elif cmd.startswith("node "):
            node_name = cmd[5:].strip()
            inspect_node(node_name, learned_bn, col_names, true_edges_named, df)
        else:
            print("Unknown command.")


if __name__ == "__main__":
    main()