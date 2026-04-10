package com.github.jvsena42.mandacaru.domain.model

/**
 * Consolidated view of all funds controlled by this wallet: on-chain and Lightning.
 *
 * All values sourced directly from ldk-node's [BalancesSnapshot]; no
 * interpolation or estimation is applied.
 *
 * @param totalOnchainSats          All on-chain funds (confirmed + unconfirmed).
 * @param spendableOnchainSats      On-chain funds available for new channel opens or withdrawals.
 * @param totalLightningMsat        Total value across all Lightning channels (milli-satoshis).
 * @param spendableLightningMsat    Immediately routable outbound balance (milli-satoshis).
 *                                  Excludes channel reserves and pending HTLCs.
 * @param receivableLightningMsat   Maximum single-payment inbound capacity (milli-satoshis).
 *                                  Aggregated across usable channels.
 * @param pendingClosingBalanceSats Funds awaiting on-chain confirmation from closing channels.
 */
data class LightningBalance(
    val totalOnchainSats: ULong,
    val spendableOnchainSats: ULong,
    val totalLightningMsat: ULong,
    val spendableLightningMsat: ULong,
    val receivableLightningMsat: ULong,
    val pendingClosingBalanceSats: ULong,
) {
    val totalLightningSats: ULong get() = totalLightningMsat / 1_000u
    val spendableLightningSats: ULong get() = spendableLightningMsat / 1_000u
    val receivableLightningSats: ULong get() = receivableLightningMsat / 1_000u

    /** Combined on-chain + Lightning total for a single portfolio view. */
    val grandTotalSats: ULong get() = totalOnchainSats + totalLightningSats + pendingClosingBalanceSats

    companion object {
        val ZERO = LightningBalance(
            totalOnchainSats = 0u,
            spendableOnchainSats = 0u,
            totalLightningMsat = 0u,
            spendableLightningMsat = 0u,
            receivableLightningMsat = 0u,
            pendingClosingBalanceSats = 0u,
        )
    }
}
