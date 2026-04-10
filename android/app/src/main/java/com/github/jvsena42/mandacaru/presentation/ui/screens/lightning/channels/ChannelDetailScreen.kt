package com.github.jvsena42.mandacaru.presentation.ui.screens.lightning.channels

import androidx.compose.foundation.background
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.foundation.verticalScroll
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.filled.ArrowBack
import androidx.compose.material3.AlertDialog
import androidx.compose.material3.Button
import androidx.compose.material3.ButtonDefaults
import androidx.compose.material3.ExperimentalMaterial3Api
import androidx.compose.material3.HorizontalDivider
import androidx.compose.material3.Icon
import androidx.compose.material3.IconButton
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.OutlinedButton
import androidx.compose.material3.Surface
import androidx.compose.material3.Text
import androidx.compose.material3.TextButton
import androidx.compose.material3.TopAppBar
import androidx.compose.material3.TopAppBarDefaults
import androidx.compose.runtime.Composable
import androidx.compose.runtime.collectAsState
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.text.font.FontFamily
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.text.style.TextAlign
import androidx.compose.ui.unit.dp
import com.github.jvsena42.mandacaru.domain.model.LightningChannel
import com.github.jvsena42.mandacaru.presentation.ui.components.ChannelCapacityBar
import com.github.jvsena42.mandacaru.presentation.ui.screens.lightning.LightningViewModel
import org.koin.androidx.compose.koinViewModel

@OptIn(ExperimentalMaterial3Api::class)
@Composable
fun ChannelDetailScreen(
    channelId: String,
    onBack: () -> Unit,
    onChannelClosed: () -> Unit,
    viewModel: LightningViewModel = koinViewModel(),
) {
    val channels by viewModel.channels.collectAsState()
    val channel = channels.find { it.channelId == channelId }

    var showCloseDialog by remember { mutableStateOf(false) }
    var showForceCloseDialog by remember { mutableStateOf(false) }

    Column(
        modifier = Modifier
            .fillMaxSize()
            .background(MaterialTheme.colorScheme.background),
    ) {
        TopAppBar(
            title = {
                Text("Channel", style = MaterialTheme.typography.titleLarge.copy(fontWeight = FontWeight.SemiBold))
            },
            navigationIcon = {
                IconButton(onClick = onBack) {
                    Icon(Icons.Default.ArrowBack, contentDescription = "Back")
                }
            },
            colors = TopAppBarDefaults.topAppBarColors(containerColor = MaterialTheme.colorScheme.background),
        )

        if (channel == null) {
            Column(
                modifier = Modifier.fillMaxSize(),
                horizontalAlignment = Alignment.CenterHorizontally,
                verticalArrangement = Arrangement.Center,
            ) {
                Text("Channel not found", color = MaterialTheme.colorScheme.onBackground.copy(alpha = 0.4f))
            }
        } else {
            ChannelDetailContent(
                channel = channel,
                onCloseClick = { showCloseDialog = true },
                onForceCloseClick = { showForceCloseDialog = true },
            )
        }
    }

    // ── Cooperative close dialog ──────────────────────────────────────────────
    if (showCloseDialog && channel != null) {
        AlertDialog(
            onDismissRequest = { showCloseDialog = false },
            title = { Text("Close Channel", fontWeight = FontWeight.SemiBold) },
            text = {
                Text(
                    "This will cooperatively close the channel with your peer. " +
                    "Funds will be returned on-chain after a short delay. " +
                    "Your peer must be online."
                )
            },
            confirmButton = {
                Button(onClick = {
                    showCloseDialog = false
                    viewModel.closeChannel(channel.userChannelId, channel.counterpartyNodeId)
                    onChannelClosed()
                }) { Text("Close Channel") }
            },
            dismissButton = {
                TextButton(onClick = { showCloseDialog = false }) { Text("Cancel") }
            },
        )
    }

    // ── Force close dialog ────────────────────────────────────────────────────
    if (showForceCloseDialog && channel != null) {
        AlertDialog(
            onDismissRequest = { showForceCloseDialog = false },
            title = { Text("Force Close", fontWeight = FontWeight.SemiBold) },
            text = {
                Text(
                    "Force-closing broadcasts the latest commitment transaction unilaterally. " +
                    "Your funds will be time-locked on-chain for several days before they are spendable. " +
                    "Use only if your peer is unresponsive or uncooperative.",
                    color = MaterialTheme.colorScheme.onSurface,
                )
            },
            confirmButton = {
                Button(
                    onClick = {
                        showForceCloseDialog = false
                        viewModel.forceCloseChannel(channel.userChannelId, channel.counterpartyNodeId)
                        onChannelClosed()
                    },
                    colors = ButtonDefaults.buttonColors(containerColor = MaterialTheme.colorScheme.error),
                ) { Text("Force Close") }
            },
            dismissButton = {
                TextButton(onClick = { showForceCloseDialog = false }) { Text("Cancel") }
            },
        )
    }
}

