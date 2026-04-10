package com.github.jvsena42.mandacaru.presentation.ui.components

import androidx.compose.foundation.clickable
import androidx.compose.foundation.interaction.MutableInteractionSource
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.size
import androidx.compose.foundation.layout.width
import androidx.compose.foundation.shape.CircleShape
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.filled.ArrowDownward
import androidx.compose.material.icons.filled.ArrowUpward
import androidx.compose.material.icons.filled.HourglassBottom
import androidx.compose.material.icons.filled.Warning
import androidx.compose.material3.Icon
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.Surface
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.runtime.remember
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.graphics.vector.ImageVector
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.unit.dp
import com.github.jvsena42.mandacaru.domain.model.LightningPayment
import com.github.jvsena42.mandacaru.domain.model.PaymentDirection
import com.github.jvsena42.mandacaru.domain.model.PaymentStatus
import java.text.SimpleDateFormat
import java.util.Date
import java.util.Locale

/**
 * Single payment row — Apple Wallet / iOS Contacts style.
 *
 * Layout:
 *   [Icon circle]  [Description / hash abbrev]  [+/- amount]
 *                  [Timestamp / status]
 *
 * The icon circle uses iOS semantic colors:
 *   • Green arrow down  = received (success)
 *   • Orange arrow up   = sent (success)
 *   • Gray hourglass    = pending
 *   • Red warning       = failed
 *
 * The amount shows a signed value with color:
 *   • Received: green "+5,000"
 *   • Sent:     primary "-2,500 (fees: 1)"
 */
@Composable
fun PaymentListItem(
    payment: LightningPayment,
    onClick: () -> Unit,
    modifier: Modifier = Modifier,
) {
    val (icon, iconTint, iconBg) = paymentVisuals(payment)
    val amountText = buildAmountText(payment)
    val amountColor = when {
        payment.status == PaymentStatus.FAILED -> MaterialTheme.colorScheme.error
        payment.direction == PaymentDirection.INBOUND -> Color(0xFF30D158)
        else -> MaterialTheme.colorScheme.onSurface
    }

    Row(
        modifier = modifier
            .fillMaxWidth()
            .clickable(
                onClick = onClick,
                indication = null,
                interactionSource = remember { MutableInteractionSource() },
            )
            .padding(horizontal = 20.dp, vertical = 12.dp),
        verticalAlignment = Alignment.CenterVertically,
    ) {
        // Icon circle
        Surface(
            shape = CircleShape,
            color = iconBg,
            modifier = Modifier.size(40.dp),
        ) {
            Icon(
                imageVector = icon,
                contentDescription = null,
                tint = iconTint,
                modifier = Modifier.padding(10.dp),
            )
        }

        Spacer(Modifier.width(14.dp))

        // Description + timestamp
        Column(modifier = Modifier.weight(1f)) {
            Text(
                text = paymentLabel(payment),
                style = MaterialTheme.typography.bodyMedium.copy(
                    fontWeight = FontWeight.Medium,
                ),
                maxLines = 1,
            )
            Text(
                text = formatTimestamp(payment.timestampSecs),
                style = MaterialTheme.typography.bodySmall.copy(
                    color = MaterialTheme.colorScheme.onSurface.copy(alpha = 0.5f),
                ),
            )
        }

        Spacer(Modifier.width(8.dp))

        // Amount column
        Column(horizontalAlignment = Alignment.End) {
            Text(
                text = amountText,
                style = MaterialTheme.typography.bodyMedium.copy(
                    fontWeight = FontWeight.SemiBold,
                    color = amountColor,
                ),
            )
            if (payment.feesMsat > 0u && payment.direction == PaymentDirection.OUTBOUND
                && payment.status == PaymentStatus.SUCCEEDED
            ) {
                Text(
                    text = "fee ${formatSats(payment.feesSats.toLong())}",
                    style = MaterialTheme.typography.bodySmall.copy(
                        color = MaterialTheme.colorScheme.onSurface.copy(alpha = 0.4f),
                    ),
                )
            }
        }
    }
}

private data class PaymentVisuals(
    val icon: ImageVector,
    val tint: Color,
    val bg: Color,
)

@Composable
private fun paymentVisuals(payment: LightningPayment): PaymentVisuals = when {
    payment.status == PaymentStatus.PENDING ->
        PaymentVisuals(
            Icons.Default.HourglassBottom,
            Color(0xFFFF9F0A),
            Color(0xFFFF9F0A).copy(alpha = 0.15f),
        )
    payment.status == PaymentStatus.FAILED ->
        PaymentVisuals(
            Icons.Default.Warning,
            MaterialTheme.colorScheme.error,
            MaterialTheme.colorScheme.error.copy(alpha = 0.12f),
        )
    payment.direction == PaymentDirection.INBOUND ->
        PaymentVisuals(
            Icons.Default.ArrowDownward,
            Color(0xFF30D158),
            Color(0xFF30D158).copy(alpha = 0.12f),
        )
    else ->
        PaymentVisuals(
            Icons.Default.ArrowUpward,
            MaterialTheme.colorScheme.primary,
            MaterialTheme.colorScheme.primary.copy(alpha = 0.12f),
        )
}

private fun paymentLabel(payment: LightningPayment): String {
    payment.description?.takeIf { it.isNotBlank() }?.let { return it }
    return when (payment.direction) {
        PaymentDirection.INBOUND -> "Received"
        PaymentDirection.OUTBOUND -> "Sent"
    }
}

private fun buildAmountText(payment: LightningPayment): String {
    val sats = payment.amountSats ?: return "—"
    return when (payment.direction) {
        PaymentDirection.INBOUND -> "+${formatSats(sats.toLong())}"
        PaymentDirection.OUTBOUND -> "-${formatSats(sats.toLong())}"
    }
}

private val dateFormat = SimpleDateFormat("d MMM, HH:mm", Locale.getDefault())

private fun formatTimestamp(epochSecs: ULong): String =
    dateFormat.format(Date(epochSecs.toLong() * 1_000))

private fun formatSats(sats: Long): String = "%,d".format(sats)
