package com.github.jvsena42.mandacaru.data.lightning

import com.github.jvsena42.mandacaru.domain.model.LightningBalance
import com.github.jvsena42.mandacaru.domain.model.LightningChannel
import com.github.jvsena42.mandacaru.domain.model.LightningNodeEvent
import com.github.jvsena42.mandacaru.domain.model.LightningNodeState
import com.github.jvsena42.mandacaru.domain.model.LightningPayment
import kotlinx.coroutines.flow.SharedFlow
import kotlinx.coroutines.flow.StateFlow

/**
 * Domain-facing interface for all Lightning operations.
 *
 * ViewModels depend on this interface, not on [LightningNodeManager] directly,
 * keeping the Koin module the only place that knows about the implementation.
 *
 * Convention: all methods that dispatch to the node return [Result<T>]:
 *   - [Result.success] = the operation was accepted (not necessarily confirmed).
 *   - [Result.failure] = the node rejected the operation synchronously.
 *
 * Final outcomes (payment settled, channel ready) arrive through [events].
 */
interface LightningRepository {

    // ── Observables ───────────────────────────────────────────────────────────

    val state: StateFlow<LightningNodeState>
    val channels: StateFlow<List<LightningChannel>>
    val payments: StateFlow<List<LightningPayment>>
    val balance: StateFlow<LightningBalance>

    /** One-shot events for toast / haptic feedback. Collect in a LaunchedEffect. */
    val events: SharedFlow<LightningNodeEvent>

    // ── Payments ──────────────────────────────────────────────────────────────

    suspend fun sendBolt11(invoiceStr: String): Result<String>
    suspend fun sendBolt11WithAmount(invoiceStr: String, amountMsat: ULong): Result<String>

    suspend fun createBolt11Invoice(
        amountMsat: ULong?,
        description: String,
        expirySecs: UInt,
    ): Result<String>

    suspend fun createBolt12Offer(description: String): Result<String>

    // ── Channels ──────────────────────────────────────────────────────────────

    suspend fun openChannel(
        nodeIdHex: String,
        addressStr: String,
        channelAmountSats: ULong,
        pushMsat: ULong?,
    ): Result<String>

    suspend fun closeChannel(userChannelId: String, counterpartyNodeId: String): Result<Unit>
    suspend fun forceCloseChannel(userChannelId: String, counterpartyNodeId: String): Result<Unit>

    // ── Node info ─────────────────────────────────────────────────────────────

    suspend fun nodeId(): String?
    suspend fun refreshAll()
}
