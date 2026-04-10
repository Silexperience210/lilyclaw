package com.github.jvsena42.mandacaru.presentation.ui.screens.lightning.channels

import androidx.compose.animation.AnimatedVisibility
import androidx.compose.animation.fadeIn
import androidx.compose.animation.fadeOut
import androidx.compose.foundation.background
import androidx.compose.foundation.layout.Arrangement
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
import androidx.compose.material3.Button
import androidx.compose.material3.CircularProgressIndicator
import androidx.compose.material3.ExperimentalMaterial3Api
import androidx.compose.material3.Icon
import androidx.compose.material3.IconButton
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.OutlinedTextField
import androidx.compose.material3.Surface
import androidx.compose.material3.Text
import androidx.compose.material3.TopAppBar
import androidx.compose.material3.TopAppBarDefaults
import androidx.compose.runtime.Composable
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.collectAsState
import androidx.compose.runtime.getValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.platform.LocalFocusManager
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.text.input.ImeAction
import androidx.compose.ui.text.input.KeyboardType
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import com.github.jvsena42.mandacaru.presentation.ui.screens.lightning.LightningViewModel
import org.koin.androidx.compose.koinViewModel

/**
 * Open a new Lightning channel.
 *
 * Security notes:
 *   • The node ID is a 33-byte compressed public key (66 hex chars).
 *     We show a label explaining this so the user knows what to paste.
 *   • The address must be "host:port". We do no DNS lookups here — that
 *     happens inside ldk-node's connect call.
 *   • The minimum channel size (20,000 sats) guards against dust channels
 *     that would be uneconomical to close on-chain.
 *   • 0-conf is never offered as an option for arbitrary peers — it is only
 *     available to the configured LSP (enforced at the ldk-node Config level).
 */
@OptIn(ExperimentalMaterial3Api::class)
@Composable
fun OpenChannelScreen(
    onBack: () -> Unit,
    onChannelOpened: () -> Unit,
    viewModel: LightningViewModel = koinViewModel(),
) {
    val state by viewModel.openChannelState.collectAsState()
    val balance by viewModel.balance.collectAsState()
    val focusManager = LocalFocusManager.current

    // Navigate back when open was successful
    LaunchedEffect(state.success) {
        if (state.success) onChannelOpened()
    }

    Column(
        modifier = Modifier
            .fillMaxSize()
            .background(MaterialTheme.colorScheme.background),
    ) {
        TopAppBar(
            title = { Text("Open Channel", style = MaterialTheme.typography.titleLarge.copy(fontWeight = FontWeight.SemiBold)) },
            navigationIcon = {
                IconButton(onClick = onBack) { Icon(Icons.Default.Close, contentDescription = "Close") }
            },
            colors = TopAppBarDefaults.topAppBarColors(containerColor = MaterialTheme.colorScheme.background),
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

            // Available on-chain balance
            Surface(
                shape = RoundedCornerShape(12.dp),
                color = MaterialTheme.colorScheme.surfaceVariant,
            ) {
                Row(
                    modifier = Modifier.fillMaxWidth().padding(horizontal = 16.dp, vertical = 12.dp),
                    horizontalArrangement = Arrangement.SpaceBetween,
                ) {
                    Text("Available on-chain", style = MaterialTheme.typography.bodyMedium.copy(color = MaterialTheme.colorScheme.onSurfaceVariant))
                    Text(
                        "${"%,d".format(balance.spendableOnchainSats.toLong())} sats",
                        style = MaterialTheme.typography.bodyMedium.copy(fontWeight = FontWeight.SemiBold),
                    )
                }
            }

            // Node ID field
            OutlinedTextField(
                value = state.nodeId,
                onValueChange = viewModel::onOpenChannelNodeId,
                label = { Text("Node Public Key") },
                placeholder = { Text("02abc… (66 hex chars)") },
                supportingText = { Text("33-byte compressed public key of the remote node") },
                isError = state.error != null && state.nodeId.isBlank(),
                keyboardOptions = KeyboardOptions(imeAction = ImeAction.Next),
                modifier = Modifier.fillMaxWidth(),
                shape = RoundedCornerShape(16.dp),
                minLines = 2,
                maxLines = 3,
            )

            // Peer address field
            OutlinedTextField(
                value = state.address,
                onValueChange = viewModel::onOpenChannelAddress,
                label = { Text("Peer Address") },
                placeholder = { Text("host:port  (e.g. 1.2.3.4:9735)") },
                isError = state.error != null && state.address.isBlank(),
                keyboardOptions = KeyboardOptions(imeAction = ImeAction.Next),
                modifier = Modifier.fillMaxWidth(),
                shape = RoundedCornerShape(16.dp),
            )

            // Channel amount field
            OutlinedTextField(
                value = state.amountSats?.toString() ?: "",
                onValueChange = viewModel::onOpenChannelAmount,
                label = { Text("Channel amount") },
                suffix = { Text("sats") },
                supportingText = { Text("Minimum 20,000 sats") },
                isError = state.error != null && state.amountSats == null,
                keyboardOptions = KeyboardOptions(keyboardType = KeyboardType.Number, imeAction = ImeAction.Done),
                keyboardActions = KeyboardActions(onDone = { focusManager.clearFocus() }),
                modifier = Modifier.fillMaxWidth(),
                shape = RoundedCornerShape(16.dp),
            )

            // Private channel note
            Text(
                text = "Channels opened here are private (unannounced) by default, matching the mobile self-sovereign model. For routing node usage, edit the source.",
                style = MaterialTheme.typography.bodySmall.copy(
                    color = MaterialTheme.colorScheme.onBackground.copy(alpha = 0.45f),
                ),
            )

            // Error
            AnimatedVisibility(visible = state.error != null, enter = fadeIn(), exit = fadeOut()) {
                Surface(
                    shape = RoundedCornerShape(12.dp),
                    color = MaterialTheme.colorScheme.errorContainer,
                ) {
                    Text(
                        text = state.error ?: "",
                        modifier = Modifier.padding(horizontal = 16.dp, vertical = 12.dp),
                        style = MaterialTheme.typography.bodyMedium.copy(color = MaterialTheme.colorScheme.onErrorContainer),
                    )
                }
            }

            Spacer(Modifier.height(8.dp))
        }

        // Confirm button
        Button(
            onClick = {
                focusManager.clearFocus()
                viewModel.openChannel()
            },
            enabled = !state.isOpening,
            modifier = Modifier
                .fillMaxWidth()
                .padding(horizontal = 20.dp, vertical = 16.dp)
                .height(56.dp),
            shape = RoundedCornerShape(16.dp),
        ) {
            if (state.isOpening) {
                CircularProgressIndicator(
                    modifier = Modifier.size(20.dp),
                    color = MaterialTheme.colorScheme.onPrimary,
                    strokeWidth = 2.dp,
                )
            } else {
                Text("Open Channel", style = MaterialTheme.typography.titleMedium.copy(fontWeight = FontWeight.SemiBold, letterSpacing = 0.sp))
            }
        }
    }
}
