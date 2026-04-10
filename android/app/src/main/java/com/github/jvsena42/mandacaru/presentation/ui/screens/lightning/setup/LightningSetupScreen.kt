package com.github.jvsena42.mandacaru.presentation.ui.screens.lightning.setup

import android.app.Activity
import android.view.WindowManager
import androidx.compose.animation.AnimatedContent
import androidx.compose.animation.slideInHorizontally
import androidx.compose.animation.slideOutHorizontally
import androidx.compose.animation.togetherWith
import androidx.compose.foundation.background
import androidx.compose.foundation.border
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.ExperimentalLayoutApi
import androidx.compose.foundation.layout.FlowRow
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
import androidx.compose.foundation.text.KeyboardOptions
import androidx.compose.foundation.verticalScroll
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.filled.Bolt
import androidx.compose.material.icons.filled.Close
import androidx.compose.material.icons.filled.Lock
import androidx.compose.material.icons.filled.Shield
import androidx.compose.material.icons.filled.Visibility
import androidx.compose.material.icons.filled.VisibilityOff
import androidx.compose.material3.Button
import androidx.compose.material3.Checkbox
import androidx.compose.material3.ExperimentalMaterial3Api
import androidx.compose.material3.Icon
import androidx.compose.material3.IconButton
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.OutlinedTextField
import androidx.compose.material3.Surface
import androidx.compose.material3.Switch
import androidx.compose.material3.Text
import androidx.compose.material3.TextButton
import androidx.compose.material3.TopAppBar
import androidx.compose.material3.TopAppBarDefaults
import androidx.compose.runtime.Composable
import androidx.compose.runtime.DisposableEffect
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.collectAsState
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.rememberCoroutineScope
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.platform.LocalContext
import androidx.compose.ui.text.font.FontFamily
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.text.input.ImeAction
import androidx.compose.ui.text.style.TextAlign
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import com.github.jvsena42.mandacaru.data.lightning.KeystoreManager
import com.github.jvsena42.mandacaru.data.preferences.LightningPreferencesDataSource
import kotlinx.coroutines.launch
import org.koin.androidx.compose.koinInject

/**
 * First-time Lightning setup wizard — three steps:
 *
 *   Step 1 — Introduction
 *     • Explains what LN is and what it requires.
 *     • Lists the trade-offs clearly (LSP trust, connectivity, etc.).
 *
 *   Step 2 — Backup mnemonic
 *     • Generates and displays the 12-word BIP-39 mnemonic.
 *     • Screen is FLAG_SECURE — the mnemonic won't appear in screenshots
 *       or recent apps thumbnails.
 *     • User must acknowledge they have backed it up before proceeding.
 *
 *   Step 3 — Optional LSP configuration
 *     • The user can configure an LSP (address + pubkey) for JIT inbound
 *       liquidity, or skip this step (manual channel management only).
 *     • RGS gossip URL is editable for advanced users.
 *
 * Completing the wizard writes [LightningPreferenceKeys.SETUP_COMPLETE] = true
 * and [LightningPreferenceKeys.LIGHTNING_ENABLED] = true, which causes
 * [FlorestaService] to start ldk-node on the next (re)start.
 */
