#ifndef MCMC_HW_CONFIG_H
#define MCMC_HW_CONFIG_H

/*
 * Keep this file matched to rtl/mcmc_hw_config.vh and the loaded bitstream.
 *
 * Current Avalon table map:
 *   address[11:7] selects node 0..31
 *   address[6:1]  selects one score/mask pair slot
 *   address[0]    selects score word (0) or mask word (1)
 */
#define MCMC_HW_MAX_NODES 32
#define MCMC_HW_CANDIDATE_SLOTS_PER_NODE 64
#define MCMC_HW_USABLE_CANDIDATES_PER_NODE (MCMC_HW_CANDIDATE_SLOTS_PER_NODE - 1)
#define MCMC_HW_WORDS_PER_CANDIDATE 2
#define MCMC_HW_NODE_ADDR_SHIFT 7

static inline int mcmc_hw_candidate_score_offset(int node, int candidate)
{
    return (node << MCMC_HW_NODE_ADDR_SHIFT) | (candidate << 1);
}

static inline int mcmc_hw_candidate_mask_offset(int node, int candidate)
{
    return mcmc_hw_candidate_score_offset(node, candidate) + 1;
}

#endif
