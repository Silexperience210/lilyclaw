package com.github.jvsena42.mandacaru.data.lightning

import android.content.Context
import android.net.ConnectivityManager
import android.net.Network
import android.net.NetworkCapabilities
import android.net.NetworkRequest
import android.util.Log
import com.github.jvsena42.mandacaru.data.preferences.LightningPreferences
import com.github.jvsena42.mandacaru.domain.model.LightningBalance
import com.github.jvsena42.mandacaru.domain.model.LightningChannel
import com.github.jvsena42.mandacaru.domain.model.LightningNodeEvent
import com.github.jvsena42.mandacaru.domain.model.LightningNodeState
import com.github.jvsena42.mandacaru.domain.model.LightningPayment
import com.github.jvsena42.mandacaru.domain.model.PaymentDirection
import com.github.jvsena42.mandacaru.domain.model.PaymentStatus
import com.github.jvsena42.mandacaru.domain.model.PaymentType
import kotlinx.coroutines.CancellationException
import kotlinx.coroutines.CoroutineScope
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.Job
import kotlinx.coroutines.SupervisorJob
import kotlinx.coroutines.cancel
import kotlinx.coroutines.channels.awaitClose
import kotlinx.coroutines.delay
import kotlinx.coroutines.flow.MutableSharedFlow
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.SharedFlow
import kotlinx.coroutines.flow.StateFlow
import kotlinx.coroutines.flow.asSharedFlow
import kotlinx.coroutines.flow.asStateFlow
import kotlinx.coroutines.flow.callbackFlow
import kotlinx.coroutines.flow.debounce
import kotlinx.coroutines.isActive
import kotlinx.coroutines.launch
import kotlinx.coroutines.sync.Mutex
import kotlinx.coroutines.sync.withLock
import kotlinx.coroutines.withContext
import org.lightningdevkit.ldknode.AnchorChannelsConfig
import org.lightningdevkit.ldknode.Bolt11Invoice
import org.lightningdevkit.ldknode.Builder
import org.lightningdevkit.ldknode.ChannelDetails
import org.lightningdevkit.ldknode.Config
import org.lightningdevkit.ldknode.Event
import org.lightningdevkit.ldknode.NodeAlias
import org.lightningdevkit.ldknode.PaymentDetails
import org.lightningdevkit.ldknode.PaymentDirection as LdkPaymentDirection
import org.lightningdevkit.ldknode.PaymentKind
import org.lightningdevkit.ldknode.PaymentStatus as LdkPaymentStatus
import org.lightningdevkit.ldknode.PublicKey
import org.lightningdevkit.ldknode.SocketAddress
import org.lightningdevkit.ldknode.UserChannelId
import java.io.File
import org.lightningdevkit.ldknode.Network as LdkNetwork
import org.lightningdevkit.ldknode.Node as LdkNode

/**
 * Central coordinator for the Lightning node lifecycle.
 *
 * This singleton is managed by Koin and shared between [FlorestaService]
 * (which starts/stops it) and all ViewModels (which observe its state flows).
 *
 * ── Startup sequence ──────────────────────────────────────────────────────────
 *
 *  1. FlorestaService successfully starts Floresta and its Electrum server.
 *  2. FlorestaService calls [start] with the active network.
 *  3. [LightningNodeManager] builds ldk-node pointing at Floresta's Electrum
 *     server on localhost.
 *  4. ldk-node handles gossip sync (RGS snapshot), wallet sync, and peer
 *     reconnection internally.
 *  5. The event loop begins; state flows are updated on every event.
 *
 * ── Connectivity & NAT ────────────────────────────────────────────────────────
 *
 *  Mobile IPs change on every WiFi/4G transition. Instead of announcing a
 *  public IP (which would break after every handover), we rely exclusively on
 *  the configured LSP (LSPS2) for inbound routing:
 *
 *    - Outbound payments: routed via our gossip-maintained routing table.
 *    - Inbound payments:  LSP opens a JIT channel when the first payment
 *      arrives (LSPS2 protocol). The LSP has a stable public IP.
 *    - Channel monitoring: handled by ldk-node's built-in chain monitor,
 *      which reads from Floresta's Electrum server.
 *
 *  When the device regains network connectivity, [reconnectPeers] is called
 *  after a 3-second debounce to give the radio stack time to stabilise.
 *
 * ── Security ──────────────────────────────────────────────────────────────────
 *
 *  • All key material is sourced from [KeystoreManager] (Android Keystore).
 *  • ldk-node persists channel monitors in SQLite; losing these files means
 *    losing the ability to produce justice transactions — but funds can still
 *    be recovered via the mnemonic if the counterparty is cooperative.
 *  • Anchor channels are used by default so fee-bumping is available in
 *    the event of a force-close during mempool congestion.
 *  • Trusted-peers-0-conf is restricted to the configured LSP only.
 */