@OptIn(ExperimentalMaterial3Api::class, ExperimentalLayoutApi::class)
@Composable
fun LightningSetupScreen(
    onSetupComplete: () -> Unit,
    onDismiss: () -> Unit,
    keystoreManager: KeystoreManager = koinInject(),
    preferencesDataSource: LightningPreferencesDataSource = koinInject(),
) {
    var step by remember { mutableStateOf(0) }
    val scope = rememberCoroutineScope()

    // Mnemonic — generated once and cached in this composable's lifetime.
    // KeystoreManager persists it; we only read it here for display.
    val mnemonicWords: List<String> = remember {
        keystoreManager.getOrGenerateMnemonic().toString().split(" ")
    }

    // LSP config state (step 3)
    var lspEnabled by remember { mutableStateOf(false) }
    var lspAddress by remember { mutableStateOf("") }
    var lspPubkey by remember { mutableStateOf("") }
    var lspToken by remember { mutableStateOf("") }
    var rgsUrl by remember { mutableStateOf(com.github.jvsena42.mandacaru.data.preferences.LightningPreferenceKeys.DEFAULT_RGS_URL) }

    Column(
        modifier = Modifier
            .fillMaxSize()
            .background(MaterialTheme.colorScheme.background),
    ) {
        TopAppBar(
            title = { Text("Lightning Setup", style = MaterialTheme.typography.titleLarge.copy(fontWeight = FontWeight.SemiBold)) },
            navigationIcon = {
                IconButton(onClick = onDismiss) { Icon(Icons.Default.Close, contentDescription = "Cancel") }
            },
            colors = TopAppBarDefaults.topAppBarColors(containerColor = MaterialTheme.colorScheme.background),
        )

        AnimatedContent(
            targetState = step,
            transitionSpec = {
                if (targetState > initialState)
                    slideInHorizontally { it } togetherWith slideOutHorizontally { -it }
                else
                    slideInHorizontally { -it } togetherWith slideOutHorizontally { it }
            },
            modifier = Modifier.weight(1f),
            label = "setup_step",
        ) { currentStep ->
            when (currentStep) {
                0 -> IntroStep(onNext = { step = 1 })
                1 -> MnemonicStep(
                    words = mnemonicWords,
                    onNext = { step = 2 },
                    onBack = { step = 0 },
                )
                2 -> LspStep(
                    lspEnabled = lspEnabled,
                    lspAddress = lspAddress,
                    lspPubkey = lspPubkey,
                    lspToken = lspToken,
                    rgsUrl = rgsUrl,
                    onLspEnabledChange = { lspEnabled = it },
                    onLspAddressChange = { lspAddress = it },
                    onLspPubkeyChange = { lspPubkey = it },
                    onLspTokenChange = { lspToken = it },
                    onRgsUrlChange = { rgsUrl = it },
                    onBack = { step = 1 },
                    onFinish = {
                        scope.launch {
                            preferencesDataSource.setLsp(lspEnabled, lspAddress, lspPubkey, lspToken)
                            preferencesDataSource.setGossipConfig(useRgs = true, rgsUrl = rgsUrl)
                            preferencesDataSource.setEnabled(true)
                            preferencesDataSource.setSetupComplete(true)
                            onSetupComplete()
                        }
                    },
                )
            }
        }
    }
}

// ── Step 1: Introduction ──────────────────────────────────────────────────────

