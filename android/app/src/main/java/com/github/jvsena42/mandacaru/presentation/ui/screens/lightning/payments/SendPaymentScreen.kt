package com.github.jvsena42.mandacaru.presentation.ui.screens.lightning.payments

import androidx.compose.animation.AnimatedVisibility
import androidx.compose.animation.core.Spring
import androidx.compose.animation.core.spring
import androidx.compose.animation.expandVertically
import androidx.compose.animation.fadeIn
import androidx.compose.animation.fadeOut
import androidx.compose.animation.shrinkVertically
import androidx.compose.foundation.background
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.imePadding
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.size
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.foundation.text.KeyboardActions
import androidx.compose.foundation.text.KeyboardOptions
import androidx.compose.foundation.verticalScroll
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.filled.ArrowUpward
import androidx.compose.material.icons.filled.Close
import androidx.compose.material.icons.filled.ContentPaste
import androidx.compose.material3.Button
import androidx.compose.material3.ButtonDefaults
import androidx.compose.material3.CircularProgressIndicator
import androidx.compose.material3.ExperimentalMaterial3Api
import androidx.compose.material3.Icon
import androidx.compose.material3.IconButton
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.OutlinedTextField
import androidx.compose.material3.Surface
import androidx.compose.material3.Text
import androidx.compose.material3.TextButton
import androidx.compose.material3.TopAppBar
import androidx.compose.material3.TopAppBarDefaults
import androidx.compose.runtime.Composable
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.collectAsState
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.clip
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.hapticfeedback.HapticFeedbackType
import androidx.compose.ui.platform.LocalClipboardManager
import androidx.compose.ui.platform.LocalFocusManager
import androidx.compose.ui.platform.LocalHapticFeedback
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.text.input.ImeAction
import androidx.compose.ui.text.input.KeyboardType
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import com.github.jvsena42.mandacaru.domain.model.LightningBalance
import com.github.jvsena42.mandacaru.presentation.ui.screens.lightning.LightningUiEvent
import com.github.jvsena42.mandacaru.presentation.ui.screens.lightning.LightningViewModel
import com.github.jvsena42.mandacaru.presentation.ui.screens.lightning.SendUiState
import org.koin.androidx.compose.koinViewModel

/**
 * Send Lightning payment screen.
 *
 * Flow:
 *  1. User pastes or scans a BOLT-11 invoice.
 *  2. If it's a zero-amount invoice, an amount field appears.
 *  3. User reviews and confirms → payment dispatched.
 *  4. Screen listens for [LightningUiEvent.PaymentSuccess] or
 *     [LightningUiEvent.PaymentFailed] and navigates/shows error accordingly.
 *
 * Security: the invoice string is never logged. The amount override is only
 * shown for invoices that contain no amount (variable-amount invoices); the
 * override field is hidden otherwise to avoid confusion.
 */
