package com.github.jvsena42.mandacaru.data.preferences

import androidx.datastore.preferences.core.booleanPreferencesKey
import androidx.datastore.preferences.core.stringPreferencesKey

/**
 * DataStore keys for all Lightning-related user preferences.
 *
 * None of these store secrets. The node mnemonic lives exclusively in
 * [KeystoreManager] (Android Keystore-backed EncryptedSharedPreferences).
 */
object LightningPreferenceKeys {

    /** Whether the user has explicitly enabled the Lightning node. */
    val LIGHTNING_ENABLED = booleanPreferencesKey("lightning_enabled")

    /**
     * Whether the first-time setup wizard has been completed.
     * Guards the LightningSetupScreen from showing again after initial setup.
     */
    val SETUP_COMPLETE = booleanPreferencesKey("lightning_setup_complete")

    // ── LSP (LSPS2) ──────────────────────────────────────────────────────────

    /** Whether to use an LSP for JIT inbound liquidity (LSPS2). */
    val LSP_ENABLED = booleanPreferencesKey("lightning_lsp_enabled")

    /**
     * LSP node address in "host:port" format, e.g. "45.79.192.236:9735".
     * Empty string means the LSP has not been configured.
     */
    val LSP_ADDRESS = stringPreferencesKey("lightning_lsp_address")

    /**
     * LSP node public key (hex-encoded compressed 33-byte pubkey).
     * Used to authenticate the LSPS2 connection.
     */
    val LSP_PUBKEY = stringPreferencesKey("lightning_lsp_pubkey")

    /**
     * Optional LSPS2 bearer token for LSPs that require authentication.
     * Empty string means no token.
     */
    val LSP_TOKEN = stringPreferencesKey("lightning_lsp_token")

    // ── Gossip ───────────────────────────────────────────────────────────────

    /**
     * Whether to use Rapid Gossip Sync (RGS) instead of full P2P gossip.
     * RGS downloads a compressed snapshot; faster startup, slight trust trade-off.
     */
    val USE_RGS_GOSSIP = booleanPreferencesKey("lightning_use_rgs")

    /**
     * URL of the RGS server snapshot endpoint.
     * The snapshot index is appended by ldk-node automatically.
     */
    val RGS_URL = stringPreferencesKey("lightning_rgs_url")

    // ── Node metadata ─────────────────────────────────────────────────────────

    /** Alias broadcasted to the LN gossip network (max 32 UTF-8 bytes). */
    val NODE_ALIAS = stringPreferencesKey("lightning_node_alias")

    // ── Default values ────────────────────────────────────────────────────────

    const val DEFAULT_NODE_ALIAS = "Mandacaru"

    /**
     * LDK's public Rapid Gossip Sync server.
     * ldk-node appends the timestamp of the last known snapshot automatically,
     * so this URL is stable and does not require manual version pinning.
     */
    const val DEFAULT_RGS_URL = "https://rapidsync.lightningdevkit.org/snapshot/0"
}
