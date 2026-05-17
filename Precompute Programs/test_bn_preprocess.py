from __future__ import annotations

import json
import math

import numpy as np
import pandas as pd

from bn_preprocess.candidate_selection import NodeCandidates
from bn_preprocess.data import encode_discrete_frame
from bn_preprocess.emit import emit_tables
from bn_preprocess.fixed_point import convert_scores_to_fixed, parse_fixed_point
from bn_preprocess.parent_sets import decode_parent_set, encode_parent_set, enumerate_parent_sets
from bn_preprocess.scoring import DiscreteLocalScorer


def test_empty_parent_set_included() -> None:
    records = enumerate_parent_sets([[1, 2], [0], [0, 1]], num_variables=3, max_parent_size=2)
    assert records[0][0].parents == ()
    assert records[1][0].mask_chunks == (0,)


def test_bitmask_encoding_decoding_multiword() -> None:
    parents = (0, 63, 64, 129)
    chunks = encode_parent_set(parents, num_variables=130)
    assert len(chunks) == 3
    assert decode_parent_set(chunks, num_variables=130) == parents


def test_bdeu_score_runs_without_nan() -> None:
    df = _synthetic_discrete_frame()
    encoded = encode_discrete_frame(df, ["discrete"] * df.shape[1])
    scorer = DiscreteLocalScorer(encoded=encoded, score_kind="bdeu", equivalent_sample_size=1.0)
    assert math.isfinite(scorer.local_score(1, ()))
    assert math.isfinite(scorer.local_score(1, (0,)))


def test_offset_table_matches_binary_lengths(tmp_path) -> None:
    variable_names = ["a", "b", "c"]
    parent_sets = enumerate_parent_sets([[1, 2], [0], [0, 1]], num_variables=3, max_parent_size=2)
    scores = [[-1.0] * len(records) for records in parent_sets]
    flat_scores = [score for node_scores in scores for score in node_scores]
    fixed = convert_scores_to_fixed(flat_scores, parse_fixed_point("q16.16"))
    candidates = [
        NodeCandidates(0, [1, 2], {1: 1.0, 2: 1.0}, {1: 0.5, 2: 0.5}),
        NodeCandidates(1, [0], {0: 1.0}, {0: 1.0}),
        NodeCandidates(2, [0, 1], {0: 1.0, 1: 1.0}, {0: 0.5, 1: 0.5}),
    ]
    result = emit_tables(
        tmp_path,
        parent_sets,
        scores,
        fixed,
        variable_names,
        ["discrete"] * 3,
        candidates,
        {"a": ["0", "1"], "b": ["0", "1"], "c": ["0", "1"]},
        {"test": True},
        emit_hex=True,
    )
    meta = json.loads((tmp_path / "node_offsets.json").read_text())
    total_count = sum(node["count"] for node in meta["nodes"])
    assert total_count == result.total_parent_sets
    assert (tmp_path / "parent_sets.bin").stat().st_size == total_count * 8
    assert (tmp_path / "local_scores.bin").stat().st_size == total_count * 4
    assert meta["nodes"][2]["parent_set_offset_bytes"] == (
        len(parent_sets[0]) + len(parent_sets[1])
    ) * 8


def _synthetic_discrete_frame(rows: int = 200) -> pd.DataFrame:
    rng = np.random.default_rng(2)
    a = rng.integers(0, 2, size=rows)
    noise = rng.integers(0, 2, size=rows)
    b = a ^ noise
    c = rng.integers(0, 3, size=rows)
    return pd.DataFrame({"a": a, "b": b, "c": c})