class LightningNodeManager(
    private val context: Context,
    private val keystoreManager: KeystoreManager,
) {
    // ── State flows ───────────────────────────────────────────────────────────

    private val _state = MutableStateFlow<LightningNodeState>(LightningNodeState.Idle)
    val state: StateFlow<LightningNodeState> = _state.asStateFlow()

    private val _channels = MutableStateFlow<List<LightningChannel>>(emptyList())
    val channels: StateFlow<List<LightningChannel>> = _channels.asStateFlow()

    private val _payments = MutableStateFlow<List<LightningPayment>>(emptyList())
    val payments: StateFlow<List<LightningPayment>> = _payments.asStateFlow()

    private val _balance = MutableStateFlow(LightningBalance.ZERO)
    val balance: StateFlow<LightningBalance> = _balance.asStateFlow()

    /**
     * One-shot events (payment received, channel opened, etc.) for UI feedback.
     * Replay = 0: events are not cached; a ViewModel that starts late will not
     * receive stale events.
     */
    private val _events = MutableSharedFlow<LightningNodeEvent>(
        replay = 0,
        extraBufferCapacity = 32,
    )
    val events: SharedFlow<LightningNodeEvent> = _events.asSharedFlow()

    // ── Internal state ────────────────────────────────────────────────────────

    private var ldkNode: LdkNode? = null
    private val nodeMutex = Mutex()
    private val managerScope = CoroutineScope(SupervisorJob() + Dispatchers.IO)

    // Background job references — stored so they can be cancelled individually on stop().
    private var eventLoopJob: Job? = null
    private var pollingJob: Job? = null
    private var networkMonitorJob: Job? = null

    // ── Lifecycle ─────────────────────────────────────────────────────────────

    /**
     * Builds and starts the ldk-node instance.
     *
     * Must be called from an IO coroutine. Idempotent: no-op if already running.
     *
     * @param ldkNetwork  The active Floresta network, converted to ldk-node enum.
     * @param prefs       Snapshot of Lightning preferences read from DataStore.
     */
    suspend fun start(ldkNetwork: LdkNetwork, prefs: LightningPreferences) {
        if (_state.value.isOperational || _state.value.isTransitioning) return
        _state.value = LightningNodeState.Starting

        withContext(Dispatchers.IO) {
            try {
                val mnemonic = keystoreManager.getOrGenerateMnemonic()
                val storagePath = File(context.filesDir, "ldk_node_v1").absolutePath
                File(storagePath).mkdirs()

                // Build LSP trusted-peers list for 0-conf and anchor reserves.
                val lspPubkeyList = prefs.lspPubkey
                    .takeIf { it.isNotBlank() }
                    ?.let { listOf(PublicKey(it)) }
                    ?: emptyList()

                val config = Config(
                    storageDirPath = storagePath,
                    network = ldkNetwork,
                    // No listening address announced: we rely on the LSP for inbound.
                    listeningAddresses = null,
                    announcementAddresses = null,
                    nodeAlias = NodeAlias(prefs.nodeAlias.padEnd(32, '\u0000').substring(0, 32)),
                    // Only trust the configured LSP for zero-reserve channels.
                    trustedPeers0conf = lspPubkeyList,
                    // Anchor channels enable CPFP fee-bumping on force-closes.
                    anchorChannelsConfig = AnchorChannelsConfig(
                        trustedPeersNoReserve = lspPubkeyList,
                        perChannelReserveSats = ANCHOR_RESERVE_SATS,
                    ),
                )

                val builder = Builder(config).apply {
                    // Chain source: Floresta's built-in Electrum server (localhost).
                    // TLS not needed for loopback connections.
                    setChainSourceElectrum(
                        serverUrl = electrumUrl(ldkNetwork),
                        tlsCertificate = null,
                    )

                    // Gossip: RGS is strongly preferred on mobile (reduces startup
                    // time from minutes to seconds). Falls back to P2P if disabled.
                    if (prefs.useRgsGossip && prefs.rgsUrl.isNotBlank()) {
                        setGossipSourceRgs(prefs.rgsUrl)
                    } else {
                        setGossipSourceP2p()
                    }

                    // LSPS2: JIT inbound liquidity from the configured LSP.
                    // Only enabled if both address and pubkey are set.
                    if (prefs.isLspConfigured) {
                        setLiquiditySourceLsps2(
                            address = SocketAddress(prefs.lspAddress),
                            nodeId = PublicKey(prefs.lspPubkey),
                            token = prefs.lspToken.takeIf { it.isNotBlank() },
                        )
                    }

                    setEntropyBip39Mnemonic(mnemonic = mnemonic, passphrase = null)
                }

                val node = builder.buildWithSqliteStore()
                node.start()

                nodeMutex.withLock { ldkNode = node }
                _state.value = LightningNodeState.Running(node.nodeId().toString())

                refreshAll()
                launchEventLoop()
                launchPolling()
                launchNetworkMonitor()

            } catch (e: Exception) {
                Log.e(TAG, "Failed to start Lightning node", e)
                _state.value = LightningNodeState.Error(
                    message = e.message ?: "Lightning node failed to start",
                    cause = e,
                )
            }
        }
    }

    /**
     * Gracefully stops the node, waiting for any pending operations to finish.
     * Safe to call multiple times.
     */
    suspend fun stop() {
        if (_state.value is LightningNodeState.Idle) return
        _state.value = LightningNodeState.Stopping

        // Cancel background jobs before stopping the node so they don't
        // race with the shutdown or launch duplicate copies on the next start().
        eventLoopJob?.cancel()
        pollingJob?.cancel()
        networkMonitorJob?.cancel()
        eventLoopJob = null
        pollingJob = null
        networkMonitorJob = null

        withContext(Dispatchers.IO) {
            nodeMutex.withLock {
                try {
                    ldkNode?.stop()
                } catch (e: Exception) {
                    Log.e(TAG, "Error while stopping Lightning node", e)
                } finally {
                    ldkNode = null
                }
            }
        }

        _state.value = LightningNodeState.Idle
        _channels.value = emptyList()
        _payments.value = emptyList()
        _balance.value = LightningBalance.ZERO
    }

    // ── Background jobs ───────────────────────────────────────────────────────

    private fun launchEventLoop() {
        eventLoopJob = managerScope.launch {
            while (isActive) {
                val node = nodeMutex.withLock { ldkNode } ?: break
                try {
                    // nextEventAsync() suspends until an event is available.
                    val event = node.nextEventAsync()
                    handleEvent(event)
                    node.eventHandled()
                } catch (e: CancellationException) {
                    throw e
                } catch (e: Exception) {
                    Log.e(TAG, "Event loop error", e)
                    delay(EVENT_LOOP_RETRY_DELAY_MS)
                }
            }
        }
    }

    private fun launchPolling() {
        pollingJob = managerScope.launch {
            while (isActive) {
                delay(POLLING_INTERVAL_MS)
                try {
                    refreshAll()
                } catch (e: CancellationException) {
                    throw e
                } catch (e: Exception) {
                    Log.w(TAG, "State polling error", e)
                }
            }
        }
    }

    /**
     * Observes network connectivity. After a reconnection, waits 3 seconds
     * for the IP stack to stabilise before requesting a state refresh.
     * ldk-node handles peer reconnection internally; we just refresh our
     * cached state to reflect any updates.
     */
    private fun launchNetworkMonitor() {
        networkMonitorJob = managerScope.launch {
            val cm = context.getSystemService(Context.CONNECTIVITY_SERVICE) as ConnectivityManager
            callbackFlow {
                val cb = object : ConnectivityManager.NetworkCallback() {
                    override fun onAvailable(network: Network) { trySend(true) }
                    override fun onLost(network: Network) { trySend(false) }
                }
                val req = NetworkRequest.Builder()
                    .addCapability(NetworkCapabilities.NET_CAPABILITY_INTERNET)
                    .build()
                cm.registerNetworkCallback(req, cb)
                awaitClose { cm.unregisterNetworkCallback(cb) }
            }
                .debounce(NETWORK_DEBOUNCE_MS)
                .collect { isAvailable ->
                    if (isAvailable) {
                        Log.d(TAG, "Network restored — refreshing node state")
                        refreshAll()
                    }
                }
        }
    }

    // ── Event handling ────────────────────────────────────────────────────────

    private suspend fun handleEvent(event: Event) {
        when (event) {
            is Event.PaymentSuccessful -> {
                refreshPayments()
                refreshBalance()
                _events.emit(
                    LightningNodeEvent.PaymentSuccessful(
                        paymentId = event.paymentId.toString(),
                        // amountMsat is not on this event; retrieve from the payments list.
                        amountMsat = null,
                        feesPaidMsat = event.feesPaidMsat,
                    )
                )
            }

            is Event.PaymentFailed -> {
                refreshPayments()
                _events.emit(
                    LightningNodeEvent.PaymentFailed(
                        paymentId = event.paymentId.toString(),
                        reason = event.reason?.toString(),
                    )
                )
            }

            is Event.PaymentReceived -> {
                refreshPayments()
                refreshBalance()
                _events.emit(
                    LightningNodeEvent.PaymentReceived(
                        paymentId = event.paymentId.toString(),
                        amountMsat = event.amountMsat,
                    )
                )
            }

            is Event.PaymentClaimable -> {
                // ldk-node auto-claims; nothing for us to do except refresh.
                refreshPayments()
            }

            is Event.ChannelPendingOpen -> {
                refreshChannels()
                _events.emit(
                    LightningNodeEvent.ChannelPendingOpen(
                        channelId = event.channelId.toString(),
                        counterpartyNodeId = event.counterpartyNodeId.toString(),
                    )
                )
            }

            is Event.ChannelReady -> {
                refreshChannels()
                refreshBalance()
                _events.emit(
                    LightningNodeEvent.ChannelReady(
                        channelId = event.channelId.toString(),
                        counterpartyNodeId = event.counterpartyNodeId.toString(),
                    )
                )
            }

            is Event.ChannelClosed -> {
                refreshChannels()
                refreshBalance()
                _events.emit(
                    LightningNodeEvent.ChannelClosed(
                        channelId = event.channelId.toString(),
                        counterpartyNodeId = event.counterpartyNodeId.toString(),
                        reason = event.reason?.toString(),
                    )
                )
            }
        }
    }

    // ── State refresh ─────────────────────────────────────────────────────────

    suspend fun refreshAll() {
        refreshChannels()
        refreshPayments()
        refreshBalance()
    }

    suspend fun refreshChannels() {
        val node = nodeMutex.withLock { ldkNode } ?: return
        withContext(Dispatchers.IO) {
            _channels.value = node.listChannels().map { it.toDomain() }
        }
    }

    suspend fun refreshPayments() {
        val node = nodeMutex.withLock { ldkNode } ?: return
        withContext(Dispatchers.IO) {
            _payments.value = node.listPayments()
                .sortedByDescending { it.latestUpdateTimestamp }
                .map { it.toDomain() }
        }
    }

    suspend fun refreshBalance() {
        val node = nodeMutex.withLock { ldkNode } ?: return
        withContext(Dispatchers.IO) {
            val snap = node.listBalances()

            // Query channels once; derive both receivable and spendable from the same list.
            val usableChannels = node.listChannels().filter { it.isUsable }

            // Receivable: max single HTLC inbound (the channel with the most room).
            val receivableMsat = usableChannels
                .maxOfOrNull { it.inboundCapacityMsat }
                ?: 0u

            // Spendable LN: sum of outbound capacity across all usable channels,
            // excluding reserves and in-flight HTLCs (already reflected in the field).
            val spendableLnMsat = usableChannels
                .sumOf { it.outboundCapacityMsat }

            // Pending-closing balance: sweep outputs awaiting confirmation.
            val pendingSats = snap.pendingBalancesFromChannelClosures
                .sumOf { pending ->
                    when (pending) {
                        is org.lightningdevkit.ldknode.PendingSweepBalance.PendingBroadcast ->
                            pending.amountSatoshis
                        is org.lightningdevkit.ldknode.PendingSweepBalance.BroadcastAwaitingConfirmation ->
                            pending.amountSatoshis
                        is org.lightningdevkit.ldknode.PendingSweepBalance.AwaitingThresholdConfirmations ->
                            pending.amountSatoshis
                    }
                }.toULong()

            _balance.value = LightningBalance(
                totalOnchainSats = snap.totalOnchainBalanceSats,
                spendableOnchainSats = snap.spendableOnchainBalanceSats,
                totalLightningMsat = snap.totalLightningBalanceMsat,
                spendableLightningMsat = spendableLnMsat,
                receivableLightningMsat = receivableMsat,
                pendingClosingBalanceSats = pendingSats,
            )
        }
    }

    // ── Payment operations ────────────────────────────────────────────────────

    /**
     * Sends a BOLT-11 payment.
     *
     * @param invoiceStr  Raw invoice string (e.g. "lnbc500n1p...").
     * @return [Result.success] with the payment ID on dispatch, or
     *         [Result.failure] with the ldk-node exception.
     *
     * Note: success here means the payment was dispatched. The final
     * outcome (success/failure) arrives as a [LightningNodeEvent] via
     * the [events] SharedFlow.
     */
    suspend fun sendBolt11(invoiceStr: String): Result<String> =
        withContext(Dispatchers.IO) {
            runCatching {
                val node = checkNotNull(nodeMutex.withLock { ldkNode }) { "Node not running" }
                val invoice = Bolt11Invoice(invoiceStr)
                val paymentId = node.bolt11Payment().send(invoice)
                refreshPayments()
                paymentId.toString()
            }
        }

    /**
     * Sends a BOLT-11 payment, overriding the invoice amount.
     * Use for zero-amount invoices.
     */
    suspend fun sendBolt11WithAmount(invoiceStr: String, amountMsat: ULong): Result<String> =
        withContext(Dispatchers.IO) {
            runCatching {
                val node = checkNotNull(nodeMutex.withLock { ldkNode }) { "Node not running" }
                val invoice = Bolt11Invoice(invoiceStr)
                val paymentId = node.bolt11Payment().sendUsingAmount(invoice, amountMsat)
                refreshPayments()
                paymentId.toString()
            }
        }

    /**
     * Creates a BOLT-11 receive invoice.
     *
     * If an LSP is configured and there is insufficient inbound capacity,
     * ldk-node automatically requests a JIT channel via LSPS2 and returns
     * an invoice that includes the LSP routing hint.
     *
     * @param amountMsat  null = variable-amount invoice (payer chooses amount).
     */
    suspend fun createBolt11Invoice(
        amountMsat: ULong?,
        description: String,
        expirySecs: UInt = DEFAULT_INVOICE_EXPIRY_SECS,
    ): Result<String> =
        withContext(Dispatchers.IO) {
            runCatching {
                val node = checkNotNull(nodeMutex.withLock { ldkNode }) { "Node not running" }
                val bolt11 = node.bolt11Payment()
                val invoice = if (amountMsat != null) {
                    bolt11.receiveViaJitChannel(amountMsat, description, expirySecs, null)
                } else {
                    bolt11.receiveVariableAmountViaJitChannel(description, expirySecs, null)
                }
                invoice.toString()
            }
        }

    /**
     * Creates a reusable BOLT-12 offer.
     * The offer encodes no amount (variable), suitable for tip-jar or donation use.
     */
    suspend fun createBolt12Offer(description: String): Result<String> =
        withContext(Dispatchers.IO) {
            runCatching {
                val node = checkNotNull(nodeMutex.withLock { ldkNode }) { "Node not running" }
                node.bolt12Payment().receiveVariableAmount(description).toString()
            }
        }

    /**
     * Opens a channel to a remote node.
     *
     * Security note: opening a channel without the LSP flag should prompt
     * the user to verify the remote node ID. We never open 0-conf channels
     * to unknown peers; 0-conf is reserved for the configured LSP only
     * (enforced by [Config.trustedPeers0conf]).
     *
     * @param nodeIdHex        Remote node public key (66-char hex string).
     * @param addressStr       Remote peer address ("host:port").
     * @param channelAmountSats  Total channel capacity we will fund.
     * @param pushMsat         Optional initial balance pushed to counterparty.
     */
    suspend fun openChannel(
        nodeIdHex: String,
        addressStr: String,
        channelAmountSats: ULong,
        pushMsat: ULong?,
    ): Result<String> =
        withContext(Dispatchers.IO) {
            runCatching {
                val node = checkNotNull(nodeMutex.withLock { ldkNode }) { "Node not running" }
                val userChannelId = node.openChannel(
                    nodeId = PublicKey(nodeIdHex),
                    address = SocketAddress(addressStr),
                    channelAmountSats = channelAmountSats,
                    pushToCounterpartyMsat = pushMsat,
                    channelConfig = null,
                    announceChannel = false,
                )
                refreshChannels()
                userChannelId.toString()
            }
        }

    /**
     * Initiates a cooperative close of the given channel.
     * The closing transaction will be broadcast once the peer acknowledges.
     */
    suspend fun closeChannel(
        userChannelIdStr: String,
        counterpartyNodeIdHex: String,
    ): Result<Unit> =
        withContext(Dispatchers.IO) {
            runCatching {
                val node = checkNotNull(nodeMutex.withLock { ldkNode }) { "Node not running" }
                node.closeChannel(
                    userChannelId = UserChannelId(userChannelIdStr),
                    counterpartyNodeId = PublicKey(counterpartyNodeIdHex),
                )
                refreshChannels()
            }
        }

    /**
     * Force-closes a channel unilaterally.
     *
     * WARNING: This broadcasts the latest commitment transaction and will
     * trigger a timelock before funds are spendable on-chain. Use only when
     * cooperative close is not possible.
     */
    suspend fun forceCloseChannel(
        userChannelIdStr: String,
        counterpartyNodeIdHex: String,
    ): Result<Unit> =
        withContext(Dispatchers.IO) {
            runCatching {
                val node = checkNotNull(nodeMutex.withLock { ldkNode }) { "Node not running" }
                node.forceCloseChannel(
                    userChannelId = UserChannelId(userChannelIdStr),
                    counterpartyNodeId = PublicKey(counterpartyNodeIdHex),
                    reason = null,
                )
                refreshChannels()
            }
        }

    /** Returns the node's public key as a hex string, or null if not running. */
    suspend fun nodeId(): String? =
        nodeMutex.withLock { ldkNode }?.nodeId()?.toString()

    // ── Type mappers ──────────────────────────────────────────────────────────

    private fun ChannelDetails.toDomain() = LightningChannel(
        channelId = channelId.toString(),
        userChannelId = userChannelId.toString(),
        counterpartyNodeId = counterpartyNodeId.toString(),
        capacitySats = channelValueSats,
        outboundCapacityMsat = outboundCapacityMsat,
        inboundCapacityMsat = inboundCapacityMsat,
        isUsable = isUsable,
        isReady = isChannelReady,
        isPublic = isPublic,
        isOutbound = isOutbound,
        confirmations = confirmations,
        confirmationsRequired = confirmationsRequired,
        nextOutboundHtlcLimitMsat = nextOutboundHtlcLimitMsat,
    )

    private fun PaymentDetails.toDomain() = LightningPayment(
        id = id.toString(),
        amountMsat = amountMsat,
        feesMsat = feesPaidMsat,
        direction = when (direction) {
            LdkPaymentDirection.INBOUND -> PaymentDirection.INBOUND
            LdkPaymentDirection.OUTBOUND -> PaymentDirection.OUTBOUND
        },
        status = when (status) {
            LdkPaymentStatus.PENDING -> PaymentStatus.PENDING
            LdkPaymentStatus.SUCCEEDED -> PaymentStatus.SUCCEEDED
            LdkPaymentStatus.FAILED -> PaymentStatus.FAILED
        },
        type = when (kind) {
            is PaymentKind.Bolt11 -> PaymentType.BOLT11
            is PaymentKind.Bolt11Jit -> PaymentType.BOLT11_JIT
            is PaymentKind.Bolt12Offer -> PaymentType.BOLT12_OFFER
            is PaymentKind.Bolt12Refund -> PaymentType.BOLT12_REFUND
            is PaymentKind.Spontaneous -> PaymentType.SPONTANEOUS
            is PaymentKind.Onchain -> PaymentType.ONCHAIN
        },
        timestampSecs = latestUpdateTimestamp,
        description = null,
        paymentHash = when (val k = kind) {
            is PaymentKind.Bolt11 -> k.hash.toString()
            is PaymentKind.Bolt11Jit -> k.hash.toString()
            is PaymentKind.Bolt12Offer -> k.hash?.toString()
            is PaymentKind.Spontaneous -> k.hash.toString()
            else -> null
        },
        bolt11Invoice = null, // stored separately by ReceivePaymentScreen
    )

    // ── Constants ─────────────────────────────────────────────────────────────

    companion object {
        private const val TAG = "LightningNodeManager"
        private const val POLLING_INTERVAL_MS = 30_000L
        private const val EVENT_LOOP_RETRY_DELAY_MS = 1_000L
        private const val NETWORK_DEBOUNCE_MS = 3_000L
        private const val ANCHOR_RESERVE_SATS = 25_000uL
        private const val DEFAULT_INVOICE_EXPIRY_SECS = 3600u

        /**
         * Floresta's built-in Electrum server (TCP, plaintext, loopback).
         * These are Floresta's compile-time defaults; expose them here so
         * FlorestaService can read them if it ever needs to gate LN startup
         * on Electrum availability.
         */
        fun electrumUrl(network: LdkNetwork): String = when (network) {
            LdkNetwork.BITCOIN -> "127.0.0.1:50001"
            LdkNetwork.TESTNET -> "127.0.0.1:60001"
            LdkNetwork.SIGNET  -> "127.0.0.1:60601"
            LdkNetwork.REGTEST -> "127.0.0.1:60401"
        }
    }
}
