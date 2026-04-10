package com.github.jvsena42.mandacaru.presentation.service

import android.app.Notification
import android.app.NotificationChannel
import android.app.NotificationManager
import android.app.PendingIntent
import android.app.Service
import android.content.Intent
import android.os.IBinder
import android.util.Log
import androidx.core.app.NotificationCompat
import com.github.jvsena42.mandacaru.R
import com.github.jvsena42.mandacaru.data.lightning.LightningNodeManager
import com.github.jvsena42.mandacaru.data.preferences.LightningPreferencesDataSource
import com.github.jvsena42.mandacaru.domain.floresta.FlorestaDaemon
import com.github.jvsena42.mandacaru.domain.model.LightningNodeState
import com.github.jvsena42.mandacaru.presentation.ui.screens.main.MainActivity
import kotlinx.coroutines.CoroutineScope
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.Job
import kotlinx.coroutines.SupervisorJob
import kotlinx.coroutines.delay
import kotlinx.coroutines.flow.first
import kotlinx.coroutines.launch
import kotlinx.coroutines.withTimeout
import org.koin.android.ext.android.inject
import org.lightningdevkit.ldknode.Network as LdkNetwork

/**
 * Long-running foreground service that manages the lifecycles of both
 * the Floresta Bitcoin node and the ldk-node Lightning node.
 *
 * ── Start order ───────────────────────────────────────────────────────────────
 *
 *   1. [FlorestaDaemon.start] — Starts the Floresta Rust binary.
 *   2. Poll [FlorestaDaemon.getBlockchainInfo] until the RPC server responds.
 *      The Electrum server becomes available at the same time as the RPC server.
 *   3. [LightningNodeManager.start] — Starts ldk-node, connecting to Floresta's
 *      Electrum server on localhost.
 *
 * ── Stop order ────────────────────────────────────────────────────────────────
 *
 *   1. [LightningNodeManager.stop] — Closes pending HTLCs gracefully.
 *   2. [FlorestaDaemon.stop] — Shuts down the Floresta Rust binary.
 *
 * Lightning is always stopped before Floresta because ldk-node holds open
 * sockets to the Electrum server; killing Floresta first would cause an
 * error cascade in ldk-node's chain monitor.
 *
 * ── Notification ──────────────────────────────────────────────────────────────
 *
 * A single persistent notification shows the combined node status:
 *   • While syncing Bitcoin blocks: "Syncing: 42.3%"
 *   • Once synced: "Synced · Block #859,012"
 *   • When a Lightning payment is received: "⚡ Payment received: 5,000 sats"
 *     (auto-reverts to sync status after 10 seconds)
 */
class FlorestaService : Service() {

    private val florestaDaemon: FlorestaDaemon by inject()
    private val lightningNodeManager: LightningNodeManager by inject()
    private val lightningPrefs: LightningPreferencesDataSource by inject()

    private val serviceScope = CoroutineScope(SupervisorJob() + Dispatchers.IO)
    private var pollingJob: Job? = null
    private var lightningEventJob: Job? = null

    // ── Service lifecycle ─────────────────────────────────────────────────────

    override fun onCreate() {
        super.onCreate()
        createNotificationChannel()
        startForeground(NOTIFICATION_ID, buildNotification("Starting Bitcoin node…"))
        startDaemonAndLightning()
        observeLightningEvents()
    }

    override fun onStartCommand(intent: Intent?, flags: Int, startId: Int): Int {
        when (intent?.action) {
            ACTION_STOP -> stopEverything()
        }
        return START_STICKY
    }

    override fun onBind(intent: Intent?): IBinder? = null

    override fun onDestroy() {
        super.onDestroy()
        stopEverything()
    }

    // ── Daemon lifecycle ──────────────────────────────────────────────────────

    private fun startDaemonAndLightning() {
        pollingJob = serviceScope.launch {
            try {
                florestaDaemon.start()
                Log.i(TAG, "Floresta daemon started")

                // ── Gate: wait until Floresta's RPC (and Electrum) server is ready ──
                // Poll getBlockchainInfo with exponential back-off (max 30s interval).
                var backoffMs = 500L
                var florestaReady = false
                while (!florestaReady) {
                    try {
                        florestaDaemon.getBlockchainInfo()
                        florestaReady = true
                        Log.i(TAG, "Floresta RPC ready — starting Lightning node")
                    } catch (e: Exception) {
                        delay(backoffMs)
                        backoffMs = (backoffMs * 2).coerceAtMost(30_000L)
                    }
                }

                // ── Start Lightning (if user has enabled it) ──────────────────────
                val prefs = lightningPrefs.preferencesFlow.first()
                if (prefs.isEnabled && prefs.isSetupComplete) {
                    val ldkNetwork = florestaDaemon.currentNetwork().toLdkNetwork()
                    if (ldkNetwork != null) {
                        lightningNodeManager.start(ldkNetwork, prefs)
                    } else {
                        Log.w(TAG, "Current network not supported by ldk-node (testnet4?)")
                        updateNotification("Lightning not available on this network")
                    }
                }

                // ── Begin polling loop for notification updates ────────────────────
                while (true) {
                    try {
                        val info = florestaDaemon.getBlockchainInfo()
                        val lnState = lightningNodeManager.state.value
                        updateNotification(buildStatusText(info, lnState))
                    } catch (e: Exception) {
                        Log.w(TAG, "Poll failed: ${e.message}")
                    }
                    delay(POLL_INTERVAL_MS)
                }
            } catch (e: Exception) {
                Log.e(TAG, "Fatal error in service job", e)
                updateNotification("Node error: ${e.message}")
            }
        }
    }

