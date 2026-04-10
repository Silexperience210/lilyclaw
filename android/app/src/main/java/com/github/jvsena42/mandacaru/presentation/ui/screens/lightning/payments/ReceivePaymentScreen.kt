package com.github.jvsena42.mandacaru.presentation.ui.screens.lightning.payments

import android.graphics.Bitmap
import android.graphics.Color as AndroidColor
import androidx.compose.animation.AnimatedContent
import androidx.compose.animation.fadeIn
import androidx.compose.animation.fadeOut
import androidx.compose.animation.togetherWith
import androidx.compose.foundation.Image
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
import androidx.compose.material.icons.filled.Close
import androidx.compose.material.icons.filled.ContentCopy
import androidx.compose.material.icons.filled.Share
import androidx.compose.material3.Button
import androidx.compose.material3.ButtonDefaults
import androidx.compose.material3.CircularProgressIndicator
import androidx.compose.material3.ExperimentalMaterial3Api
import androidx.compose.material3.Icon
import androidx.compose.material3.IconButton
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.OutlinedButton
import androidx.compose.material3.OutlinedTextField
import androidx.compose.material3.Surface
import androidx.compose.material3.Text
import androidx.compose.material3.TopAppBar
import androidx.compose.material3.TopAppBarDefaults
import androidx.compose.runtime.Composable
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.collectAsState
import androidx.compose.runtime.getValue
import androidx.compose.runtime.remember
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.clip
import androidx.compose.ui.graphics.asImageBitmap
import androidx.compose.ui.hapticfeedback.HapticFeedbackType
import androidx.compose.ui.platform.LocalClipboardManager
import androidx.compose.ui.platform.LocalContext
import androidx.compose.ui.platform.LocalFocusManager
import androidx.compose.ui.platform.LocalHapticFeedback
import androidx.compose.ui.text.AnnotatedString
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.text.input.ImeAction
import androidx.compose.ui.text.input.KeyboardType
import androidx.compose.ui.text.style.TextAlign
import androidx.compose.ui.text.style.TextOverflow
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import com.github.jvsena42.mandacaru.presentation.ui.screens.lightning.LightningUiEvent
import com.github.jvsena42.mandacaru.presentation.ui.screens.lightning.LightningViewModel
import com.github.jvsena42.mandacaru.presentation.ui.screens.lightning.ReceiveUiState
import com.google.zxing.BarcodeFormat
import com.google.zxing.EncodeHintType
import com.google.zxing.qrcode.QRCodeWriter
import org.koin.androidx.compose.koinViewModel

/**
 * Receive Lightning payment screen.
 *
 * Flow:
 *  1. User enters optional amount + description.
 *  2. Tap "Generate Invoice" → ldk-node creates a BOLT-11.
 *     If the node has insufficient inbound capacity AND an LSP is configured,
 *     ldk-node transparently requests a JIT channel via LSPS2 and embeds the
 *     LSP routing hint in the invoice.
 *  3. Invoice is displayed as a QR code + copyable text.
 *  4. When payment arrives, a haptic pulse confirms receipt.
 *
 * Security: FLAG_SECURE is intentionally NOT set here because the invoice is
 * not a secret — only the payment preimage is sensitive, and it lives in ldk-node.
 */
