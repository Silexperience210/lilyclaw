package com.github.jvsena42.mandacaru.presentation.ui.screens.lightning.payments

import androidx.compose.foundation.background
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.size
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.shape.CircleShape
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.foundation.verticalScroll
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.filled.ArrowBack
import androidx.compose.material.icons.filled.ArrowDownward
import androidx.compose.material.icons.filled.ArrowUpward
import androidx.compose.material.icons.filled.HourglassBottom
import androidx.compose.material.icons.filled.Warning
import androidx.compose.material3.ExperimentalMaterial3Api
import androidx.compose.material3.Icon
import androidx.compose.material3.IconButton
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.Surface
import androidx.compose.material3.Text
import androidx.compose.material3.TopAppBar
import androidx.compose.material3.TopAppBarDefaults
import androidx.compose.runtime.Composable
import androidx.compose.runtime.collectAsState
import androidx.compose.runtime.getValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.clip
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.text.font.FontFamily
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.text.style.TextAlign
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import com.github.jvsena42.mandacaru.domain.model.LightningPayment
import com.github.jvsena42.mandacaru.domain.model.PaymentDirection
import com.github.jvsena42.mandacaru.domain.model.PaymentStatus
import com.github.jvsena42.mandacaru.domain.model.PaymentType
import com.github.jvsena42.mandacaru.presentation.ui.screens.lightning.LightningViewModel
import org.koin.androidx.compose.koinViewModel
import java.text.SimpleDateFormat
import java.util.Date
import java.util.Locale

@OptIn(ExperimentalMaterial3Api::class)
@Composable
fun PaymentDetailScreen(
    paymentId: String,
    onBack: () -> Unit,
    viewModel: LightningViewModel = koinViewModel(),
) {
    val payments by viewModel.payments.collectAsState()
    val payment = payments.find { it.id == paymentId }

    Column(
        modifier = Modifier
            .fillMaxSize()
            .background(MaterialTheme.colorScheme.background),
    ) {
        TopAppBar(
            title = { Text("Payment", style = MaterialTheme.typography.titleLarge.copy(fontWeight = FontWeight.SemiBold)) },
            navigationIcon = {
                IconButton(onClick = onBack) {
                    Icon(Icons.Default.ArrowBack, contentDescription = "Back")
                }
            },
            colors = TopAppBarDefaults.topAppBarColors(containerColor = MaterialTheme.colorScheme.background),
        )

        if (payment == null) {
            Column(
                modifier = Modifier.fillMaxSize(),
                horizontalAlignment = Alignment.CenterHorizontally,
                verticalArrangement = Arrangement.Center,
            ) {
                Text("Payment not found", color = MaterialTheme.colorScheme.onBackground.copy(alpha = 0.4f))
            }
        } else {
            PaymentDetailContent(payment = payment)
        }
    }
}

