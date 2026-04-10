package com.github.jvsena42.mandacaru.data.lightning

import com.github.jvsena42.mandacaru.domain.model.LightningBalance
import com.github.jvsena42.mandacaru.domain.model.LightningChannel
import com.github.jvsena42.mandacaru.domain.model.LightningNodeEvent
import com.github.jvsena42.mandacaru.domain.model.LightningNodeState
import com.github.jvsena42.mandacaru.domain.model.LightningPayment
import kotlinx.coroutines.flow.SharedFlow
import kotlinx.coroutines.flow.StateFlow

/**
 * Thin delegation layer between [LightningRepository] and [LightningNodeManager].
 *
 * This class exists so ViewModels depend on the interface, not the manager.
 * Additional cross-cutting logic (analytics, logging, feature flags) can be
 * inserted here without touching either the interface or the manager.
 */
class LightningRepositoryImpl(
    private val manager: LightningNodeManager,
) : LightningRepository {

    override val state: StateFlow<LightningNodeState> = manager.state
    override val channels: StateFlow<List<LightningChannel>> = manager.channels
    override val payments: StateFlow<List<LightningPayment>> = manager.payments
    override val balance: StateFlow<LightningBalance> = manager.balance
    override val events: SharedFlow<LightningNodeEvent> = manager.events

    override suspend fun sendBolt11(invoiceStr: String) =
        manager.sendBolt11(invoiceStr.trim())

    override suspend fun sendBolt11WithAmount(invoiceStr: String, amountMsat: ULong) =
        manager.sendBolt11WithAmount(invoiceStr.trim(), amountMsat)

    override suspend fun createBolt11Invoice(
        amountMsat: ULong?,
        description: String,
        expirySecs: UInt,
    ) = manager.createBolt11Invoice(amountMsat, description, expirySecs)

    override suspend fun createBolt12Offer(description: String) =
        manager.createBolt12Offer(description)

    override suspend fun openChannel(
        nodeIdHex: String,
        addressStr: String,
        channelAmountSats: ULong,
        pushMsat: ULong?,
    ) = manager.openChannel(nodeIdHex.trim(), addressStr.trim(), channelAmountSats, pushMsat)

    override suspend fun closeChannel(userChannelId: String, counterpartyNodeId: String) =
        manager.closeChannel(userChannelId, counterpartyNodeId)

    override suspend fun forceCloseChannel(userChannelId: String, counterpartyNodeId: String) =
        manager.forceCloseChannel(userChannelId, counterpartyNodeId)

    override suspend fun nodeId() = manager.nodeId()

    override suspend fun refreshAll() = manager.refreshAll()
}
