package com.github.jvsena42.mandacaru.presentation.ui.screens.lightning

import androidx.compose.animation.AnimatedVisibility
import androidx.compose.animation.fadeIn
import androidx.compose.animation.fadeOut
import androidx.compose.animation.slideInVertically
import androidx.compose.foundation.background
import androidx.compose.foundation.clickable
import androidx.compose.foundation.interaction.MutableInteractionSource
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.PaddingValues
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.size
import androidx.compose.foundation.lazy.LazyColumn
import androidx.compose.foundation.lazy.items
import androidx.compose.foundation.shape.CircleShape
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.filled.Add
import androidx.compose.material.icons.filled.ArrowDownward
import androidx.compose.material.icons.filled.ArrowUpward
import androidx.compose.material3.HorizontalDivider
import androidx.compose.material3.Icon
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.Surface
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.collectAsState
import androidx.compose.runtime.getValue
import androidx.compose.runtime.remember
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.clip
import androidx.compose.ui.hapticfeedback.HapticFeedbackType
import androidx.compose.ui.platform.LocalHapticFeedback
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import com.github.jvsena42.mandacaru.domain.model.LightningNodeState
import com.github.jvsena42.mandacaru.presentation.ui.components.ChannelRowItem
import com.github.jvsena42.mandacaru.presentation.ui.components.LightningBalanceCard
import com.github.jvsena42.mandacaru.presentation.ui.components.PaymentListItem
import org.koin.androidx.compose.koinViewModel

/**
 * Lightning hub — the main tab of the Lightning feature.
 *
 * Layout (top to bottom, scrollable):
 *   ① Balance card      (full-width, gradient)
 *   ② Quick actions     (Send / Receive buttons — iOS-style rounded squares)
 *   ③ Channels section  (up to 3 channels; "See all" navigates to ChannelsScreen)
 *   ④ Recent payments   (last 5; "See all" navigates to payment history)
 *
 * If Lightning has never been set up, it defers to [LightningSetupScreen].
 * While the node is starting, a non-blocking status indicator is shown below
 * the balance card rather than blocking the whole screen.
 */
@Composable
fun LightningScreen(
    onNavigateToSetup: () -> Unit,
    onNavigateToSend: () -> Unit,
    onNavigateToReceive: () -> Unit,
    onNavigateToChannels: () -> Unit,
    onNavigateToChannelDetail: (channelId: String) -> Unit,
    onNavigateToPaymentDetail: (paymentId: String) -> Unit,
    onNavigateToOpenChannel: () -> Unit,
    viewModel: LightningViewModel = koinViewModel(),
) {
    val nodeState by viewModel.nodeState.collectAsState()
    val balance by viewModel.balance.collectAsState()
    val channels by viewModel.channels.collectAsState()
    val payments by viewModel.payments.collectAsState()
    val haptic = LocalHapticFeedback.current

    // Navigate to setup when node has never been configured.
    // The condition is: node is Idle AND we have never completed setup.
    // FlorestaService only starts LN after setup is done, so Idle means
    // either "never set up" or "user disabled LN" — SetupScreen handles both.
    LaunchedEffect(nodeState) {
        if (nodeState is LightningNodeState.Idle) {
            onNavigateToSetup()
        }
    }

    // Haptic feedback for payments received
    LaunchedEffect(Unit) {
        viewModel.uiEvents.collect { event ->
            when (event) {
                is LightningUiEvent.PaymentReceived ->
                    haptic.performHapticFeedback(HapticFeedbackType.LongPress)
                is LightningUiEvent.PaymentSuccess ->
                    haptic.performHapticFeedback(HapticFeedbackType.LongPress)
                else -> Unit
            }
        }
    }

    LazyColumn(
        modifier = Modifier
            .fillMaxSize()
            .background(MaterialTheme.colorScheme.background),
        contentPadding = PaddingValues(horizontal = 20.dp, vertical = 20.dp),
        verticalArrangement = Arrangement.spacedBy(0.dp),
    ) {
        // ── ① Balance card ────────────────────────────────────────────────────
        item(key = "balance_card") {
            LightningBalanceCard(
                balance = balance,
                nodeState = nodeState,
            )
        }

        item(key = "spacer_1") { Spacer(Modifier.height(16.dp)) }

        // ── ② Quick actions ───────────────────────────────────────────────────
        item(key = "quick_actions") {
            Row(
                modifier = Modifier.fillMaxWidth(),
                horizontalArrangement = Arrangement.spacedBy(12.dp),
            ) {
                QuickActionButton(
                    label = "Send",
                    icon = Icons.Default.ArrowUpward,
                    modifier = Modifier.weight(1f),
                    enabled = nodeState.isOperational,
                    onClick = onNavigateToSend,
                )
                QuickActionButton(
                    label = "Receive",
                    icon = Icons.Default.ArrowDownward,
                    modifier = Modifier.weight(1f),
                    enabled = nodeState.isOperational,
                    onClick = onNavigateToReceive,
                )
                QuickActionButton(
                    label = "Channel",
                    icon = Icons.Default.Add,
                    modifier = Modifier.weight(1f),
                    enabled = nodeState.isOperational,
                    onClick = onNavigateToOpenChannel,
                )
            }
        }

        item(key = "spacer_2") { Spacer(Modifier.height(28.dp)) }

        // ── ③ Channels section ────────────────────────────────────────────────
        item(key = "channels_header") {
            SectionHeader(
                title = "Channels",
                actionLabel = if (channels.size > 3) "See all (${channels.size})" else null,
                onAction = onNavigateToChannels,
            )
            Spacer(Modifier.height(10.dp))
        }

        if (channels.isEmpty()) {
            item(key = "no_channels") {
                EmptyStateCard(text = "No channels yet.\nTap + Channel to open your first.")
                Spacer(Modifier.height(28.dp))
            }
        } else {
            items(
                items = channels.take(3),
                key = { it.channelId },
            ) { channel ->
                AnimatedVisibility(
                    visible = true,
                    enter = fadeIn() + slideInVertically(),
                ) {
                    ChannelRowItem(
                        channel = channel,
                        onClick = { onNavigateToChannelDetail(channel.channelId) },
                        modifier = Modifier.padding(bottom = 8.dp),
                    )
                }
            }
            item(key = "channels_spacer") { Spacer(Modifier.height(20.dp)) }
        }

        // ── ④ Recent payments ─────────────────────────────────────────────────
        item(key = "payments_header") {
            SectionHeader(
                title = "Recent",
                actionLabel = if (payments.size > 5) "See all" else null,
                onAction = { /* navigate to full payment history */ },
            )
        }

        if (payments.isEmpty()) {
            item(key = "no_payments") {
                Spacer(Modifier.height(10.dp))
                EmptyStateCard(text = "No payments yet.")
            }
        } else {
            item(key = "payments_card") {
                Spacer(Modifier.height(10.dp))
                Surface(
                    shape = RoundedCornerShape(16.dp),
                    color = MaterialTheme.colorScheme.surface,
                    tonalElevation = 0.5.dp,
                ) {
                    Column {
                        payments.take(5).forEachIndexed { index, payment ->
                            PaymentListItem(
                                payment = payment,
                                onClick = { onNavigateToPaymentDetail(payment.id) },
                            )
                            if (index < payments.take(5).lastIndex) {
                                HorizontalDivider(
                                    modifier = Modifier.padding(horizontal = 20.dp),
                                    thickness = 0.5.dp,
                                    color = MaterialTheme.colorScheme.outlineVariant.copy(alpha = 0.5f),
                                )
                            }
                        }
                    }
                }
            }
        }

        item(key = "bottom_spacer") { Spacer(Modifier.height(32.dp)) }
    }
}