@Composable
private fun PaymentDetailContent(payment: LightningPayment) {
    Column(
        modifier = Modifier
            .fillMaxSize()
            .verticalScroll(rememberScrollState())
            .padding(horizontal = 20.dp),
        horizontalAlignment = Alignment.CenterHorizontally,
    ) {
        Spacer(Modifier.height(16.dp))

        // ── Hero: icon + amount ───────────────────────────────────────────────
        val (icon, iconTint, iconBg) = when {
            payment.status == PaymentStatus.PENDING ->
                Triple(Icons.Default.HourglassBottom, Color(0xFFFF9F0A), Color(0xFFFF9F0A).copy(alpha = 0.15f))
            payment.status == PaymentStatus.FAILED ->
                Triple(Icons.Default.Warning, Color(0xFFFF453A), Color(0xFFFF453A).copy(alpha = 0.12f))
            payment.direction == PaymentDirection.INBOUND ->
                Triple(Icons.Default.ArrowDownward, Color(0xFF30D158), Color(0xFF30D158).copy(alpha = 0.12f))
            else ->
                Triple(Icons.Default.ArrowUpward, MaterialTheme.colorScheme.primary, MaterialTheme.colorScheme.primaryContainer)
        }

        Surface(
            shape = CircleShape,
            color = iconBg,
            modifier = Modifier.size(72.dp),
        ) {
            Icon(
                imageVector = icon,
                contentDescription = null,
                tint = iconTint,
                modifier = Modifier.padding(18.dp),
            )
        }

        Spacer(Modifier.height(16.dp))

        val amountSats = payment.amountSats
        if (amountSats != null) {
            val prefix = if (payment.direction == PaymentDirection.INBOUND) "+" else "−"
            val color = when {
                payment.status == PaymentStatus.FAILED -> MaterialTheme.colorScheme.error
                payment.direction == PaymentDirection.INBOUND -> Color(0xFF30D158)
                else -> MaterialTheme.colorScheme.onBackground
            }
            Text(
                text = "$prefix${"%,d".format(amountSats.toLong())}",
                style = MaterialTheme.typography.displaySmall.copy(
                    fontWeight = FontWeight.Bold,
                    letterSpacing = (-1).sp,
                    color = color,
                ),
                textAlign = TextAlign.Center,
            )
            Text(
                text = "sats",
                style = MaterialTheme.typography.titleMedium.copy(
                    color = MaterialTheme.colorScheme.onBackground.copy(alpha = 0.5f),
                    fontWeight = FontWeight.Normal,
                ),
            )
        }

        Spacer(Modifier.height(8.dp))

        StatusBadge(payment.status)

        Spacer(Modifier.height(28.dp))

        // ── Detail rows ───────────────────────────────────────────────────────
        Surface(
            shape = RoundedCornerShape(16.dp),
            color = MaterialTheme.colorScheme.surface,
            modifier = Modifier.fillMaxWidth(),
        ) {
            Column {
                if (payment.description != null) {
                    DetailRow("Description", payment.description)
                    RowDivider()
                }
                DetailRow("Type", payment.type.displayName())
                RowDivider()
                DetailRow("Direction", if (payment.direction == PaymentDirection.INBOUND) "Received" else "Sent")
                RowDivider()
                if (payment.feesMsat > 0u) {
                    DetailRow("Fees", "${"%,d".format(payment.feesSats.toLong())} sats")
                    RowDivider()
                }
                DetailRow(
                    "Date",
                    SimpleDateFormat("d MMM yyyy, HH:mm:ss", Locale.getDefault())
                        .format(Date(payment.timestampSecs.toLong() * 1_000)),
                )
                if (payment.paymentHash != null) {
                    RowDivider()
                    DetailRow(
                        label = "Payment hash",
                        value = payment.paymentHash,
                        monospace = true,
                        maxLines = 3,
                    )
                }
            }
        }

        Spacer(Modifier.height(32.dp))
    }
}

@Composable
private fun DetailRow(
    label: String,
    value: String,
    monospace: Boolean = false,
    maxLines: Int = 1,
) {
    Row(
        modifier = Modifier
            .fillMaxWidth()
            .padding(horizontal = 16.dp, vertical = 14.dp),
        horizontalArrangement = Arrangement.SpaceBetween,
        verticalAlignment = Alignment.Top,
    ) {
        Text(
            text = label,
            style = MaterialTheme.typography.bodyMedium.copy(
                color = MaterialTheme.colorScheme.onSurface.copy(alpha = 0.55f),
            ),
            modifier = Modifier.weight(0.4f),
        )
        Text(
            text = value,
            style = MaterialTheme.typography.bodyMedium.copy(
                fontWeight = FontWeight.Medium,
                fontFamily = if (monospace) FontFamily.Monospace else FontFamily.Default,
            ),
            textAlign = TextAlign.End,
            maxLines = maxLines,
            modifier = Modifier.weight(0.6f),
        )
    }
}

@Composable
private fun RowDivider() {
    androidx.compose.material3.HorizontalDivider(
        modifier = Modifier.padding(horizontal = 16.dp),
        thickness = 0.5.dp,
        color = MaterialTheme.colorScheme.outlineVariant.copy(alpha = 0.4f),
    )
}

@Composable
private fun StatusBadge(status: PaymentStatus) {
    val (text, bg, fg) = when (status) {
        PaymentStatus.SUCCEEDED -> Triple("Completed", Color(0xFF30D158).copy(alpha = 0.15f), Color(0xFF30D158))
        PaymentStatus.PENDING -> Triple("Pending", Color(0xFFFF9F0A).copy(alpha = 0.15f), Color(0xFFFF9F0A))
        PaymentStatus.FAILED -> Triple("Failed", Color(0xFFFF453A).copy(alpha = 0.12f), Color(0xFFFF453A))
    }
    Surface(
        shape = RoundedCornerShape(50),
        color = bg,
    ) {
        Text(
            text = text,
            modifier = Modifier.padding(horizontal = 14.dp, vertical = 6.dp),
            style = MaterialTheme.typography.labelMedium.copy(
                color = fg,
                fontWeight = FontWeight.SemiBold,
            ),
        )
    }
}

private fun PaymentType.displayName() = when (this) {
    PaymentType.BOLT11 -> "Lightning (BOLT-11)"
    PaymentType.BOLT11_JIT -> "Lightning JIT (BOLT-11)"
    PaymentType.BOLT12_OFFER -> "Lightning Offer (BOLT-12)"
    PaymentType.BOLT12_REFUND -> "Lightning Refund (BOLT-12)"
    PaymentType.SPONTANEOUS -> "Spontaneous (keysend)"
    PaymentType.ONCHAIN -> "On-chain"
}