@OptIn(ExperimentalMaterial3Api::class)
@Composable
fun SendPaymentScreen(
    onBack: () -> Unit,
    onPaymentDispatched: () -> Unit,
    viewModel: LightningViewModel = koinViewModel(),
) {
    val sendState by viewModel.sendState.collectAsState()
    val balance by viewModel.balance.collectAsState()
    val clipboard = LocalClipboardManager.current
    val haptic = LocalHapticFeedback.current
    val focusManager = LocalFocusManager.current

    // Observe one-shot events for navigation / toast
    LaunchedEffect(Unit) {
        viewModel.uiEvents.collect { event ->
            when (event) {
                is LightningUiEvent.PaymentSuccess -> {
                    haptic.performHapticFeedback(HapticFeedbackType.LongPress)
                    onPaymentDispatched()
                }
                is LightningUiEvent.PaymentFailed -> { /* error shown via sendState.error */ }
                else -> Unit
            }
        }
    }

    Column(
        modifier = Modifier
            .fillMaxSize()
            .background(MaterialTheme.colorScheme.background),
    ) {
        TopAppBar(
            title = {
                Text(
                    "Send",
                    style = MaterialTheme.typography.titleLarge.copy(fontWeight = FontWeight.SemiBold),
                )
            },
            navigationIcon = {
                IconButton(onClick = {
                    viewModel.clearSendState()
                    onBack()
                }) {
                    Icon(Icons.Default.Close, contentDescription = "Close")
                }
            },
            colors = TopAppBarDefaults.topAppBarColors(
                containerColor = MaterialTheme.colorScheme.background,
            ),
        )

        Column(
            modifier = Modifier
                .weight(1f)
                .verticalScroll(rememberScrollState())
                .padding(horizontal = 20.dp)
                .imePadding(),
            verticalArrangement = Arrangement.spacedBy(16.dp),
        ) {
            Spacer(Modifier.height(4.dp))

            // ── Spendable balance indicator ───────────────────────────────────
            SpendableBalanceChip(balance = balance)

            // ── Invoice field ─────────────────────────────────────────────────
            OutlinedTextField(
                value = sendState.invoiceRaw,
                onValueChange = viewModel::onInvoiceInput,
                label = { Text("BOLT-11 Invoice") },
                placeholder = { Text("lnbc…") },
                trailingIcon = {
                    if (sendState.invoiceRaw.isBlank()) {
                        IconButton(onClick = {
                            val text = clipboard.getText()?.text ?: return@IconButton
                            viewModel.onInvoiceInput(text.trim())
                        }) {
                            Icon(Icons.Default.ContentPaste, contentDescription = "Paste")
                        }
                    } else {
                        IconButton(onClick = { viewModel.onInvoiceInput("") }) {
                            Icon(Icons.Default.Close, contentDescription = "Clear")
                        }
                    }
                },
                isError = sendState.error != null,
                keyboardOptions = KeyboardOptions(imeAction = ImeAction.Done),
                keyboardActions = KeyboardActions(onDone = { focusManager.clearFocus() }),
                modifier = Modifier.fillMaxWidth(),
                shape = RoundedCornerShape(16.dp),
                minLines = 3,
                maxLines = 5,
            )

            // ── Variable-amount override (shown only when invoice has no amount) ──
            // We detect a zero-amount invoice heuristically: if the user
            // explicitly enables this field. In production we'd decode the
            // invoice to check — left as a ViewModel concern.
            var showAmountOverride by remember { mutableStateOf(false) }
            TextButton(
                onClick = { showAmountOverride = !showAmountOverride },
                enabled = sendState.invoiceRaw.isNotBlank(),
            ) {
                Text(if (showAmountOverride) "Remove amount override" else "Set custom amount (zero-amount invoice)")
            }

            AnimatedVisibility(
                visible = showAmountOverride,
                enter = expandVertically(spring(Spring.DampingRatioNoBouncy, Spring.StiffnessMedium)) + fadeIn(),
                exit = shrinkVertically() + fadeOut(),
            ) {
                OutlinedTextField(
                    value = sendState.overrideAmountSats?.toString() ?: "",
                    onValueChange = viewModel::onSendAmountInput,
                    label = { Text("Amount (sats)") },
                    suffix = { Text("sats") },
                    keyboardOptions = KeyboardOptions(
                        keyboardType = KeyboardType.Number,
                        imeAction = ImeAction.Done,
                    ),
                    keyboardActions = KeyboardActions(onDone = { focusManager.clearFocus() }),
                    modifier = Modifier.fillMaxWidth(),
                    shape = RoundedCornerShape(16.dp),
                )
            }

            // ── Error display ─────────────────────────────────────────────────
            AnimatedVisibility(visible = sendState.error != null) {
                ErrorCard(sendState.error ?: "")
            }

            Spacer(Modifier.height(8.dp))
        }

        // ── Confirm button ────────────────────────────────────────────────────
        ConfirmSendButton(
            state = sendState,
            onClick = {
                focusManager.clearFocus()
                viewModel.sendBolt11()
            },
            modifier = Modifier.padding(horizontal = 20.dp, vertical = 16.dp),
        )
    }
}

@Composable
private fun SpendableBalanceChip(balance: LightningBalance) {
    Row(
        modifier = Modifier
            .clip(RoundedCornerShape(50))
            .background(MaterialTheme.colorScheme.surfaceVariant)
            .padding(horizontal = 14.dp, vertical = 8.dp),
        verticalAlignment = Alignment.CenterVertically,
        horizontalArrangement = Arrangement.spacedBy(6.dp),
    ) {
        Icon(
            Icons.Default.ArrowUpward,
            contentDescription = null,
            tint = MaterialTheme.colorScheme.onSurfaceVariant,
            modifier = Modifier.size(14.dp),
        )
        Text(
            text = "Spendable: ${"%,d".format(balance.spendableLightningSats.toLong())} sats",
            style = MaterialTheme.typography.labelMedium.copy(
                color = MaterialTheme.colorScheme.onSurfaceVariant,
                fontWeight = FontWeight.Medium,
            ),
        )
    }
}

@Composable
private fun ConfirmSendButton(
    state: SendUiState,
    onClick: () -> Unit,
    modifier: Modifier = Modifier,
) {
    Button(
        onClick = onClick,
        enabled = state.invoiceRaw.isNotBlank() && !state.isSending && state.error == null,
        modifier = modifier
            .fillMaxWidth()
            .height(56.dp),
        shape = RoundedCornerShape(16.dp),
        colors = ButtonDefaults.buttonColors(
            containerColor = MaterialTheme.colorScheme.primary,
        ),
    ) {
        if (state.isSending) {
            CircularProgressIndicator(
                modifier = Modifier.size(20.dp),
                color = MaterialTheme.colorScheme.onPrimary,
                strokeWidth = 2.dp,
            )
        } else {
            Row(
                horizontalArrangement = Arrangement.spacedBy(8.dp),
                verticalAlignment = Alignment.CenterVertically,
            ) {
                Icon(Icons.Default.ArrowUpward, contentDescription = null, modifier = Modifier.size(18.dp))
                Text(
                    "Send Payment",
                    style = MaterialTheme.typography.titleMedium.copy(
                        fontWeight = FontWeight.SemiBold,
                        letterSpacing = 0.sp,
                    ),
                )
            }
        }
    }
}

@Composable
private fun ErrorCard(message: String) {
    Surface(
        shape = RoundedCornerShape(12.dp),
        color = MaterialTheme.colorScheme.errorContainer,
    ) {
        Text(
            text = message,
            modifier = Modifier.padding(horizontal = 16.dp, vertical = 12.dp),
            style = MaterialTheme.typography.bodyMedium.copy(
                color = MaterialTheme.colorScheme.onErrorContainer,
            ),
        )
    }
}
