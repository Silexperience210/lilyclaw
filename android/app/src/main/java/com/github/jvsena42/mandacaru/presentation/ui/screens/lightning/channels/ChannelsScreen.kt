package com.github.jvsena42.mandacaru.presentation.ui.screens.lightning.channels

import androidx.compose.animation.AnimatedVisibility
import androidx.compose.animation.fadeIn
import androidx.compose.animation.slideInVertically
import androidx.compose.foundation.background
import androidx.compose.foundation.layout.Arrangement
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
import androidx.compose.foundation.lazy.itemsIndexed
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.filled.Add
import androidx.compose.material.icons.filled.ArrowBack
import androidx.compose.material3.ExperimentalMaterial3Api
import androidx.compose.material3.FloatingActionButton
import androidx.compose.material3.FloatingActionButtonDefaults
import androidx.compose.material3.Icon
import androidx.compose.material3.IconButton
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.Scaffold
import androidx.compose.material3.Surface
import androidx.compose.material3.Text
import androidx.compose.material3.TopAppBar
import androidx.compose.material3.TopAppBarDefaults
import androidx.compose.runtime.Composable
import androidx.compose.runtime.collectAsState
import androidx.compose.runtime.getValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.text.style.TextAlign
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import com.github.jvsena42.mandacaru.domain.model.LightningChannel
import com.github.jvsena42.mandacaru.presentation.ui.components.ChannelCapacityBar
import com.github.jvsena42.mandacaru.presentation.ui.screens.lightning.LightningViewModel
import org.koin.androidx.compose.koinViewModel

/**
 * Full channel list screen.
 *
 * Each card shows:
 *   • Counterparty node ID (abbreviated)
 *   • Capacity + status pill
 *   • Local/remote capacity bar
 *   • Tap → ChannelDetailScreen
 *
 * The FAB opens [OpenChannelScreen].
 */
@OptIn(ExperimentalMaterial3Api::class)
@Composable
fun ChannelsScreen(
    onBack: () -> Unit,
    onNavigateToChannelDetail: (channelId: String) -> Unit,
    onNavigateToOpenChannel: () -> Unit,
    viewModel: LightningViewModel = koinViewModel(),
) {
    val channels by viewModel.channels.collectAsState()

    Scaffold(
        topBar = {
            TopAppBar(
                title = {
                    Text(
                        "Channels (${channels.size})",
                        style = MaterialTheme.typography.titleLarge.copy(fontWeight = FontWeight.SemiBold),
                    )
                },
                navigationIcon = {
                    IconButton(onClick = onBack) {
                        Icon(Icons.Default.ArrowBack, contentDescription = "Back")
                    }
                },
                colors = TopAppBarDefaults.topAppBarColors(
                    containerColor = MaterialTheme.colorScheme.background,
                ),
            )
        },
        floatingActionButton = {
            FloatingActionButton(
                onClick = onNavigateToOpenChannel,
                shape = RoundedCornerShape(16.dp),
                containerColor = MaterialTheme.colorScheme.primary,
                elevation = FloatingActionButtonDefaults.elevation(defaultElevation = 2.dp),
            ) {
                Icon(Icons.Default.Add, contentDescription = "Open channel")
            }
        },
        containerColor = MaterialTheme.colorScheme.background,
    ) { innerPadding ->
        if (channels.isEmpty()) {
            Column(
                modifier = Modifier
                    .fillMaxSize()
                    .padding(innerPadding),
                horizontalAlignment = Alignment.CenterHorizontally,
                verticalArrangement = Arrangement.Center,
            ) {
                Text(
                    "No channels yet.\nTap + to open your first.",
                    style = MaterialTheme.typography.bodyLarge.copy(
                        color = MaterialTheme.colorScheme.onBackground.copy(alpha = 0.4f),
                    ),
                    textAlign = TextAlign.Center,
                )
            }
        } else {
            LazyColumn(
                modifier = Modifier
                    .fillMaxSize()
                    .padding(innerPadding),
                contentPadding = PaddingValues(horizontal = 20.dp, vertical = 12.dp),
                verticalArrangement = Arrangement.spacedBy(10.dp),
            ) {
                itemsIndexed(
                    items = channels,
                    key = { _, ch -> ch.channelId },
                ) { index, channel ->
                    AnimatedVisibility(
                        visible = true,
                        enter = fadeIn() + slideInVertically(initialOffsetY = { it / 4 }),
                    ) {
                        ChannelCard(
                            channel = channel,
                            onClick = { onNavigateToChannelDetail(channel.channelId) },
                        )
                    }
                }
                item { Spacer(Modifier.height(80.dp)) } // FAB clearance
            }
        }
    }
}

@Composable
private fun ChannelCard(
    channel: LightningChannel,
    onClick: () -> Unit,
) {
    Surface(
        onClick = onClick,
        shape = RoundedCornerShape(18.dp),
        color = MaterialTheme.colorScheme.surface,
        tonalElevation = 0.5.dp,
        modifier = Modifier.fillMaxWidth(),
    ) {
        Column(modifier = Modifier.padding(16.dp)) {
            Row(
                modifier = Modifier.fillMaxWidth(),
                horizontalArrangement = Arrangement.SpaceBetween,
                verticalAlignment = Alignment.CenterVertically,
            ) {
                Column(modifier = Modifier.weight(1f)) {
                    Text(
                        text = channel.counterpartyNodeId.take(16) + "…",
                        style = MaterialTheme.typography.bodyMedium.copy(
                            fontWeight = FontWeight.SemiBold,
                        ),
                    )
                    Spacer(Modifier.height(2.dp))
                    Text(
                        text = "${"%,d".format(channel.capacitySats.toLong())} sats total",
                        style = MaterialTheme.typography.bodySmall.copy(
                            color = MaterialTheme.colorScheme.onSurface.copy(alpha = 0.5f),
                        ),
                    )
                }
                ChannelStatusPill(channel = channel)
            }

            Spacer(Modifier.height(14.dp))

            ChannelCapacityBar(channel = channel, showLabels = true)

            // Pending-open progress indicator
            if (channel.isPendingOpen) {
                Spacer(Modifier.height(10.dp))
                val remaining = channel.confirmationsRemaining
                Text(
                    text = if (remaining != null && remaining > 0u)
                        "⏳ $remaining confirmations remaining"
                    else "⏳ Awaiting confirmation",
                    style = MaterialTheme.typography.labelSmall.copy(
                        color = Color(0xFFFF9F0A),
                        fontWeight = FontWeight.Medium,
                    ),
                )
            }
        }
    }
}

@Composable
private fun ChannelStatusPill(channel: LightningChannel) {
    val (text, fg, bg) = when {
        channel.isPendingOpen -> Triple("Pending", Color(0xFFFF9F0A), Color(0xFFFF9F0A).copy(0.15f))
        channel.isUsable -> Triple("Active", Color(0xFF30D158), Color(0xFF30D158).copy(0.12f))
        channel.isReady -> Triple("Connected", MaterialTheme.colorScheme.primary, MaterialTheme.colorScheme.primaryContainer)
        else -> Triple("Inactive", Color(0xFF636366), Color(0xFF636366).copy(0.15f))
    }
    Surface(
        shape = RoundedCornerShape(50),
        color = bg,
    ) {
        Text(
            text = text,
            modifier = Modifier.padding(horizontal = 10.dp, vertical = 4.dp),
            style = MaterialTheme.typography.labelSmall.copy(
                color = fg,
                fontWeight = FontWeight.SemiBold,
                letterSpacing = 0.2.sp,
            ),
        )
    }
}
