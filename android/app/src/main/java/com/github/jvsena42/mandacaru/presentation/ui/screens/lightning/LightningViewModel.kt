package com.github.jvsena42.mandacaru.presentation.ui.screens.lightning

import androidx.lifecycle.ViewModel
import androidx.lifecycle.viewModelScope
import com.github.jvsena42.mandacaru.data.lightning.LightningRepository
import com.github.jvsena42.mandacaru.data.preferences.LightningPreferencesDataSource
import com.github.jvsena42.mandacaru.domain.model.LightningBalance
import com.github.jvsena42.mandacaru.domain.model.LightningChannel
import com.github.jvsena42.mandacaru.domain.model.LightningNodeEvent
import com.github.jvsena42.mandacaru.domain.model.LightningNodeState
import com.github.jvsena42.mandacaru.domain.model.LightningPayment
import kotlinx.coroutines.flow.MutableSharedFlow
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.SharingStarted
import kotlinx.coroutines.flow.SharedFlow
import kotlinx.coroutines.flow.StateFlow
import kotlinx.coroutines.flow.asSharedFlow
import kotlinx.coroutines.flow.asStateFlow
import kotlinx.coroutines.flow.stateIn
import kotlinx.coroutines.launch

/**
 * ViewModel for the Lightning hub screen and all its nested screens.
 *
 * State is derived entirely from [LightningRepository] and [LightningPreferencesDataSource];
 * no business logic lives here — only mapping between domain types and UI state.
 *
 * ── Send flow ─────────────────────────────────────────────────────────────────
 *
 *   1. User pastes/scans a BOLT-11 invoice.
 *   2. [parseBolt11] validates the invoice and populates [SendUiState].
 *   3. User confirms → [sendBolt11].
 *   4. The result is an optimistic dispatch; final success/failure arrives
 *      via [uiEvents] as [LightningUiEvent.PaymentResult].
 *
 * ── Receive flow ──────────────────────────────────────────────────────────────
 *
 *   1. User enters optional amount + description.
 *   2. [createBolt11Invoice] generates an invoice and populates [ReceiveUiState].
 *   3. [ReceivePaymentScreen] renders the invoice as a QR code.
 *   4. Payment arrival comes via [LightningUiEvent.PaymentReceived].
 *
 * ── Security note ─────────────────────────────────────────────────────────────
 *
 *   Invoices are never logged. [sendBolt11] trims whitespace but does not
 *   modify the invoice string further — any alteration would invalidate the
 *   BOLT-11 signature.
 */