    /**
     * Observe Lightning events to surface payment-received feedback in the
     * notification, even when the app is in the background.
     */
    private fun observeLightningEvents() {
        lightningEventJob = serviceScope.launch {
            lightningNodeManager.events.collect { event ->
                when (event) {
                    is com.github.jvsena42.mandacaru.domain.model.LightningNodeEvent.PaymentReceived -> {
                        val sats = event.amountMsat / 1_000u
                        updateNotification("⚡ Received ${formatSats(sats.toLong())} sats")
                        // Revert to normal status after 10 seconds
                        delay(10_000)
                    }
                    else -> { /* Other events don't need notification feedback */ }
                }
            }
        }
    }

    private fun stopEverything() {
        pollingJob?.cancel()
        lightningEventJob?.cancel()

        serviceScope.launch {
            // Stop Lightning before Floresta (see class-level KDoc).
            try {
                withTimeout(15_000) { lightningNodeManager.stop() }
            } catch (e: Exception) {
                Log.e(TAG, "Lightning stop error", e)
            }
            try {
                withTimeout(10_000) { florestaDaemon.stop() }
            } catch (e: Exception) {
                Log.e(TAG, "Floresta stop error", e)
            }

            sendBroadcast(Intent(ACTION_EXIT_APP))
            stopSelf()
        }
    }

    // ── Notification helpers ──────────────────────────────────────────────────

    private fun createNotificationChannel() {
        val channel = NotificationChannel(
            CHANNEL_ID,
            "Bitcoin & Lightning Node",
            NotificationManager.IMPORTANCE_LOW,
        ).apply {
            description = "Node sync status and incoming payment alerts"
            setShowBadge(false)
        }
        getSystemService(NotificationManager::class.java)
            .createNotificationChannel(channel)
    }

    private fun buildNotification(contentText: String): Notification {
        val tapIntent = PendingIntent.getActivity(
            this, 0,
            Intent(this, MainActivity::class.java),
            PendingIntent.FLAG_UPDATE_CURRENT or PendingIntent.FLAG_IMMUTABLE,
        )
        val stopIntent = PendingIntent.getService(
            this, 0,
            Intent(this, FlorestaService::class.java).apply { action = ACTION_STOP },
            PendingIntent.FLAG_UPDATE_CURRENT or PendingIntent.FLAG_IMMUTABLE,
        )
        return NotificationCompat.Builder(this, CHANNEL_ID)
            .setSmallIcon(R.drawable.ic_notification_node)
            .setContentTitle("Mandacaru Node")
            .setContentText(contentText)
            .setContentIntent(tapIntent)
            .addAction(R.drawable.ic_stop, "Stop", stopIntent)
            .setOngoing(true)
            .setSilent(true)
            .build()
    }

    private fun updateNotification(text: String) {
        val nm = getSystemService(NotificationManager::class.java)
        nm.notify(NOTIFICATION_ID, buildNotification(text))
    }

    private fun buildStatusText(
        info: com.github.jvsena42.mandacaru.domain.floresta.BlockchainInfo,
        lnState: LightningNodeState,
    ): String {
        val syncPart = if (info.validationProgress >= 100.0) {
            "Block #${"%,d".format(info.height)}"
        } else {
            "Syncing: ${"%.1f".format(info.validationProgress)}%"
        }
        val lnPart = when (lnState) {
            is LightningNodeState.Running -> " · ⚡ LN ready"
            is LightningNodeState.Starting -> " · ⚡ LN starting"
            is LightningNodeState.WaitingForBitcoinNode -> " · ⚡ LN waiting"
            is LightningNodeState.Error -> " · ⚡ LN error"
            else -> ""
        }
        return syncPart + lnPart
    }

    private fun formatSats(sats: Long): String = "%,d".format(sats)

    companion object {
        private const val TAG = "FlorestaService"
        private const val CHANNEL_ID = "mandacaru_node"
        private const val NOTIFICATION_ID = 1
        private const val POLL_INTERVAL_MS = 10_000L
        const val ACTION_STOP = "com.github.jvsena42.mandacaru.STOP_SERVICE"
        const val ACTION_EXIT_APP = "com.github.jvsena42.mandacaru.EXIT_APP"
    }
}

// ── Network mapping ───────────────────────────────────────────────────────────

/**
 * Maps Floresta's network string (stored in preferences) to ldk-node's Network enum.
 *
 * Returns null for networks that ldk-node does not support (e.g. testnet4),
 * in which case the UI should surface a "Lightning unavailable" message.
 */
private fun String.toLdkNetwork(): LdkNetwork? = when (this.lowercase()) {
    "bitcoin", "mainnet" -> LdkNetwork.BITCOIN
    "testnet" -> LdkNetwork.TESTNET
    "signet" -> LdkNetwork.SIGNET
    "regtest" -> LdkNetwork.REGTEST
    else -> null
}
