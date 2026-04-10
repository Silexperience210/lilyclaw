package com.github.jvsena42.mandacaru.data.preferences

import androidx.datastore.core.DataStore
import androidx.datastore.preferences.core.Preferences
import androidx.datastore.preferences.core.edit
import kotlinx.coroutines.flow.Flow
import kotlinx.coroutines.flow.map

/**
 * Snapshot of all Lightning preferences read from DataStore.
 *
 * Intentionally a plain data class — no logic, no coroutines — so it can
 * be passed synchronously to [LightningNodeManager.start] after being
 * collected from the flow once.
 */
data class LightningPreferences(
    val isEnabled: Boolean = false,
    val isSetupComplete: Boolean = false,
    val lspEnabled: Boolean = false,
    val lspAddress: String = "",
    val lspPubkey: String = "",
    val lspToken: String = "",
    val useRgsGossip: Boolean = true,
    val rgsUrl: String = LightningPreferenceKeys.DEFAULT_RGS_URL,
    val nodeAlias: String = LightningPreferenceKeys.DEFAULT_NODE_ALIAS,
) {
    val isLspConfigured: Boolean
        get() = lspEnabled && lspAddress.isNotBlank() && lspPubkey.isNotBlank()
}

/**
 * Reads and writes Lightning preferences in DataStore.
 *
 * Injected as a singleton via Koin; shares the same [DataStore] instance
 * as the rest of the app to avoid concurrent write conflicts.
 */
class LightningPreferencesDataSource(private val dataStore: DataStore<Preferences>) {

    val preferencesFlow: Flow<LightningPreferences> = dataStore.data.map { prefs ->
        LightningPreferences(
            isEnabled = prefs[LightningPreferenceKeys.LIGHTNING_ENABLED] ?: false,
            isSetupComplete = prefs[LightningPreferenceKeys.SETUP_COMPLETE] ?: false,
            lspEnabled = prefs[LightningPreferenceKeys.LSP_ENABLED] ?: false,
            lspAddress = prefs[LightningPreferenceKeys.LSP_ADDRESS] ?: "",
            lspPubkey = prefs[LightningPreferenceKeys.LSP_PUBKEY] ?: "",
            lspToken = prefs[LightningPreferenceKeys.LSP_TOKEN] ?: "",
            useRgsGossip = prefs[LightningPreferenceKeys.USE_RGS_GOSSIP] ?: true,
            rgsUrl = prefs[LightningPreferenceKeys.RGS_URL]
                ?.takeIf { it.isNotBlank() }
                ?: LightningPreferenceKeys.DEFAULT_RGS_URL,
            nodeAlias = prefs[LightningPreferenceKeys.NODE_ALIAS]
                ?.takeIf { it.isNotBlank() }
                ?: LightningPreferenceKeys.DEFAULT_NODE_ALIAS,
        )
    }

    suspend fun setEnabled(enabled: Boolean) {
        dataStore.edit { it[LightningPreferenceKeys.LIGHTNING_ENABLED] = enabled }
    }

    suspend fun setSetupComplete(complete: Boolean) {
        dataStore.edit { it[LightningPreferenceKeys.SETUP_COMPLETE] = complete }
    }

    suspend fun setLsp(enabled: Boolean, address: String, pubkey: String, token: String) {
        dataStore.edit { prefs ->
            prefs[LightningPreferenceKeys.LSP_ENABLED] = enabled
            prefs[LightningPreferenceKeys.LSP_ADDRESS] = address.trim()
            prefs[LightningPreferenceKeys.LSP_PUBKEY] = pubkey.trim()
            prefs[LightningPreferenceKeys.LSP_TOKEN] = token.trim()
        }
    }

    suspend fun setGossipConfig(useRgs: Boolean, rgsUrl: String) {
        dataStore.edit { prefs ->
            prefs[LightningPreferenceKeys.USE_RGS_GOSSIP] = useRgs
            prefs[LightningPreferenceKeys.RGS_URL] = rgsUrl.trim()
        }
    }

    suspend fun setNodeAlias(alias: String) {
        // Enforce 32-byte limit (LN spec: node_announcement alias field)
        val safe = alias.take(32)
        dataStore.edit { it[LightningPreferenceKeys.NODE_ALIAS] = safe }
    }
}