@Composable
private fun IntroStep(onNext: () -> Unit) {
    Column(
        modifier = Modifier
            .fillMaxSize()
            .verticalScroll(rememberScrollState())
            .padding(horizontal = 28.dp),
        horizontalAlignment = Alignment.CenterHorizontally,
    ) {
        Spacer(Modifier.height(20.dp))

        Icon(
            Icons.Default.Bolt,
            contentDescription = null,
            tint = MaterialTheme.colorScheme.primary,
            modifier = Modifier.size(56.dp),
        )
        Spacer(Modifier.height(16.dp))

        Text(
            "Add Lightning",
            style = MaterialTheme.typography.headlineMedium.copy(fontWeight = FontWeight.Bold),
            textAlign = TextAlign.Center,
        )
        Spacer(Modifier.height(8.dp))
        Text(
            "Instant, near-zero-fee Bitcoin payments on top of your full node.",
            style = MaterialTheme.typography.bodyLarge.copy(
                color = MaterialTheme.colorScheme.onBackground.copy(alpha = 0.65f),
            ),
            textAlign = TextAlign.Center,
        )

        Spacer(Modifier.height(28.dp))

        FeatureRow(Icons.Default.Shield, "Self-sovereign", "ldk-node runs on your device, using your Floresta node as the chain source. No third-party server sees your payments.")
        Spacer(Modifier.height(14.dp))
        FeatureRow(Icons.Default.Lock, "Encrypted keys", "Your 12-word seed is generated on-device and stored in Android Keystore (hardware-backed encryption).")
        Spacer(Modifier.height(14.dp))
        FeatureRow(Icons.Default.Bolt, "LSP inbound liquidity", "An optional Lightning Service Provider can open JIT channels so you can receive payments even without outbound liquidity.")

        Spacer(Modifier.height(20.dp))

        Surface(
            shape = RoundedCornerShape(14.dp),
            color = MaterialTheme.colorScheme.secondaryContainer.copy(alpha = 0.5f),
        ) {
            Text(
                "Note: Lightning requires your Bitcoin node to be synced and running. ldk-node will start automatically alongside Floresta.",
                modifier = Modifier.padding(14.dp),
                style = MaterialTheme.typography.bodySmall.copy(color = MaterialTheme.colorScheme.onSecondaryContainer),
                textAlign = TextAlign.Center,
            )
        }

        Spacer(Modifier.weight(1f))
        Spacer(Modifier.height(16.dp))

        Button(
            onClick = onNext,
            modifier = Modifier.fillMaxWidth().height(56.dp),
            shape = RoundedCornerShape(16.dp),
        ) {
            Text("Continue", style = MaterialTheme.typography.titleMedium.copy(fontWeight = FontWeight.SemiBold, letterSpacing = 0.sp))
        }
        Spacer(Modifier.height(20.dp))
    }
}

@Composable
private fun FeatureRow(
    icon: androidx.compose.ui.graphics.vector.ImageVector,
    title: String,
    body: String,
) {
    Row(
        modifier = Modifier.fillMaxWidth(),
        horizontalArrangement = Arrangement.spacedBy(14.dp),
        verticalAlignment = Alignment.Top,
    ) {
        Icon(icon, contentDescription = null, tint = MaterialTheme.colorScheme.primary, modifier = Modifier.size(22.dp))
        Column {
            Text(title, style = MaterialTheme.typography.bodyMedium.copy(fontWeight = FontWeight.SemiBold))
            Text(body, style = MaterialTheme.typography.bodySmall.copy(color = MaterialTheme.colorScheme.onBackground.copy(alpha = 0.6f)))
        }
    }
}

// ── Step 2: Mnemonic backup ───────────────────────────────────────────────────

