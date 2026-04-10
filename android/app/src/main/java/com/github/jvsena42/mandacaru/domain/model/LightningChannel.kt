package com.github.jvsena42.mandacaru.domain.model

/**
 * Domain representation of a Lightning channel.
 *
 * Derived from ldk-node's [ChannelDetails]; all raw ULong msats fields
 * are preserved alongside convenience sats getters to avoid lossy conversion
 * at the boundary.
 *
 * Security note: [counterpartyNodeId] is the hex-encoded public key of the
 * remote peer. It must be validated against the expected LSP pubkey when
 * performing privileged operations (e.g. zero-reserve or 0-conf opens).
 *
 * @param channelId               Unique channel identifier (32-byte hex).
 * @param userChannelId           Local identifier used for close/splice calls.
 * @param counterpartyNodeId      Remote node pubkey (hex).
 * @param capacitySats            Total channel capacity as agreed at open time.
 * @param outboundCapacityMsat    Our spendable balance minus reserves (msat).
 * @param inboundCapacityMsat     Remote's spendable balance minus reserves (msat).
 * @param isUsable                True when HTLCs can be forwarded: connected + ready.
 * @param isReady                 Funding confirmed to required depth.
 * @param isPublic                Whether the channel is announced to the network.
 * @param isOutbound              True if we opened (funded) this channel.
 * @param confirmations           Current on-chain confirmations of funding tx.
 * @param confirmationsRequired   Funding confirmations needed before use.
 * @param nextOutboundHtlcLimitMsat  Max single HTLC we can route out (msat).
 */
data class LightningChannel(
    val channelId: String,
    val userChannelId: String,
    val counterpartyNodeId: String,
    val capacitySats: ULong,
    val outboundCapacityMsat: ULong,
    val inboundCapacityMsat: ULong,
    val isUsable: Boolean,
    val isReady: Boolean,
    val isPublic: Boolean,
    val isOutbound: Boolean,
    val confirmations: UInt?,
    val confirmationsRequired: UInt?,
    val nextOutboundHtlcLimitMsat: ULong,
) {
    val outboundCapacitySats: ULong get() = outboundCapacityMsat / 1_000u
    val inboundCapacitySats: ULong get() = inboundCapacityMsat / 1_000u
    val nextOutboundHtlcLimitSats: ULong get() = nextOutboundHtlcLimitMsat / 1_000u

    /**
     * Ratio of local balance to total capacity [0..1].
     * Used for the channel capacity bar visual.
     */
    val localBalanceRatio: Float
        get() = if (capacitySats > 0u) {
            outboundCapacitySats.toFloat() / capacitySats.toFloat()
        } else {
            0f
        }

    /** True if the funding tx has been seen on-chain but not yet confirmed enough. */
    val isPendingOpen: Boolean
        get() = !isReady && confirmations != null

    /** Remaining confirmations before the channel becomes usable. */
    val confirmationsRemaining: UInt?
        get() = if (isPendingOpen && confirmations != null && confirmationsRequired != null) {
            if (confirmationsRequired > confirmations) confirmationsRequired - confirmations else 0u
        } else null
}
