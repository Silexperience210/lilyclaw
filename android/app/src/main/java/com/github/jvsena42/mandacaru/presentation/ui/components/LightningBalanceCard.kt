package com.github.jvsena42.mandacaru.presentation.ui.components

import androidx.compose.animation.core.Spring
import androidx.compose.animation.core.animateFloatAsState
import androidx.compose.animation.core.spring
import androidx.compose.foundation.background
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.size
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.filled.Bolt
import androidx.compose.material3.Icon
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.runtime.getValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.clip
import androidx.compose.ui.graphics.Brush
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import com.github.jvsena42.mandacaru.domain.model.LightningBalance
import com.github.jvsena42.mandacaru.domain.model.LightningNodeState

/**
 * Full-width balance card inspired by Apple Wallet's card layout.
 *
 * Design principles:
 *   • Gradient background (primary → deep orange) keeps it premium.
 *   • The Lightning bolt icon echoes the app's brand without being cluttered.
 *   • Amounts use a large, tight-tracked typeface — like iOS's SF Pro Display.
 *   • Secondary line (on-chain balance) uses reduced opacity to establish hierarchy.
 *   • The "Syncing…" indicator pulses only while the node is transitioning,
 *     not permanently — avoiding notification fatigue.
 *
 * Animation: the balance amount uses a spring-driven float animation so any
 * change (payment received, channel opened) transitions smoothly rather than
 * jumping. Spring parameters match iOS's default interruptible spring.
 */
@Composable
fun LightningBalanceCard(
    balance: LightningBalance,
    nodeState: LightningNodeState,
    modifier: Modifier = Modifier,
) {
    // Animate the LN balance for a smooth value change effect.
    val animatedLnSats by animateFloatAsState(
        targetValue = balance.totalLightningSats.toFloat(),
        animationSpec = spring(
            dampingRatio = Spring.DampingRatioNoBouncy,
            stiffness = Spring.StiffnessLow,
        ),
        label = "ln_balance",
    )

    Box(
        modifier = modifier
            .fillMaxWidth()
            .clip(RoundedCornerShape(24.dp))
            .background(
                Brush.linearGradient(
                    colors = listOf(
                        MaterialTheme.colorScheme.primary,
                        MaterialTheme.colorScheme.primary.copy(alpha = 0.75f),
                        Color(0xFFBF6A00),
                    )
                )
            )
            .padding(horizontal = 24.dp, vertical = 28.dp)
    ) {
        Column {
            // ── Header ────────────────────────────────────────────────────────
            Row(
                verticalAlignment = Alignment.CenterVertically,
                horizontalArrangement = Arrangement.SpaceBetween,
                modifier = Modifier.fillMaxWidth(),
            ) {
                Row(
                    verticalAlignment = Alignment.CenterVertically,
                    horizontalArrangement = Arrangement.spacedBy(6.dp),
                ) {
                    Icon(
                        imageVector = Icons.Default.Bolt,
                        contentDescription = null,
                        tint = Color.White.copy(alpha = 0.9f),
                        modifier = Modifier.size(18.dp),
                    )
                    Text(
                        text = "Lightning",
                        style = MaterialTheme.typography.labelLarge.copy(
                            color = Color.White.copy(alpha = 0.85f),
                            fontWeight = FontWeight.SemiBold,
                            letterSpacing = 0.3.sp,
                        ),
                    )
                }

                NodeStatusPill(state = nodeState)
            }

            Spacer(Modifier.height(20.dp))

            // ── Main balance ──────────────────────────────────────────────────
            Text(
                text = formatSats(animatedLnSats.toLong()),
                style = MaterialTheme.typography.displayMedium.copy(
                    color = Color.White,
                    fontWeight = FontWeight.Bold,
                    letterSpacing = (-1.5).sp,
                    lineHeight = 52.sp,
                ),
            )
            Text(
                text = "sats",
                style = MaterialTheme.typography.titleMedium.copy(
                    color = Color.White.copy(alpha = 0.65f),
                    fontWeight = FontWeight.Normal,
                    letterSpacing = 0.sp,
                ),
            )

            Spacer(Modifier.height(20.dp))

            // ── Secondary row: on-chain + receivable ──────────────────────────
            Row(
                modifier = Modifier.fillMaxWidth(),
                horizontalArrangement = Arrangement.SpaceBetween,
            ) {
                BalanceSubItem(
                    label = "On-chain",
                    value = formatSats(balance.spendableOnchainSats.toLong()),
                )
                BalanceSubItem(
                    label = "Receivable",
                    value = formatSats(balance.receivableLightningSats.toLong()),
                    alignment = Alignment.End,
                )
            }

            // Pending closing balance (shown only when non-zero)
            if (balance.pendingClosingBalanceSats > 0u) {
                Spacer(Modifier.height(8.dp))
                Text(
                    text = "⏳ ${formatSats(balance.pendingClosingBalanceSats.toLong())} sats pending close",
                    style = MaterialTheme.typography.bodySmall.copy(
                        color = Color.White.copy(alpha = 0.55f),
                        fontWeight = FontWeight.Normal,
                    ),
                )
            }
        }
    }
}

@Composable
private fun BalanceSubItem(
    label: String,
    value: String,
    alignment: Alignment.Horizontal = Alignment.Start,
) {
    Column(horizontalAlignment = alignment) {
        Text(
            text = label.uppercase(),
            style = MaterialTheme.typography.labelSmall.copy(
                color = Color.White.copy(alpha = 0.55f),
                letterSpacing = 0.8.sp,
                fontWeight = FontWeight.Medium,
            ),
        )
        Text(
            text = "$value sats",
            style = MaterialTheme.typography.bodyMedium.copy(
                color = Color.White.copy(alpha = 0.85f),
                fontWeight = FontWeight.SemiBold,
            ),
        )
    }
}

/**
 * Small pill showing the Lightning node's current status.
 * Matches iOS's style of small, rounded, colour-coded status badges.
 */
@Composable
fun NodeStatusPill(state: LightningNodeState) {
    val (text, bg) = when (state) {
        is LightningNodeState.Running -> "Active" to Color(0xFF30D158) // iOS green
        is LightningNodeState.Starting,
        is LightningNodeState.WaitingForBitcoinNode -> "Starting" to Color(0xFFFF9F0A) // iOS orange
        is LightningNodeState.Error -> "Error" to Color(0xFFFF453A) // iOS red
        is LightningNodeState.Stopping -> "Stopping" to Color(0xFF636366) // iOS gray
        is LightningNodeState.Idle -> return
    }

    Box(
        modifier = Modifier
            .clip(RoundedCornerShape(50))
            .background(bg.copy(alpha = 0.2f))
            .padding(horizontal = 10.dp, vertical = 4.dp),
    ) {
        Text(
            text = text,
            style = MaterialTheme.typography.labelSmall.copy(
                color = bg,
                fontWeight = FontWeight.SemiBold,
                letterSpacing = 0.2.sp,
            ),
        )
    }
}

private fun formatSats(sats: Long): String = "%,d".format(sats)