@OptIn(ExperimentalMaterial3Api::class)
@Composable
fun ReceivePaymentScreen(
    onBack: () -> Unit,
    viewModel: LightningViewModel = koinViewModel(),
) {
    val receiveState by viewModel.receiveState.collectAsState()
    val clipboard = LocalClipboardManager.current
    val haptic = LocalHapticFeedback.current
    val focusManager = LocalFocusManager.current
    val context = LocalContext.current

    // Haptic feedback when payment is received while the screen is open
    LaunchedEffect(Unit) {
        viewModel.uiEvents.collect { event ->
            if (event is LightningUiEvent.PaymentReceived) {
                haptic.performHapticFeedback(HapticFeedbackType.LongPress)
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
                    "Receive",
                    style = MaterialTheme.typography.titleLarge.copy(fontWeight = FontWeight.SemiBold),
                )
            },
            navigationIcon = {
                IconButton(onClick = {
                    viewModel.clearReceiveState()
                    onBack()
                }) {
                    Icon(Icons.Default.Close, contentDescription = "Close")
                }
            },
            colors = TopAppBarDefaults.topAppBarColors(
                containerColor = MaterialTheme.colorScheme.background,
            ),
        )

        AnimatedContent(
            targetState = receiveState.invoice != null,
            transitionSpec = { fadeIn() togetherWith fadeOut() },
            modifier = Modifier.weight(1f),
            label = "receive_content",
        ) { hasInvoice ->
            if (hasInvoice) {
                InvoiceDisplay(
                    invoice = receiveState.invoice!!,
                    onCopy = {
                        clipboard.setText(AnnotatedString(receiveState.invoice!!))
                        haptic.performHapticFeedback(HapticFeedbackType.LongPress)
                    },
                    onShare = {
                        val sendIntent = android.content.Intent(android.content.Intent.ACTION_SEND).apply {
                            putExtra(android.content.Intent.EXTRA_TEXT, receiveState.invoice)
                            type = "text/plain"
                        }
                        context.startActivity(android.content.Intent.createChooser(sendIntent, null))
                    },
                    onNewInvoice = { viewModel.clearReceiveState() },
                )
            } else {
                InvoiceForm(
                    state = receiveState,
                    onAmountChange = viewModel::onReceiveAmountInput,
                    onDescriptionChange = viewModel::onReceiveDescriptionInput,
                    onGenerate = {
                        focusManager.clearFocus()
                        viewModel.createBolt11Invoice()
                    },
                )
            }
        }
    }
}

// ── Invoice form (before generation) ─────────────────────────────────────────

@Composable
private fun InvoiceForm(
    state: ReceiveUiState,
    onAmountChange: (String) -> Unit,
    onDescriptionChange: (String) -> Unit,
    onGenerate: () -> Unit,
) {
    val focusManager = LocalFocusManager.current

    Column(
        modifier = Modifier
            .fillMaxSize()
            .verticalScroll(rememberScrollState())
            .padding(horizontal = 20.dp)
            .imePadding(),
        verticalArrangement = Arrangement.spacedBy(16.dp),
    ) {
        Spacer(Modifier.height(8.dp))

        OutlinedTextField(
            value = state.amountSats?.toString() ?: "",
            onValueChange = onAmountChange,
            label = { Text("Amount (optional)") },
            placeholder = { Text("Any amount") },
            suffix = { Text("sats") },
            keyboardOptions = KeyboardOptions(
                keyboardType = KeyboardType.Number,
                imeAction = ImeAction.Next,
            ),
            modifier = Modifier.fillMaxWidth(),
            shape = RoundedCornerShape(16.dp),
        )

        OutlinedTextField(
            value = state.description,
            onValueChange = onDescriptionChange,
            label = { Text("Description (optional)") },
            placeholder = { Text("Coffee, donation…") },
            keyboardOptions = KeyboardOptions(imeAction = ImeAction.Done),
            keyboardActions = KeyboardActions(onDone = { focusManager.clearFocus() }),
            modifier = Modifier.fillMaxWidth(),
            shape = RoundedCornerShape(16.dp),
        )

        // Show LSP notice only when amount is provided (JIT channel may apply)
        if (state.amountSats != null) {
            Surface(
                shape = RoundedCornerShape(12.dp),
                color = MaterialTheme.colorScheme.secondaryContainer.copy(alpha = 0.5f),
            ) {
                Text(
                    text = "ℹ If you don't have enough inbound liquidity, your LSP will open a JIT channel. A small routing fee will be deducted from the received amount.",
                    modifier = Modifier.padding(14.dp),
                    style = MaterialTheme.typography.bodySmall.copy(
                        color = MaterialTheme.colorScheme.onSecondaryContainer,
                    ),
                )
            }
        }

        if (state.error != null) {
            ErrorCard(state.error)
        }

        Spacer(Modifier.weight(1f))

        Button(
            onClick = onGenerate,
            enabled = !state.isGenerating,
            modifier = Modifier
                .fillMaxWidth()
                .height(56.dp)
                .padding(bottom = 16.dp),
            shape = RoundedCornerShape(16.dp),
        ) {
            if (state.isGenerating) {
                CircularProgressIndicator(
                    modifier = Modifier.size(20.dp),
                    color = MaterialTheme.colorScheme.onPrimary,
                    strokeWidth = 2.dp,
                )
            } else {
                Text(
                    "Generate Invoice",
                    style = MaterialTheme.typography.titleMedium.copy(
                        fontWeight = FontWeight.SemiBold,
                        letterSpacing = 0.sp,
                    ),
                )
            }
        }
    }
}