// ── Supporting composables ────────────────────────────────────────────────────

/**
 * iOS-style action button: square-ish with rounded corners, icon + label.
 * The design mirrors Apple Wallet's "Send / Request" quick actions.
 */
@Composable
private fun QuickActionButton(
    label: String,
    icon: androidx.compose.ui.graphics.vector.ImageVector,
    modifier: Modifier = Modifier,
    enabled: Boolean = true,
    onClick: () -> Unit,
) {
    val alpha = if (enabled) 1f else 0.4f
    Column(
        modifier = modifier
            .clip(RoundedCornerShape(16.dp))
            .background(MaterialTheme.colorScheme.primaryContainer.copy(alpha = alpha))
            .clickable(
                enabled = enabled,
                indication = null,
                interactionSource = remember { MutableInteractionSource() },
                onClick = onClick,
            )
            .padding(vertical = 16.dp),
        horizontalAlignment = Alignment.CenterHorizontally,
        verticalArrangement = Arrangement.spacedBy(6.dp),
    ) {
        Box(
            modifier = Modifier
                .size(36.dp)
                .clip(CircleShape)
                .background(MaterialTheme.colorScheme.primary.copy(alpha = alpha)),
            contentAlignment = Alignment.Center,
        ) {
            Icon(
                imageVector = icon,
                contentDescription = label,
                tint = MaterialTheme.colorScheme.onPrimary,
                modifier = Modifier.size(18.dp),
            )
        }
        Text(
            text = label,
            style = MaterialTheme.typography.labelMedium.copy(
                fontWeight = FontWeight.SemiBold,
                color = MaterialTheme.colorScheme.onPrimaryContainer.copy(alpha = alpha),
                letterSpacing = 0.2.sp,
            ),
        )
    }
}

/**
 * Section header with an optional right-side action link.
 * Matches iOS's "grouped list" section header style.
 */
@Composable
private fun SectionHeader(
    title: String,
    actionLabel: String?,
    onAction: (() -> Unit)?,
) {
    Row(
        modifier = Modifier.fillMaxWidth(),
        verticalAlignment = Alignment.CenterVertically,
        horizontalArrangement = Arrangement.SpaceBetween,
    ) {
        Text(
            text = title.uppercase(),
            style = MaterialTheme.typography.labelMedium.copy(
                fontWeight = FontWeight.SemiBold,
                color = MaterialTheme.colorScheme.onBackground.copy(alpha = 0.5f),
                letterSpacing = 0.8.sp,
            ),
        )
        if (actionLabel != null && onAction != null) {
            Text(
                text = actionLabel,
                style = MaterialTheme.typography.labelMedium.copy(
                    color = MaterialTheme.colorScheme.primary,
                    fontWeight = FontWeight.Medium,
                ),
                modifier = Modifier.clickable(
                    indication = null,
                    interactionSource = remember { MutableInteractionSource() },
                    onClick = onAction,
                ),
            )
        }
    }
}

@Composable
private fun EmptyStateCard(text: String) {
    Box(
        modifier = Modifier
            .fillMaxWidth()
            .clip(RoundedCornerShape(16.dp))
            .background(MaterialTheme.colorScheme.surface)
            .padding(24.dp),
        contentAlignment = Alignment.Center,
    ) {
        Text(
            text = text,
            style = MaterialTheme.typography.bodyMedium.copy(
                color = MaterialTheme.colorScheme.onSurface.copy(alpha = 0.4f),
            ),
            textAlign = androidx.compose.ui.text.style.TextAlign.Center,
        )
    }
}