class LightningViewModel(
    private val repository: LightningRepository,
    private val preferencesDataSource: LightningPreferencesDataSource,
) : ViewModel() {

    // ── Observables from repository ───────────────────────────────────────────

    val nodeState: StateFlow<LightningNodeState> = repository.state
        .stateIn(viewModelScope, SharingStarted.WhileSubscribed(5_000), LightningNodeState.Idle)

    val balance: StateFlow<LightningBalance> = repository.balance
        .stateIn(viewModelScope, SharingStarted.WhileSubscribed(5_000), LightningBalance.ZERO)

    val channels: StateFlow<List<LightningChannel>> = repository.channels
        .stateIn(viewModelScope, SharingStarted.WhileSubscribed(5_000), emptyList())

    val payments: StateFlow<List<LightningPayment>> = repository.payments
        .stateIn(viewModelScope, SharingStarted.WhileSubscribed(5_000), emptyList())

    // ── UI-layer events ───────────────────────────────────────────────────────

    private val _uiEvents = MutableSharedFlow<LightningUiEvent>(
        replay = 0,
        extraBufferCapacity = 8,
    )
    val uiEvents: SharedFlow<LightningUiEvent> = _uiEvents.asSharedFlow()

    // ── Send state ────────────────────────────────────────────────────────────

    private val _sendState = MutableStateFlow(SendUiState())
    val sendState: StateFlow<SendUiState> = _sendState.asStateFlow()

    // ── Receive state ─────────────────────────────────────────────────────────

    private val _receiveState = MutableStateFlow(ReceiveUiState())
    val receiveState: StateFlow<ReceiveUiState> = _receiveState.asStateFlow()

    // ── Open channel state ────────────────────────────────────────────────────

    private val _openChannelState = MutableStateFlow(OpenChannelUiState())
    val openChannelState: StateFlow<OpenChannelUiState> = _openChannelState.asStateFlow()

    // ── LN event forwarding ───────────────────────────────────────────────────

    init {
        viewModelScope.launch {
            repository.events.collect { event ->
                when (event) {
                    is LightningNodeEvent.PaymentReceived ->
                        _uiEvents.emit(LightningUiEvent.PaymentReceived(event.amountMsat))

                    is LightningNodeEvent.PaymentSuccessful ->
                        _uiEvents.emit(LightningUiEvent.PaymentSuccess)

                    is LightningNodeEvent.PaymentFailed ->
                        _uiEvents.emit(
                            LightningUiEvent.PaymentFailed(event.reason ?: "Unknown error")
                        )

                    is LightningNodeEvent.ChannelReady ->
                        _uiEvents.emit(LightningUiEvent.ChannelReady(event.channelId))

                    is LightningNodeEvent.ChannelClosed ->
                        _uiEvents.emit(
                            LightningUiEvent.ChannelClosed(event.channelId, event.reason)
                        )

                    else -> Unit
                }
            }
        }
    }

    // ── Node info ─────────────────────────────────────────────────────────────

    fun refreshAll() {
        viewModelScope.launch { repository.refreshAll() }
    }

    // ── Send ──────────────────────────────────────────────────────────────────

    fun onInvoiceInput(raw: String) {
        _sendState.value = _sendState.value.copy(
            invoiceRaw = raw,
            error = null,
        )
    }

    fun onSendAmountInput(satsStr: String) {
        val sats = satsStr.toLongOrNull()
        _sendState.value = _sendState.value.copy(
            overrideAmountSats = sats?.coerceAtLeast(1L),
            error = if (sats != null && sats <= 0) "Amount must be positive" else null,
        )
    }

    fun sendBolt11() {
        val state = _sendState.value
        val invoice = state.invoiceRaw.trim()
        if (invoice.isBlank()) {
            _sendState.value = state.copy(error = "Paste or scan an invoice first")
            return
        }

        _sendState.value = state.copy(isSending = true, error = null)

        viewModelScope.launch {
            val result = if (state.overrideAmountSats != null) {
                repository.sendBolt11WithAmount(invoice, state.overrideAmountSats.toULong() * 1_000u)
            } else {
                repository.sendBolt11(invoice)
            }

            result.fold(
                onSuccess = {
                    _sendState.value = SendUiState() // Reset after dispatch
                    // Final success/failure comes via events
                },
                onFailure = { ex ->
                    _sendState.value = _sendState.value.copy(
                        isSending = false,
                        error = ex.message ?: "Send failed",
                    )
                },
            )
        }
    }

    fun clearSendState() {
        _sendState.value = SendUiState()
    }

    // ── Receive ───────────────────────────────────────────────────────────────

    fun onReceiveAmountInput(satsStr: String) {
        val sats = satsStr.toLongOrNull()
        _receiveState.value = _receiveState.value.copy(
            amountSats = sats?.coerceAtLeast(1L),
            error = if (sats != null && sats <= 0) "Amount must be positive" else null,
        )
    }

    fun onReceiveDescriptionInput(desc: String) {
        _receiveState.value = _receiveState.value.copy(description = desc)
    }

    fun createBolt11Invoice() {
        val state = _receiveState.value
        _receiveState.value = state.copy(isGenerating = true, error = null, invoice = null)

        viewModelScope.launch {
            val amountMsat = state.amountSats?.let { it.toULong() * 1_000u }
            val result = repository.createBolt11Invoice(
                amountMsat = amountMsat,
                description = state.description.ifBlank { "Mandacaru payment" },
                expirySecs = 3600u,
            )

            result.fold(
                onSuccess = { invoice ->
                    _receiveState.value = _receiveState.value.copy(
                        isGenerating = false,
                        invoice = invoice,
                        error = null,
                    )
                },
                onFailure = { ex ->
                    _receiveState.value = _receiveState.value.copy(
                        isGenerating = false,
                        error = ex.message ?: "Failed to generate invoice",
                    )
                },
            )
        }
    }

    fun clearReceiveState() {
        _receiveState.value = ReceiveUiState()
    }

    // ── Open Channel ──────────────────────────────────────────────────────────

    fun onOpenChannelNodeId(nodeId: String) {
        _openChannelState.value = _openChannelState.value.copy(nodeId = nodeId, error = null)
    }

    fun onOpenChannelAddress(address: String) {
        _openChannelState.value = _openChannelState.value.copy(address = address, error = null)
    }

    fun onOpenChannelAmount(satsStr: String) {
        val sats = satsStr.toLongOrNull()
        _openChannelState.value = _openChannelState.value.copy(
            amountSats = sats?.coerceAtLeast(1L),
            error = if (sats != null && sats < MIN_CHANNEL_SATS) {
                "Minimum channel size is ${MIN_CHANNEL_SATS.formatSats()} sats"
            } else null,
        )
    }

    fun openChannel() {
        val state = _openChannelState.value
        if (state.nodeId.isBlank() || state.address.isBlank()) {
            _openChannelState.value = state.copy(error = "Node ID and address are required")
            return
        }
        val sats = state.amountSats ?: run {
            _openChannelState.value = state.copy(error = "Enter a channel amount")
            return
        }
        if (sats < MIN_CHANNEL_SATS) {
            _openChannelState.value = state.copy(
                error = "Minimum channel size is ${MIN_CHANNEL_SATS.formatSats()} sats"
            )
            return
        }

        _openChannelState.value = state.copy(isOpening = true, error = null)

        viewModelScope.launch {
            val result = repository.openChannel(
                nodeIdHex = state.nodeId.trim(),
                addressStr = state.address.trim(),
                channelAmountSats = sats.toULong(),
                pushMsat = null,
            )

            result.fold(
                onSuccess = {
                    _openChannelState.value = OpenChannelUiState(success = true)
                },
                onFailure = { ex ->
                    _openChannelState.value = _openChannelState.value.copy(
                        isOpening = false,
                        error = ex.message ?: "Failed to open channel",
                    )
                },
            )
        }
    }

    // ── Close Channel ─────────────────────────────────────────────────────────

    fun closeChannel(userChannelId: String, counterpartyNodeId: String) {
        viewModelScope.launch {
            val result = repository.closeChannel(userChannelId, counterpartyNodeId)
            result.onFailure { ex ->
                _uiEvents.emit(LightningUiEvent.Error(ex.message ?: "Close channel failed"))
            }
        }
    }

    fun forceCloseChannel(userChannelId: String, counterpartyNodeId: String) {
        viewModelScope.launch {
            val result = repository.forceCloseChannel(userChannelId, counterpartyNodeId)
            result.onFailure { ex ->
                _uiEvents.emit(LightningUiEvent.Error(ex.message ?: "Force close failed"))
            }
        }
    }

    companion object {
        private const val MIN_CHANNEL_SATS = 20_000L
        private fun Long.formatSats() = "%,d".format(this)
    }
}

// ── UI state classes ──────────────────────────────────────────────────────────

data class SendUiState(
    val invoiceRaw: String = "",
    val overrideAmountSats: Long? = null,
    val isSending: Boolean = false,
    val error: String? = null,
)

data class ReceiveUiState(
    val amountSats: Long? = null,
    val description: String = "",
    val isGenerating: Boolean = false,
    val invoice: String? = null,
    val error: String? = null,
)

data class OpenChannelUiState(
    val nodeId: String = "",
    val address: String = "",
    val amountSats: Long? = null,
    val isOpening: Boolean = false,
    val success: Boolean = false,
    val error: String? = null,
)

// ── One-shot UI events ────────────────────────────────────────────────────────

sealed class LightningUiEvent {
    data class PaymentReceived(val amountMsat: ULong) : LightningUiEvent()
    object PaymentSuccess : LightningUiEvent()
    data class PaymentFailed(val reason: String) : LightningUiEvent()
    data class ChannelReady(val channelId: String) : LightningUiEvent()
    data class ChannelClosed(val channelId: String, val reason: String?) : LightningUiEvent()
    data class Error(val message: String) : LightningUiEvent()
}
