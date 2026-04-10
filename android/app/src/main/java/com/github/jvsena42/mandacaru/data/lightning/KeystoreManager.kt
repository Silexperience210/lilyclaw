package com.github.jvsena42.mandacaru.data.lightning

import android.content.Context
import android.util.Log
import androidx.security.crypto.EncryptedSharedPreferences
import androidx.security.crypto.MasterKey
import org.lightningdevkit.ldknode.Mnemonic

/**
 * Manages the BIP-39 mnemonic used to derive the Lightning node's key material.
 *
 * ── Security model ────────────────────────────────────────────────────────────
 *
 * The mnemonic is generated once using ldk-node's [Mnemonic.generate] (which
 * internally sources entropy from the OS CSPRNG). It is then encrypted at rest
 * with AES-256-GCM, using a key that lives in the Android Keystore hardware
 * module (StrongBox if available, TEE otherwise). The ciphertext is stored in
 * EncryptedSharedPreferences.
 *
 * The plaintext mnemonic is:
 *   • Never written to Logcat (not even at DEBUG level).
 *   • Never included in crash reports.
 *   • Only surfaced to the UI when the user explicitly requests a backup.
 *   • The backup screen must set [WindowManager.LayoutParams.FLAG_SECURE].
 *
 * The mnemonic is passed to ldk-node's Builder once per node start; ldk-node
 * derives the HD wallet and channel keys internally and does not expose them.
 *
 * ── Key hierarchy ─────────────────────────────────────────────────────────────
 *
 * BIP-39 mnemonic (12 words)
 *   └─ BIP-32 root key
 *        ├─ BDK on-chain wallet (m/84'/0'/0')
 *        └─ LDK channel key material
 *
 * The same mnemonic is used for both on-chain and Lightning keys, consistent
 * with ldk-node's unified wallet design.
 */
class KeystoreManager(private val context: Context) {

    private val masterKey: MasterKey by lazy {
        MasterKey.Builder(context)
            .setKeyScheme(MasterKey.KeyScheme.AES256_GCM)
            // Request StrongBox (dedicated security chip) when available;
            // falls back to TEE transparently.
            .setRequestStrongBoxBacked(true)
            .build()
    }

    private val encryptedPrefs by lazy {
        EncryptedSharedPreferences.create(
            context,
            PREFS_FILE,
            masterKey,
            EncryptedSharedPreferences.PrefKeyEncryptionScheme.AES256_SIV,
            EncryptedSharedPreferences.PrefValueEncryptionScheme.AES256_GCM,
        )
    }

    /**
     * Returns the existing mnemonic phrase, or generates and persists a new one
     * if this is the first launch.
     *
     * This must be called from a coroutine (I/O dispatcher) because key
     * generation involves disk I/O via EncryptedSharedPreferences.
     */
    fun getOrGenerateMnemonic(): Mnemonic {
        val stored = encryptedPrefs.getString(KEY_MNEMONIC, null)
        if (stored != null) return Mnemonic(stored)

        val generated = Mnemonic.generate()
        encryptedPrefs.edit()
            .putString(KEY_MNEMONIC, generated.toString())
            .apply()

        Log.i(TAG, "New Lightning mnemonic generated and stored.")
        return generated
    }

    /**
     * Returns the mnemonic for display in the backup screen, or null if no
     * mnemonic has been generated yet (should not happen in normal flow).
     *
     * The caller is responsible for FLAG_SECURE on the displaying window.
     */
    fun getMnemonicForBackup(): Mnemonic? {
        val stored = encryptedPrefs.getString(KEY_MNEMONIC, null) ?: return null
        return Mnemonic(stored)
    }

    /** True if a mnemonic has already been persisted (setup was completed). */
    fun hasMnemonic(): Boolean =
        encryptedPrefs.getString(KEY_MNEMONIC, null) != null

    /**
     * Erases the mnemonic from encrypted storage.
     *
     * WARNING: Calling this without first closing all Lightning channels will
     * result in permanent fund loss. This method must only be called from a
     * dedicated "wipe node" user flow that includes a prominent warning.
     */
    fun eraseMnemonic() {
        encryptedPrefs.edit().remove(KEY_MNEMONIC).apply()
        Log.w(TAG, "Lightning mnemonic erased from storage.")
    }

    companion object {
        private const val TAG = "KeystoreManager"
        private const val PREFS_FILE = "lightning_keystore_v1"
        private const val KEY_MNEMONIC = "bip39_mnemonic"
    }
}
