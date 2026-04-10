package com.github.jvsena42.mandacaru.domain.model

/**
 * One-shot events emitted by [LightningNodeManager] via a SharedFlow.
 *
 * These are distinct from the continuous StateFlows (channels, payments, balance)
 * in that they represent discrete moments requiring user feedback (toasts,
 * haptics, sound) rather than persistent state.
 *
 * The ViewModel collects these and converts them to UI-layer side-effects.
 */
sealed class LightningNodeEvent {

    data class PaymentSuccessful(
        val paymentId: String,
        val amountMsat: ULong?,
        val feesPaidMsat: ULong?,
    ) : LightningNodeEvent()

    data class PaymentFailed(
        val paymentId: String,
        /** ldk-node failure reason string; null if unknown. */
        val reason: String?,
    ) : LightningNodeEvent()

    data class PaymentReceived(
        val paymentId: String,
        val amountMsat: ULong,
    ) : LightningNodeEvent()

    data class ChannelReady(
        val channelId: String,
        val counterpartyNodeId: String,
    ) : LightningNodeEvent()

    data class ChannelClosed(
        val channelId: String,
        val counterpartyNodeId: String,
        /** Human-readable closure reason from ldk-node (may include force-close details). */
        val reason: String?,
    ) : LightningNodeEvent()

    data class ChannelPendingOpen(
        val channelId: String,
        val counterpartyNodeId: String,
    ) : LightningNodeEvent()

    /** The node encountered a non-fatal issue (e.g. peer disconnected, gossip stale). */
    data class Warning(val message: String) : LightningNodeEvent()
}