@OptIn(ExperimentalLayoutApi::class)
@Composable
private fun MnemonicStep(
    words: List<String>,
    onNext: () -> Unit,
    onBack: () -> Unit,
) {
    // Block screenshots while mnemonic is visible
    val context = LocalContext.current
    DisposableEffect(Unit) {
        val window = (context as? Activity)?.window
        window?.addFlags(WindowManager.LayoutParams.FLAG_SECURE)
        onDispose { window?.clearFlags(WindowManager.LayoutParams.FLAG_SECURE) }
    }

    var confirmed by remember { mutableStateOf(false) }
    var showWords by remember { mutableStateOf(false) }

    Column(
        modifier = Modifier
            .fillMaxSize()
            .verticalScroll(rememberScrollState())
            .padding(horizontal = 24.dp),
        horizontalAlignment = Alignment.CenterHorizontally,
    ) {
        Spacer(Modifier.height(12.dp))

        Text("Backup Your Seed", style = MaterialTheme.typography.headlineSmall.copy(fontWeight = FontWeight.Bold))
        Spacer(Modifier.height(8.dp))
        Text(
            "Write down these 12 words in order and store them safely. They are the only way to recover your Lightning funds if you lose this device.",
            style = MaterialTheme.typography.bodyMedium.copy(color = MaterialTheme.colorScheme.onBackground.copy(alpha = 0.65f)),
            textAlign = TextAlign.Center,
        )

        Spacer(Modifier.height(20.dp))

        // Mnemonic grid — blurred until user taps reveal
        Box(
            modifier = Modifier
                .fillMaxWidth()
                .background(MaterialTheme.colorScheme.surface, RoundedCornerShape(18.dp))
                .padding(16.dp),
        ) {
            if (showWords) {
                FlowRow(
                    modifier = Modifier.fillMaxWidth(),
                    horizontalArrangement = Arrangement.spacedBy(8.dp),
                    verticalArrangement = Arrangement.spacedBy(8.dp),
                    maxItemsInEachRow = 3,
                ) {
                    words.forEachIndexed { index, word ->
                        WordChip(index = index + 1, word = word)
                    }
                }
            } else {
                Column(
                    modifier = Modifier.fillMaxWidth().padding(vertical = 24.dp),
                    horizontalAlignment = Alignment.CenterHorizontally,
                    verticalArrangement = Arrangement.spacedBy(8.dp),
                ) {
                    Icon(Icons.Default.VisibilityOff, contentDescription = null, modifier = Modifier.size(32.dp), tint = MaterialTheme.colorScheme.onSurface.copy(alpha = 0.4f))
                    Text("Tap to reveal", style = MaterialTheme.typography.bodyMedium.copy(color = MaterialTheme.colorScheme.onSurface.copy(alpha = 0.5f)))
                    TextButton(onClick = { showWords = true }) { Text("Show seed phrase") }
                }
            }
        }

        Spacer(Modifier.height(16.dp))

        // Acknowledgement checkbox
        Row(
            modifier = Modifier.fillMaxWidth(),
            verticalAlignment = Alignment.CenterVertically,
            horizontalArrangement = Arrangement.spacedBy(8.dp),
        ) {
            Checkbox(checked = confirmed, onCheckedChange = { confirmed = it })
            Text(
                "I have written down my 12-word seed phrase in a safe location.",
                style = MaterialTheme.typography.bodyMedium,
            )
        }

        Spacer(Modifier.weight(1f))
        Spacer(Modifier.height(16.dp))

        Row(
            modifier = Modifier.fillMaxWidth(),
            horizontalArrangement = Arrangement.spacedBy(12.dp),
        ) {
            TextButton(onClick = onBack, modifier = Modifier.weight(1f)) { Text("Back") }
            Button(
                onClick = onNext,
                enabled = confirmed && showWords,
                modifier = Modifier.weight(2f).height(52.dp),
                shape = RoundedCornerShape(14.dp),
            ) {
                Text("I've backed it up", fontWeight = FontWeight.SemiBold)
            }
        }
        Spacer(Modifier.height(20.dp))
    }
}

@Composable
private fun WordChip(index: Int, word: String) {
    Row(
        modifier = Modifier
            .border(1.dp, MaterialTheme.colorScheme.outline.copy(alpha = 0.3f), RoundedCornerShape(8.dp))
            .padding(horizontal = 10.dp, vertical = 6.dp),
        horizontalArrangement = Arrangement.spacedBy(6.dp),
        verticalAlignment = Alignment.CenterVertically,
    ) {
        Text(
            text = "$index.",
            style = MaterialTheme.typography.labelSmall.copy(color = MaterialTheme.colorScheme.onSurface.copy(alpha = 0.4f)),
        )
        Text(
            text = word,
            style = MaterialTheme.typography.bodyMedium.copy(
                fontFamily = FontFamily.Monospace,
                fontWeight = FontWeight.Medium,
            ),
        )
    }
}

// ── Step 3: LSP configuration ─────────────────────────────────────────────────