// ── Invoice display (after generation) ───────────────────────────────────────

@Composable
private fun InvoiceDisplay(
    invoice: String,
    onCopy: () -> Unit,
    onShare: () -> Unit,
    onNewInvoice: () -> Unit,
) {
    val qrBitmap = remember(invoice) { generateQrBitmap(invoice) }

    Column(
        modifier = Modifier
            .fillMaxSize()
            .verticalScroll(rememberScrollState())
            .padding(horizontal = 20.dp),
        horizontalAlignment = Alignment.CenterHorizontally,
        verticalArrangement = Arrangement.spacedBy(20.dp),
    ) {
        Spacer(Modifier.height(8.dp))

        // QR Code
        if (qrBitmap != null) {
            Box(
                modifier = Modifier
                    .size(260.dp)
                    .clip(RoundedCornerShape(20.dp))
                    .background(androidx.compose.ui.graphics.Color.White)
                    .padding(16.dp),
                contentAlignment = Alignment.Center,
            ) {
                Image(
                    bitmap = qrBitmap.asImageBitmap(),
                    contentDescription = "Payment QR code",
                    modifier = Modifier.fillMaxSize(),
                )
            }
        }

        // Truncated invoice string
        Surface(
            shape = RoundedCornerShape(14.dp),
            color = MaterialTheme.colorScheme.surfaceVariant,
            modifier = Modifier.fillMaxWidth(),
        ) {
            Text(
                text = invoice,
                modifier = Modifier.padding(14.dp),
                style = MaterialTheme.typography.bodySmall.copy(
                    fontFamily = androidx.compose.ui.text.font.FontFamily.Monospace,
                    color = MaterialTheme.colorScheme.onSurfaceVariant,
                ),
                maxLines = 3,
                overflow = TextOverflow.Ellipsis,
            )
        }

        // Action row
        Row(
            modifier = Modifier.fillMaxWidth(),
            horizontalArrangement = Arrangement.spacedBy(12.dp),
        ) {
            Button(
                onClick = onCopy,
                modifier = Modifier.weight(1f).height(48.dp),
                shape = RoundedCornerShape(14.dp),
            ) {
                Icon(Icons.Default.ContentCopy, contentDescription = null, modifier = Modifier.size(16.dp))
                Spacer(Modifier.size(6.dp))
                Text("Copy", fontWeight = FontWeight.SemiBold)
            }
            OutlinedButton(
                onClick = onShare,
                modifier = Modifier.weight(1f).height(48.dp),
                shape = RoundedCornerShape(14.dp),
            ) {
                Icon(Icons.Default.Share, contentDescription = null, modifier = Modifier.size(16.dp))
                Spacer(Modifier.size(6.dp))
                Text("Share", fontWeight = FontWeight.SemiBold)
            }
        }

        Text(
            text = "Waiting for payment…",
            style = MaterialTheme.typography.bodyMedium.copy(
                color = MaterialTheme.colorScheme.onBackground.copy(alpha = 0.5f),
            ),
            textAlign = TextAlign.Center,
        )

        OutlinedButton(
            onClick = onNewInvoice,
            modifier = Modifier.fillMaxWidth().height(48.dp),
            shape = RoundedCornerShape(14.dp),
        ) {
            Text("New Invoice", fontWeight = FontWeight.Medium)
        }

        Spacer(Modifier.height(16.dp))
    }
}

// ── QR generation ─────────────────────────────────────────────────────────────

private fun generateQrBitmap(content: String): Bitmap? = runCatching {
    val writer = QRCodeWriter()
    val hints = mapOf(EncodeHintType.MARGIN to 0)
    val matrix = writer.encode(content.uppercase(), BarcodeFormat.QR_CODE, 512, 512, hints)
    val size = matrix.width
    val bitmap = Bitmap.createBitmap(size, size, Bitmap.Config.ARGB_8888)
    for (x in 0 until size) {
        for (y in 0 until size) {
            bitmap.setPixel(
                x, y,
                if (matrix[x, y]) AndroidColor.BLACK else AndroidColor.WHITE,
            )
        }
    }
    bitmap
}.getOrNull()

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
