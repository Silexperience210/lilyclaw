package com.github.jvsena42.mandacaru.presentation.ui.components

import androidx.compose.animation.core.Spring
import androidx.compose.animation.core.animateFloatAsState
import androidx.compose.animation.core.spring
import androidx.compose.foundation.background
import androidx.compose.foundation.clickable
import androidx.compose.foundation.interaction.MutableInteractionSource
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.fillMaxHeight
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.size
import androidx.compose.foundation.layout.width
import androidx.compose.runtime.remember
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.runtime.getValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.clip
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.unit.dp
import com.github.jvsena42.mandacaru.domain.model.LightningChannel

/**
 * Visual representation of a channel's balance distribution.
 *
 * The horizontal bar is split into two segments:
 *   • Green (left):  our spendable outbound balance.
 *   • Gray (right):  counterparty's inbound balance.
 *
 * The split point animates with a spring so any payment or splice operation
 * produces a smooth, Apple-quality transition instead of an abrupt jump.
 *
 * The bar height and corner radius are intentionally small (6 dp) to match
 * iOS Health's activity ring segment style — prominent but not dominant.
 */
@Composable
fun ChannelCapacityBar(
    channel: LightningChannel,
    modifier: Modifier = Modifier,
    showLabels: Boolean = true,
) {
    val animatedRatio by animateFloatAsState(
        targetValue = channel.localBalanceRatio.coerceIn(0f, 1f),
        animationSpec = spring(
            dampingRatio = Spring.DampingRatioNoBouncy,
            stiffness = Spring.StiffnessMediumLow,
        ),
        label = "channel_ratio_${channel.channelId}",
    )

    Column(modifier = modifier) {
        if (showLabels) {
            Row(
                modifier = Modifier.fillMaxWidth(),
                verticalAlignment = Alignment.CenterVertically,
            ) {
                Text(
                    text = "Local",
                    style = MaterialTheme.typography.labelSmall.copy(
                        color = MaterialTheme.colorScheme.onSurface.copy(alpha = 0.5f),
                        fontWeight = FontWeight.Medium,
                    ),
                )
                Spacer(Modifier.weight(1f))
                Text(
                    text = "Remote",
                    style = MaterialTheme.typography.labelSmall.copy(
                        color = MaterialTheme.colorScheme.onSurface.copy(alpha = 0.5f),
                        fontWeight = FontWeight.Medium,
                    ),
                )
            }
            Spacer(Modifier.height(4.dp))
        }

        // The bar track
        Box(
            modifier = Modifier
                .fillMaxWidth()
                .height(6.dp)
                .clip(RoundedCornerShape(3.dp))
                .background(MaterialTheme.colorScheme.surfaceVariant),
        ) {
            // Local (green) segment
            Box(
                modifier = Modifier
                    .fillMaxHeight()
                    .fillMaxWidth(animatedRatio)
                    .clip(RoundedCornerShape(topStart = 3.dp, bottomStart = 3.dp))
                    .background(
                        if (channel.isUsable) Color(0xFF30D158) // iOS green
                        else Color(0xFF636366)                   // iOS gray when unusable
                    ),
            )
        }

        if (showLabels) {
            Spacer(Modifier.height(4.dp))
            Row(
                modifier = Modifier.fillMaxWidth(),
                verticalAlignment = Alignment.CenterVertically,
            ) {
                Text(
                    text = "${formatSats(channel.outboundCapacitySats.toLong())} sats",
                    style = MaterialTheme.typography.labelSmall.copy(
                        fontWeight = FontWeight.SemiBold,
                        color = MaterialTheme.colorScheme.onSurface.copy(alpha = 0.8f),
                    ),
                )
                Spacer(Modifier.weight(1f))
                Text(
                    text = "${formatSats(channel.inboundCapacitySats.toLong())} sats",
                    style = MaterialTheme.typography.labelSmall.copy(
                        fontWeight = FontWeight.SemiBold,
                        color = MaterialTheme.colorScheme.onSurface.copy(alpha = 0.8f),
                    ),
                )
            }
        }
    }
}

/**
 * Compact single-line channel row for use in the main Lightning screen.
 * Tapping navigates to ChannelDetailScreen.
 */
@Composable
fun ChannelRowItem(
    channel: LightningChannel,
    onClick: () -> Unit,
    modifier: Modifier = Modifier,
) {
    val interactionSource = remember { MutableInteractionSource() }
    Column(
        modifier = modifier
            .fillMaxWidth()
            .clip(RoundedCornerShape(16.dp))
            .background(MaterialTheme.colorScheme.surface)
            .clickable(
                onClick = onClick,
                indication = null,
                interactionSource = interactionSource,
            )
            .padding(horizontal = 16.dp, vertical = 14.dp),
    ) {
        Row(
            verticalAlignment = Alignment.CenterVertically,
            modifier = Modifier.fillMaxWidth(),
        ) {
            Column(modifier = Modifier.weight(1f)) {
                Text(
                    text = channel.counterpartyNodeId.take(12) + "…",
                    style = MaterialTheme.typography.bodyMedium.copy(
                        fontWeight = FontWeight.SemiBold,
                    ),
                )
                Text(
                    text = "${formatSats(channel.capacitySats.toLong())} sats capacity",
                    style = MaterialTheme.typography.bodySmall.copy(
                        color = MaterialTheme.colorScheme.onSurface.copy(alpha = 0.5f),
                    ),
                )
            }
            Spacer(Modifier.width(12.dp))
            ChannelStatusDot(channel = channel)
        }

        Spacer(Modifier.height(10.dp))

        ChannelCapacityBar(
            channel = channel,
            showLabels = false,
        )
    }
}

@Composable
private fun ChannelStatusDot(channel: LightningChannel) {
    val color = when {
        channel.isPendingOpen -> Color(0xFFFF9F0A) // orange = pending
        channel.isUsable -> Color(0xFF30D158)       // green = active
        channel.isReady -> Color(0xFF636366)         // gray = connected but not usable
        else -> Color(0xFFFF453A)                    // red = error
    }
    Box(
        modifier = Modifier
            .size(8.dp)
            .clip(RoundedCornerShape(4.dp))
            .background(color),
    )
}

private fun formatSats(sats: Long): String = "%,d".format(sats)