@Composable
private fun LspStep(
    lspEnabled: Boolean,
    lspAddress: String,
    lspPubkey: String,
    lspToken: String,
    rgsUrl: String,
    onLspEnabledChange: (Boolean) -> Unit,
    onLspAddressChange: (String) -> Unit,
    onLspPubkeyChange: (String) -> Unit,
    onLspTokenChange: (String) -> Unit,
    onRgsUrlChange: (String) -> Unit,
    onBack: () -> Unit,
    onFinish: () -> Unit,
) {
    Column(
        modifier = Modifier
            .fillMaxSize()
            .verticalScroll(rememberScrollState())
            .padding(horizontal = 20.dp)
            .imePadding(),
        verticalArrangement = Arrangement.spacedBy(16.dp),
    ) {
        Spacer(Modifier.height(8.dp))

        Text("Inbound Liquidity", style = MaterialTheme.typography.headlineSmall.copy(fontWeight = FontWeight.Bold))
        Text(
            "Without an LSP, you can only receive payments if you already have inbound capacity (someone has opened a channel to you). An LSP opens a channel automatically when a payment arrives (JIT channel, LSPS2 protocol).",
            style = MaterialTheme.typography.bodyMedium.copy(color = MaterialTheme.colorScheme.onBackground.copy(alpha = 0.65f)),
        )

        // LSP toggle
        Surface(
            shape = RoundedCornerShape(16.dp),
            color = MaterialTheme.colorScheme.surface,
        ) {
            Row(
                modifier = Modifier.fillMaxWidth().padding(horizontal = 16.dp, vertical = 14.dp),
                horizontalArrangement = Arrangement.SpaceBetween,
                verticalAlignment = Alignment.CenterVertically,
            ) {
                Column(modifier = Modifier.weight(1f)) {
                    Text("Use LSP for inbound liquidity", style = MaterialTheme.typography.bodyMedium.copy(fontWeight = FontWeight.SemiBold))
                    Text("Recommended for mobile", style = MaterialTheme.typography.bodySmall.copy(color = MaterialTheme.colorScheme.onSurface.copy(alpha = 0.5f)))
                }
                Switch(checked = lspEnabled, onCheckedChange = onLspEnabledChange)
            }
        }

        if (lspEnabled) {
            OutlinedTextField(
                value = lspAddress,
                onValueChange = onLspAddressChange,
                label = { Text("LSP Address") },
                placeholder = { Text("host:port") },
                keyboardOptions = KeyboardOptions(imeAction = ImeAction.Next),
                modifier = Modifier.fillMaxWidth(),
                shape = RoundedCornerShape(14.dp),
            )
            OutlinedTextField(
                value = lspPubkey,
                onValueChange = onLspPubkeyChange,
                label = { Text("LSP Node Public Key") },
                placeholder = { Text("02abc… (66 hex chars)") },
                keyboardOptions = KeyboardOptions(imeAction = ImeAction.Next),
                modifier = Modifier.fillMaxWidth(),
                shape = RoundedCornerShape(14.dp),
                minLines = 2,
                maxLines = 3,
            )
            OutlinedTextField(
                value = lspToken,
                onValueChange = onLspTokenChange,
                label = { Text("LSP Token (optional)") },
                keyboardOptions = KeyboardOptions(imeAction = ImeAction.Next),
                modifier = Modifier.fillMaxWidth(),
                shape = RoundedCornerShape(14.dp),
            )
        }

        // RGS URL
        OutlinedTextField(
            value = rgsUrl,
            onValueChange = onRgsUrlChange,
            label = { Text("RGS Gossip URL") },
            supportingText = { Text("Rapid Gossip Sync server — speeds up payment routing on first start") },
            keyboardOptions = KeyboardOptions(imeAction = ImeAction.Done),
            modifier = Modifier.fillMaxWidth(),
            shape = RoundedCornerShape(14.dp),
        )

        Spacer(Modifier.height(8.dp))

        Row(
            modifier = Modifier.fillMaxWidth(),
            horizontalArrangement = Arrangement.spacedBy(12.dp),
        ) {
            TextButton(onClick = onBack, modifier = Modifier.weight(1f)) { Text("Back") }
            Button(
                onClick = onFinish,
                modifier = Modifier.weight(2f).height(52.dp),
                shape = RoundedCornerShape(14.dp),
            ) {
                Text("Enable Lightning", fontWeight = FontWeight.SemiBold, letterSpacing = 0.sp)
            }
        }
        Spacer(Modifier.height(24.dp))
    }
}