@Composable
private fun ChannelDetailContent(
    channel: LightningChannel,
    onCloseClick: () -> Unit,
    onForceCloseClick: () -> Unit,
) {
    Column(
        modifier = Modifier
            .fillMaxSize()
            .verticalScroll(rememberScrollState())
            .padding(horizontal = 20.dp),
    ) {
        Spacer(Modifier.height(8.dp))

        // ── Capacity bar ──────────────────────────────────────────────────────
        Surface(
            shape = RoundedCornerShape(18.dp),
            color = MaterialTheme.colorScheme.surface,
            modifier = Modifier.fillMaxWidth(),
        ) {
            Column(modifier = Modifier.padding(18.dp)) {
                Text(
                    text = "${"%,d".format(channel.capacitySats.toLong())} sats total",
                    style = MaterialTheme.typography.titleMedium.copy(fontWeight = FontWeight.SemiBold),
                )
                Spacer(Modifier.height(12.dp))
                ChannelCapacityBar(channel = channel, showLabels = true)
            }
        }

        Spacer(Modifier.height(16.dp))

        // ── Detail rows ───────────────────────────────────────────────────────
        Surface(
            shape = RoundedCornerShape(18.dp),
            color = MaterialTheme.colorScheme.surface,
            modifier = Modifier.fillMaxWidth(),
        ) {
            Column {
                DetailRow("Status", channelStatusText(channel))
                HorizontalDivider(modifier = Modifier.padding(horizontal = 16.dp), thickness = 0.5.dp, color = MaterialTheme.colorScheme.outlineVariant.copy(0.4f))
                DetailRow("Direction", if (channel.isOutbound) "Outbound (we funded)" else "Inbound (peer funded)")
                HorizontalDivider(modifier = Modifier.padding(horizontal = 16.dp), thickness = 0.5.dp, color = MaterialTheme.colorScheme.outlineVariant.copy(0.4f))
                DetailRow("Visibility", if (channel.isPublic) "Public (announced)" else "Private (unannounced)")
                HorizontalDivider(modifier = Modifier.padding(horizontal = 16.dp), thickness = 0.5.dp, color = MaterialTheme.colorScheme.outlineVariant.copy(0.4f))
                DetailRow("Max HTLC out", "${"%,d".format(channel.nextOutboundHtlcLimitSats.toLong())} sats")
                if (channel.confirmations != null) {
                    HorizontalDivider(modifier = Modifier.padding(horizontal = 16.dp), thickness = 0.5.dp, color = MaterialTheme.colorScheme.outlineVariant.copy(0.4f))
                    DetailRow("Confirmations", "${channel.confirmations} / ${channel.confirmationsRequired ?: "?"}")
                }
                HorizontalDivider(modifier = Modifier.padding(horizontal = 16.dp), thickness = 0.5.dp, color = MaterialTheme.colorScheme.outlineVariant.copy(0.4f))
                DetailRow("Counterparty", channel.counterpartyNodeId, monospace = true, maxLines = 3)
                HorizontalDivider(modifier = Modifier.padding(horizontal = 16.dp), thickness = 0.5.dp, color = MaterialTheme.colorScheme.outlineVariant.copy(0.4f))
                DetailRow("Channel ID", channel.channelId, monospace = true, maxLines = 3)
            }
        }

        Spacer(Modifier.height(24.dp))

        // ── Actions ───────────────────────────────────────────────────────────
        OutlinedButton(
            onClick = onCloseClick,
            modifier = Modifier.fillMaxWidth().height(52.dp),
            shape = RoundedCornerShape(14.dp),
            enabled = channel.isReady,
        ) {
            Text("Cooperative Close", fontWeight = FontWeight.SemiBold)
        }

        Spacer(Modifier.height(10.dp))

        OutlinedButton(
            onClick = onForceCloseClick,
            modifier = Modifier.fillMaxWidth().height(52.dp),
            shape = RoundedCornerShape(14.dp),
            colors = ButtonDefaults.outlinedButtonColors(
                contentColor = MaterialTheme.colorScheme.error,
            ),
        ) {
            Text("Force Close", fontWeight = FontWeight.SemiBold)
        }

        Spacer(Modifier.height(32.dp))
    }
}

@Composable
private fun DetailRow(label: String, value: String, monospace: Boolean = false, maxLines: Int = 1) {
    Row(
        modifier = Modifier.fillMaxWidth().padding(horizontal = 16.dp, vertical = 14.dp),
        horizontalArrangement = Arrangement.SpaceBetween,
        verticalAlignment = Alignment.Top,
    ) {
        Text(
            text = label,
            style = MaterialTheme.typography.bodyMedium.copy(color = MaterialTheme.colorScheme.onSurface.copy(alpha = 0.55f)),
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

private fun channelStatusText(channel: LightningChannel): String = when {
    channel.isPendingOpen -> "Pending open (${channel.confirmationsRemaining ?: "?"} left)"
    channel.isUsable -> "Active"
    channel.isReady -> "Ready (peer offline)"
    else -> "Inactive"
}
